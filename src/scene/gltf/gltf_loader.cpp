#include "gltf_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

// PNG + JPEG covers nearly every glTF exporter (WebP / KTX2 are reported as
// unsupported).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "CGL/CGL.h"
#include "CGL/matrix4x4.h"
#include "CGL/vector4D.h"

#include "scene/bbox.h"
#include "scene/collada/camera_info.h"
#include "scene/collada/light_info.h"
#include "scene/collada/material_info.h"
#include "scene/collada/trimesh_info.h"
#include "pathtracer/bsdf.h"

#define GLTF_LOG(s) std::cerr << "[glTF Loader] " << s << std::endl

namespace CGL { namespace GLTF {

using namespace Collada;

namespace {

// Metallic surfaces at or below this roughness become a perfect MirrorBSDF.
const float kMirrorRoughness = 0.1f;

// glTF light intensities are photometric (candela, lux). The renderer works
// in radiometric units (like emission strengths, which glTF and Blender
// treat as radiance), so divide by the luminous efficacy 683 lm/W. This is
// the same conversion Blender's glTF importer uses; checked against Cycles
// (direct lighting on a diffuse wall matched to within 1%).
const float kLumensPerWatt = 683.f;

const char* result_str(cgltf_result r) {
  switch (r) {
    case cgltf_result_success:         return "success";
    case cgltf_result_data_too_short:  return "data too short";
    case cgltf_result_unknown_format:  return "unknown format";
    case cgltf_result_invalid_json:    return "invalid JSON";
    case cgltf_result_invalid_gltf:    return "invalid glTF";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found:  return "file not found";
    case cgltf_result_io_error:        return "I/O error";
    case cgltf_result_out_of_memory:   return "out of memory";
    case cgltf_result_legacy_gltf:     return "legacy glTF 1.0 (only 2.0 is supported)";
    default:                           return "unknown error";
  }
}

// cgltf matrices are column-major float[16]; Matrix4x4(r, c) is row/column.
Matrix4x4 to_matrix(const cgltf_float m[16]) {
  Matrix4x4 M;
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++)
      M(r, c) = m[c * 4 + r];
  return M;
}

Vector3D xform_point(const Matrix4x4& M, const Vector3D& p) {
  return (M * Vector4D(p, 1)).projectTo3D();
}

Vector3D xform_dir(const Matrix4x4& M, const Vector3D& d) {
  return (M * Vector4D(d, 0)).to3D();
}

Vector3D vec3(const cgltf_float* f) { return Vector3D(f[0], f[1], f[2]); }

float max3(const Vector3D& v) { return std::max(v.x, std::max(v.y, v.z)); }

std::string name_or(const char* name, const std::string& fallback) {
  return (name && *name) ? std::string(name) : fallback;
}

int wrap_mode(cgltf_int gl_wrap) {
  switch (gl_wrap) {
    case 33071: return TEX_WRAP_CLAMP;   // GL_CLAMP_TO_EDGE
    case 33648: return TEX_WRAP_MIRROR;  // GL_MIRRORED_REPEAT
    default:    return TEX_WRAP_REPEAT;  // GL_REPEAT (and the glTF default)
  }
}

/*
  Walks the glTF node hierarchy and appends Collada::Nodes to the SceneInfo.
  Meshes and materials are cached by their cgltf pointer so instanced meshes
  share one TriMeshInfo and repeated materials share one BSDF (which also keeps
  the GPU BSDF table small, since GPUScene dedupes BSDFs by pointer).
  Textures are registered while materials are built and decoded afterwards,
  in parallel, and only if a supported slot (base color / emissive) uses them.
*/
struct Loader {
  SceneInfo* scene;
  const cgltf_data* data;
  const cgltf_options* options;
  std::string base_dir;  // directory of the file, for external image URIs

  std::unordered_map<const cgltf_material*, MaterialInfo*> materials;
  std::unordered_map<const cgltf_primitive*, TriMeshInfo*> meshes; // nullptr = skipped
  std::unordered_map<const TriMeshInfo*, BBox> mesh_bounds;        // object space
  std::unordered_set<const TriMeshInfo*> used_meshes;
  MaterialInfo* default_material = nullptr;
  CameraInfo* camera = nullptr;
  BBox world_bounds;

  std::shared_ptr<TextureSet> textures = std::make_shared<TextureSet>();
  std::unordered_map<const cgltf_image*, int> image_ids;
  std::unordered_map<const cgltf_texture*, int> texture_ids;
  std::vector<const cgltf_image*> images_to_decode;  // parallel to textures->images

  // Stats / warnings, reported once at the end instead of per object.
  size_t n_mesh_nodes = 0, n_instanced_nodes = 0;
  size_t n_unique_tris = 0, n_scene_tris = 0, n_vertices = 0;
  size_t n_dropped_tris = 0, n_bad_index_tris = 0;
  size_t n_skipped_non_tri = 0, n_skipped_unreadable = 0;
  size_t n_diffuse = 0, n_mirror = 0, n_glass = 0, n_emissive = 0;
  size_t n_textured = 0, n_textured_emission = 0, n_alpha = 0;
  size_t n_tex_unsupported = 0, n_tex_other_uv = 0, n_tex_decode_failed = 0;
  size_t n_prims_without_uv = 0, n_vertex_colors = 0;
  size_t n_lights = 0, n_spot = 0, n_area = 0;
  size_t n_extra_cameras = 0, n_ortho_cameras = 0;
  size_t n_skinned = 0, n_gpu_instancing = 0;
  double decode_ms = 0;

  Loader(SceneInfo* scene, const cgltf_data* data, const cgltf_options* options,
         const std::string& base_dir)
      : scene(scene), data(data), options(options), base_dir(base_dir) {}

  GPUTexRef texture_ref(const cgltf_texture_view& view);
  BSDF* make_bsdf(const cgltf_material& m);
  MaterialInfo* material(const cgltf_material* m);
  TriMeshInfo* convert_primitive(const cgltf_mesh& mesh, size_t pi);
  TriMeshInfo* mesh_primitive(const cgltf_mesh& mesh, size_t pi);
  void add_camera(const cgltf_node& node, const Matrix4x4& world);
  void add_light(const cgltf_node& node, const Matrix4x4& world);
  void add_area_lights(const cgltf_extras& extras);
  void visit(const cgltf_node* node);
  bool decode_image(size_t i, std::string& error);
  void decode_images();
  void finish();
};

/*
  Registers the texture a material slot uses and returns the slot's reference:
  texture index + KHR_texture_transform as a 2x3 matrix,
    uv' = T * R * S * uv   (offset, rotation, scale; as in the extension spec)
  The image is decoded later (decode_images), once per image.
*/
GPUTexRef Loader::texture_ref(const cgltf_texture_view& view) {
  GPUTexRef ref = gpu_no_texture();
  const cgltf_texture* tex = view.texture;
  if (!tex) return ref;
  if (!tex->image) {  // e.g. KHR_texture_basisu only
    n_tex_unsupported++;
    return ref;
  }

  int texcoord = (view.has_transform && view.transform.has_texcoord)
                     ? view.transform.texcoord : view.texcoord;
  if (texcoord != 0) n_tex_other_uv++;  // only TEXCOORD_0 is loaded

  auto t = texture_ids.find(tex);
  if (t == texture_ids.end()) {
    const cgltf_image* img = tex->image;
    auto im = image_ids.find(img);
    int image_id;
    if (im == image_ids.end()) {
      image_id = (int)textures->images.size();
      image_ids[img] = image_id;
      TextureImage ti;
      ti.name = name_or(img->name, name_or(img->uri, "image_" + std::to_string(image_id)));
      textures->images.push_back(std::move(ti));
      images_to_decode.push_back(img);
    } else {
      image_id = im->second;
    }

    TextureSampler s;
    s.image = image_id;
    s.wrap_s = wrap_mode(tex->sampler ? tex->sampler->wrap_s : 10497);
    s.wrap_t = wrap_mode(tex->sampler ? tex->sampler->wrap_t : 10497);
    t = texture_ids.emplace(tex, (int)textures->textures.size()).first;
    textures->textures.push_back(s);
  }
  ref.tex = t->second;

  if (view.has_transform) {
    const cgltf_texture_transform& x = view.transform;
    float c = cosf(x.rotation), s = sinf(x.rotation);
    ref.m[0] =  c * x.scale[0]; ref.m[1] = s * x.scale[1]; ref.m[2] = x.offset[0];
    ref.m[3] = -s * x.scale[0]; ref.m[4] = c * x.scale[1]; ref.m[5] = x.offset[1];
  }
  return ref;
}

/*
  glTF uses a metallic-roughness PBR model; this path tracer has a handful of
  fixed BSDFs. Rules, first match wins:
    1. emissive (factor * KHR_materials_emissive_strength) > 0, and neither an
       emissive nor a base color texture          -> EmissionBSDF
    2. KHR_materials_transmission factor > 0.5     -> GlassBSDF (KHR_materials_ior)
    3. metallic >= 0.5 and roughness <= 0.1        -> MirrorBSDF(baseColor)
    4. otherwise                                   -> DiffuseBSDF(baseColor)
  Base color textures multiply the reflectance. A textured emitter (rule 1 with
  textures) becomes a DiffuseBSDF that also emits (gpu_emission), with the
  emissive texture multiplying the emission. Metallic/roughness, normal,
  occlusion and clearcoat textures are not used.
*/
BSDF* Loader::make_bsdf(const cgltf_material& m) {
  Vector3D base(1, 1, 1);           // glTF spec defaults
  float metallic = 1, roughness = 1;
  const cgltf_texture_view* base_view = nullptr;

  if (m.has_pbr_metallic_roughness) {
    const cgltf_pbr_metallic_roughness& pbr = m.pbr_metallic_roughness;
    base = vec3(pbr.base_color_factor);
    metallic = pbr.metallic_factor;
    roughness = pbr.roughness_factor;
    base_view = &pbr.base_color_texture;
  } else if (m.has_pbr_specular_glossiness) {
    const cgltf_pbr_specular_glossiness& sg = m.pbr_specular_glossiness;
    base = vec3(sg.diffuse_factor);
    metallic = 0;
    roughness = 1 - sg.glossiness_factor;
    base_view = &sg.diffuse_texture;
  }
  GPUTexRef base_tex = base_view ? texture_ref(*base_view) : gpu_no_texture();
  if (base_tex.tex >= 0) n_textured++;

  float strength = m.has_emissive_strength ? m.emissive_strength.emissive_strength : 1.f;
  Vector3D emission = vec3(m.emissive_factor) * strength;
  bool emits = max3(emission) > 0;
  GPUTexRef emit_tex = emits ? texture_ref(m.emissive_texture) : gpu_no_texture();

  BSDF* bsdf;
  if (emits && emit_tex.tex < 0 && base_tex.tex < 0) {
    n_emissive++;
    bsdf = new EmissionBSDF(emission);
    bsdf->gpu_emission_indirect = true;  // glTF has no separate area light for it
    return bsdf;
  } else if (m.has_transmission && m.transmission.transmission_factor > 0.5f) {
    float ior = m.has_ior ? m.ior.ior : 1.5f;
    n_glass++;
    bsdf = new GlassBSDF(base, Vector3D(1, 1, 1), roughness, ior);
  } else if (metallic >= 0.5f && roughness <= kMirrorRoughness && base_tex.tex < 0) {
    n_mirror++;
    bsdf = new MirrorBSDF(base);
  } else {
    n_diffuse++;
    bsdf = new DiffuseBSDF(base);
  }

  bsdf->base_color_tex = base_tex;
  if (m.alpha_mode != cgltf_alpha_mode_opaque) {
    float factor = m.has_pbr_metallic_roughness ? m.pbr_metallic_roughness.base_color_factor[3] : 1.f;
    if (base_tex.tex >= 0 || factor < 1.f) {  // otherwise alpha is 1 everywhere
      n_alpha++;
      bsdf->gpu_alpha_mode = m.alpha_mode == cgltf_alpha_mode_mask ? GPU_ALPHA_MASK : GPU_ALPHA_BLEND;
      bsdf->gpu_alpha_cutoff = m.alpha_cutoff;
      bsdf->gpu_alpha_factor = factor;
    }
  }
  if (emits) {
    n_textured_emission++;
    bsdf->gpu_emission = emission;
    bsdf->emission_tex = emit_tex;
    bsdf->gpu_emission_indirect = true;
  }
  return bsdf;
}

MaterialInfo* Loader::material(const cgltf_material* m) {
  if (!m) {
    if (!default_material) {
      default_material = new MaterialInfo();
      default_material->type = Instance::MATERIAL;
      default_material->id = default_material->name = "gltf_default";
      default_material->bsdf = new DiffuseBSDF(Vector3D(0.5f, 0.5f, 0.5f));
    }
    return default_material;
  }

  auto it = materials.find(m);
  if (it != materials.end()) return it->second;

  MaterialInfo* mi = new MaterialInfo();
  mi->type = Instance::MATERIAL;
  mi->id = mi->name = name_or(m->name,
      "material_" + std::to_string(cgltf_material_index(data, m)));
  mi->bsdf = make_bsdf(*m);
  materials[m] = mi;
  return mi;
}

TriMeshInfo* Loader::convert_primitive(const cgltf_mesh& mesh, size_t pi) {
  const cgltf_primitive& prim = mesh.primitives[pi];
  if (prim.type != cgltf_primitive_type_triangles) {
    n_skipped_non_tri++;  // points / lines / strips / fans
    return nullptr;
  }

  const cgltf_accessor* pos = nullptr;
  const cgltf_accessor* nrm = nullptr;
  const cgltf_accessor* uv0 = nullptr;
  const cgltf_accessor* col0 = nullptr;
  for (size_t a = 0; a < prim.attributes_count; a++) {
    const cgltf_attribute& attr = prim.attributes[a];
    if (attr.index != 0) continue;
    if (attr.type == cgltf_attribute_type_position) pos = attr.data;
    if (attr.type == cgltf_attribute_type_normal)   nrm = attr.data;
    if (attr.type == cgltf_attribute_type_texcoord) uv0 = attr.data;
    if (attr.type == cgltf_attribute_type_color)    col0 = attr.data;
  }
  if (!pos || pos->count == 0) {
    n_skipped_unreadable++;
    return nullptr;
  }

  const size_t nv = pos->count;
  std::vector<float> buf(nv * 4);
  if (cgltf_accessor_unpack_floats(pos, buf.data(), nv * 3) != nv * 3) {
    n_skipped_unreadable++;
    return nullptr;
  }

  TriMeshInfo* tm = new TriMeshInfo();
  tm->type = Instance::TRIMESH;
  tm->id = tm->name = name_or(mesh.name,
      "mesh_" + std::to_string(cgltf_mesh_index(data, &mesh))) +
      (mesh.primitives_count > 1 ? "[" + std::to_string(pi) + "]" : "");

  tm->positions.resize(nv);
  for (size_t i = 0; i < nv; i++)
    tm->positions[i] = Vector3D(buf[3 * i], buf[3 * i + 1], buf[3 * i + 2]);

  // Normals are optional; TriMesh computes smooth normals when missing.
  if (nrm && nrm->count == nv &&
      cgltf_accessor_unpack_floats(nrm, buf.data(), nv * 3) == nv * 3) {
    tm->normals.resize(nv);
    for (size_t i = 0; i < nv; i++)
      tm->normals[i] = Vector3D(buf[3 * i], buf[3 * i + 1], buf[3 * i + 2]);
  }

  // TEXCOORD_0 (any component type; normalized ints are converted by cgltf).
  if (uv0 && uv0->count == nv &&
      cgltf_accessor_unpack_floats(uv0, buf.data(), nv * 2) == nv * 2) {
    tm->uvs.resize(nv);
    for (size_t i = 0; i < nv; i++) tm->uvs[i] = Vector2D(buf[2 * i], buf[2 * i + 1]);
  } else {
    n_prims_without_uv++;
  }

  // COLOR_0 should multiply the base color; it isn't supported, so only
  // warn when it would actually change something (not all white).
  if (col0 && col0->count == nv) {
    size_t nc = cgltf_num_components(col0->type);
    if (cgltf_accessor_unpack_floats(col0, buf.data(), nv * nc) == nv * nc) {
      for (size_t i = 0; i < nv * nc; i++)
        if (buf[i] < 0.999f) { n_vertex_colors++; break; }
    }
  }

  // Indices (non-indexed primitives use 0..nv-1).
  std::vector<uint32_t> idx;
  if (prim.indices) {
    size_t ni = prim.indices->count;
    idx.resize(ni);
    // Fast path is a memcpy for uint32 indices; sparse accessors need the
    // per-element reader.
    if (cgltf_accessor_unpack_indices(prim.indices, idx.data(), sizeof(uint32_t), ni) != ni)
      for (size_t i = 0; i < ni; i++)
        idx[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
  } else {
    idx.resize(nv);
    for (size_t i = 0; i < nv; i++) idx[i] = (uint32_t)i;
  }

  // Keep only valid, non-degenerate triangles.
  tm->indices.reserve(idx.size() - idx.size() % 3);
  for (size_t t = 0; t + 2 < idx.size(); t += 3) {
    uint32_t a = idx[t], b = idx[t + 1], c = idx[t + 2];
    if (a >= nv || b >= nv || c >= nv) { n_bad_index_tris++; continue; }
    if (a == b || b == c || a == c)    { n_dropped_tris++;   continue; }
    tm->indices.push_back(a);
    tm->indices.push_back(b);
    tm->indices.push_back(c);
  }
  if (tm->indices.empty()) {
    delete tm;
    n_skipped_unreadable++;
    return nullptr;
  }

  tm->material = material(prim.material);

  BBox b;
  for (const Vector3D& p : tm->positions) b.expand(p);
  mesh_bounds[tm] = b;

  n_unique_tris += tm->indices.size() / 3;
  n_vertices += nv;
  return tm;
}

TriMeshInfo* Loader::mesh_primitive(const cgltf_mesh& mesh, size_t pi) {
  const cgltf_primitive* key = &mesh.primitives[pi];
  auto it = meshes.find(key);
  if (it != meshes.end()) return it->second;
  TriMeshInfo* tm = convert_primitive(mesh, pi);
  meshes[key] = tm;
  return tm;
}

void Loader::add_camera(const cgltf_node& node, const Matrix4x4& world) {
  const cgltf_camera* cam = node.camera;
  if (camera) { n_extra_cameras++; return; }
  if (cam->type != cgltf_camera_type_perspective) { n_ortho_cameras++; return; }

  const cgltf_camera_perspective& p = cam->data.perspective;
  CameraInfo* ci = new CameraInfo();
  ci->type = Instance::CAMERA;
  ci->id = ci->name = name_or(cam->name, name_or(node.name, "camera"));

  // CameraInfo FOVs are in degrees. Camera::configure() later reconciles the
  // pair with the window aspect ratio; with hFov == vFov (no aspect given)
  // it keeps vFov on landscape windows, which is the value glTF defines.
  ci->vFov = (float)degrees(p.yfov);
  ci->hFov = (p.has_aspect_ratio && p.aspect_ratio > 0)
                 ? (float)degrees(2 * atan(tan(p.yfov / 2) * p.aspect_ratio))
                 : ci->vFov;
  ci->nClip = p.znear > 0 ? p.znear : 0.01f;
  ci->fClip = (p.has_zfar && p.zfar > ci->nClip) ? p.zfar : 0;  // 0 -> finish()

  // glTF cameras look down local -Z with +Y up.
  ci->world_position = xform_point(world, Vector3D(0, 0, 0));
  ci->world_view_dir = xform_dir(world, Vector3D(0, 0, -1)).unit();
  ci->view_dir = ci->world_view_dir;
  ci->up_dir = xform_dir(world, Vector3D(0, 1, 0)).unit();
  ci->has_world_pose = true;

  Node n;
  n.id = n.name = ci->name;
  n.instance = ci;  // transform stays identity: the pose is already world space
  scene->nodes.push_back(n);
  camera = ci;
}

/*
  KHR_lights_punctual -> LightInfo, baked to world space with an identity node
  transform. (The GLScene light wrappers apply the node transform to positions
  *and* to direction vectors with w = 1, which would add the translation to the
  direction, so they must not see a non-identity transform here.)
  Point and spot intensities are candela and fall off as 1/d^2, windowed by
  `range`; directional intensities are lux (no falloff). Both are converted
  to radiometric units with kLumensPerWatt.
*/
void Loader::add_light(const cgltf_node& node, const Matrix4x4& world) {
  const cgltf_light* l = node.light;
  LightInfo* li = new LightInfo();
  li->type = Instance::LIGHT;
  li->id = li->name = name_or(l->name, name_or(node.name, "light"));
  li->spectrum = vec3(l->color) * (l->intensity / kLumensPerWatt);
  li->inverse_square = true;
  li->range = l->range;

  Vector3D pos = xform_point(world, Vector3D(0, 0, 0));
  Vector3D travel = xform_dir(world, Vector3D(0, 0, -1)).unit(); // lights shine down -Z

  switch (l->type) {
    case cgltf_light_type_directional:
      li->light_type = LightType::DIRECTIONAL;
      // GLScene::DirectionalLight negates this, then SceneObjects::DirectionalLight
      // negates again to get dirToLight, so pass the direction *towards* the light.
      li->direction = -travel;
      break;
    case cgltf_light_type_point:
      li->light_type = LightType::POINT;
      li->position = pos;
      break;
    case cgltf_light_type_spot:
      li->light_type = LightType::SPOT;
      li->position = pos;
      // GLScene::SpotLight computes (direction - position), i.e. it wants a
      // point the light aims at.
      li->direction = pos + travel;
      li->spot_inner = l->spot_inner_cone_angle;
      li->spot_outer = l->spot_outer_cone_angle;
      n_spot++;
      break;
    default:
      delete li;
      return;
  }

  Node n;
  n.id = n.name = li->name;
  n.instance = li;
  scene->nodes.push_back(n);
  n_lights++;
}

/*
  Area lights from the scene's extras, written by
  tools/blender_bake_for_gltf.py (glTF itself has no area lights):
    "pt_area_lights": "<15 floats per light>"
  center(3) edge_x(3) edge_y(3) emit_dir(3) radiance(3), world space, Y-up.
  Radiance is already in the renderer's units (Blender W / m^2 / sr).
*/
void Loader::add_area_lights(const cgltf_extras& extras) {
  if (!extras.data) return;
  const char* key = strstr(extras.data, "\"pt_area_lights\"");
  if (!key) return;
  const char* p = strchr(key + 16, '"');  // opening quote of the value
  if (!p) return;
  p++;
  std::vector<float> v;
  while (*p && *p != '"') {
    char* end;
    float f = strtof(p, &end);
    if (end == p) break;
    v.push_back(f);
    p = end;
  }
  for (size_t i = 0; i + 15 <= v.size(); i += 15) {
    const float* f = &v[i];
    LightInfo* li = new LightInfo();
    li->type = Instance::LIGHT;
    li->id = li->name = "area_light_" + std::to_string(n_area);
    li->light_type = LightType::AREA;
    li->explicit_area = true;
    li->position = Vector3D(f[0], f[1], f[2]);
    li->dim_x = Vector3D(f[3], f[4], f[5]);
    li->dim_y = Vector3D(f[6], f[7], f[8]);
    li->direction = Vector3D(f[9], f[10], f[11]).unit();
    li->spectrum = Vector3D(f[12], f[13], f[14]);

    Node n;
    n.id = n.name = li->name;
    n.instance = li;
    scene->nodes.push_back(n);
    n_lights++;
    n_area++;
  }
}

void Loader::visit(const cgltf_node* node) {
  cgltf_float wm[16];
  cgltf_node_transform_world(node, wm);
  Matrix4x4 world = to_matrix(wm);

  if (node->mesh) {
    if (node->skin) n_skinned++;                       // rendered in bind pose
    if (node->has_mesh_gpu_instancing) n_gpu_instancing++; // extra instances ignored

    for (size_t pi = 0; pi < node->mesh->primitives_count; pi++) {
      TriMeshInfo* tm = mesh_primitive(*node->mesh, pi);
      if (!tm) continue;

      Node n;
      n.id = n.name = name_or(node->name, tm->name);
      n.instance = tm;
      n.transform = world;
      scene->nodes.push_back(n);

      if (!used_meshes.insert(tm).second) n_instanced_nodes++;
      n_mesh_nodes++;
      n_scene_tris += tm->indices.size() / 3;

      const BBox& b = mesh_bounds[tm];
      for (int corner = 0; corner < 8; corner++) {
        Vector3D c((corner & 1) ? b.max.x : b.min.x,
                   (corner & 2) ? b.max.y : b.min.y,
                   (corner & 4) ? b.max.z : b.min.z);
        world_bounds.expand(xform_point(world, c));
      }
    }
  }

  if (node->camera) add_camera(*node, world);
  if (node->light) add_light(*node, world);

  for (size_t i = 0; i < node->children_count; i++)
    visit(node->children[i]);
}

// Decodes textures->images[i] (RGBA8). Embedded (bufferView, the .glb case),
// data: URI and external file images are supported.
bool Loader::decode_image(size_t i, std::string& error) {
  const cgltf_image* img = images_to_decode[i];
  TextureImage& out = textures->images[i];
  int w = 0, h = 0, comp = 0;
  stbi_uc* px = nullptr;

  if (img->buffer_view) {
    const uint8_t* bytes = cgltf_buffer_view_data(img->buffer_view);
    if (bytes)
      px = stbi_load_from_memory(bytes, (int)img->buffer_view->size, &w, &h, &comp, 4);
  } else if (img->uri && strncmp(img->uri, "data:", 5) == 0) {
    const char* comma = strchr(img->uri, ',');
    if (comma && comma - img->uri >= 7 && strncmp(comma - 7, ";base64", 7) == 0) {
      const char* b64 = comma + 1;
      size_t len = strlen(b64);
      size_t pad = (len > 0 && b64[len - 1] == '=') + (len > 1 && b64[len - 2] == '=');
      size_t size = len / 4 * 3 - pad;
      void* bytes = nullptr;
      if (cgltf_load_buffer_base64(options, size, b64, &bytes) == cgltf_result_success) {
        px = stbi_load_from_memory((const stbi_uc*)bytes, (int)size, &w, &h, &comp, 4);
        free(bytes);
      }
    }
  } else if (img->uri) {
    std::string uri = img->uri;
    cgltf_decode_uri(&uri[0]);
    uri.resize(strlen(uri.c_str()));
    px = stbi_load((base_dir + uri).c_str(), &w, &h, &comp, 4);
  }

  if (!px) {
    const char* reason = stbi_failure_reason();
    error = std::string(reason ? reason : "no image data") +
            (img->mime_type ? std::string(" (") + img->mime_type + ")" : "");
    return false;
  }
  out.w = w;
  out.h = h;
  out.rgba.resize((size_t)w * h);
  memcpy(out.rgba.data(), px, (size_t)w * h * 4);
  stbi_image_free(px);
  return true;
}

void Loader::decode_images() {
  size_t n = images_to_decode.size();
  if (n == 0) return;
  auto t0 = std::chrono::steady_clock::now();

  // Images decode independently; big PNGs take ~100-300 ms each, so spread
  // them over the CPU cores.
  std::vector<std::string> errors(n);
  std::vector<char> ok(n, 0);
  std::atomic<size_t> next{0};
  unsigned hw = std::max(1u, std::thread::hardware_concurrency());
  unsigned nthreads = (unsigned)std::min<size_t>(n, hw);
  std::vector<std::thread> pool;
  for (unsigned t = 0; t < nthreads; t++)
    pool.emplace_back([&]() {
      for (size_t i; (i = next++) < n;) ok[i] = decode_image(i, errors[i]) ? 1 : 0;
    });
  for (std::thread& th : pool) th.join();

  for (size_t i = 0; i < n; i++) {
    if (ok[i]) continue;
    n_tex_decode_failed++;
    GLTF_LOG("Warning: could not decode image '" << textures->images[i].name
             << "': " << errors[i] << "; using white");
    TextureImage& img = textures->images[i];
    img.w = img.h = 1;
    img.rgba.assign(1, 0xffffffffu);
  }
  decode_ms = std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - t0).count();
}

void Loader::finish() {
  // Infinite projection (no zfar): pick a far plane that covers the scene.
  if (camera && camera->fClip == 0) {
    float reach = world_bounds.empty()
        ? 0.f
        : (float)(world_bounds.extent.norm() +
                  (camera->world_position - world_bounds.centroid()).norm());
    camera->fClip = std::max(1000.f, 2.f * reach);
  }

  decode_images();
  if (!textures->textures.empty()) scene->textures = textures;

  size_t texels = 0;
  for (const TextureImage& img : textures->images) texels += (size_t)img.w * img.h;

  GLTF_LOG(" |- " << n_mesh_nodes << " mesh nodes (" << n_instanced_nodes
           << " instanced), " << n_scene_tris << " triangles in scene ("
           << n_unique_tris << " unique), " << n_vertices << " vertices");
  GLTF_LOG(" |- materials: " << n_diffuse << " diffuse, " << n_mirror
           << " mirror, " << n_glass << " glass, " << n_emissive << " emissive; "
           << n_textured << " with a base color texture, " << n_textured_emission
           << " emissive on a reflective surface, " << n_alpha << " with alpha");
  GLTF_LOG(" |- textures: " << textures->textures.size() << " textures, "
           << textures->images.size() << " of " << data->images_count
           << " images decoded (" << texels * 4 / (1024 * 1024) << " MB RGBA8) in "
           << (int)decode_ms << " ms");
  GLTF_LOG(" |- " << n_lights << " lights (" << n_spot << " spot, " << n_area
           << " area from extras), camera: "
           << (camera ? camera->name : std::string("none (orbiting scene bbox)")));

  if (n_tex_unsupported)
    GLTF_LOG("Warning: " << n_tex_unsupported << " texture slots use an unsupported "
             "image source (e.g. KTX2/basisu only); ignored");
  if (n_tex_other_uv)
    GLTF_LOG("Warning: " << n_tex_other_uv << " texture slots use a TEXCOORD other "
             "than 0; TEXCOORD_0 is used instead");
  if (n_textured && n_prims_without_uv)
    GLTF_LOG("Warning: " << n_prims_without_uv << " primitives have no TEXCOORD_0; "
             "textures on them sample uv (0,0)");
  if (n_vertex_colors)
    GLTF_LOG("Warning: " << n_vertex_colors << " primitives have non-white vertex "
             "colors (COLOR_0), which are ignored");
  if (n_extra_cameras)
    GLTF_LOG("Warning: " << n_extra_cameras << " extra cameras ignored (using the first)");
  if (n_ortho_cameras)
    GLTF_LOG("Warning: " << n_ortho_cameras << " orthographic cameras ignored");
  if (n_skipped_non_tri)
    GLTF_LOG("Warning: skipped " << n_skipped_non_tri
             << " non-triangle primitives (points/lines/strips/fans)");
  if (n_skipped_unreadable)
    GLTF_LOG("Warning: skipped " << n_skipped_unreadable
             << " primitives with missing or unreadable positions/indices");
  if (n_dropped_tris || n_bad_index_tris)
    GLTF_LOG("Warning: dropped " << n_dropped_tris << " degenerate and "
             << n_bad_index_tris << " out-of-range triangles");
  if (n_skinned)
    GLTF_LOG("Warning: " << n_skinned << " skinned meshes rendered in bind pose");
  if (n_gpu_instancing)
    GLTF_LOG("Warning: EXT_mesh_gpu_instancing on " << n_gpu_instancing
             << " nodes ignored (only the base instance is rendered)");
  if (n_lights == 0)
    GLTF_LOG("Warning: no KHR_lights_punctual lights. The GPU renderer only "
             "lights surfaces from scene lights; emissive meshes are visible "
             "but do not illuminate anything");
}

} // namespace

bool is_gltf_path(const std::string& path) {
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return false;
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return ext == "glb" || ext == "gltf";
}

int load(const char* filename, SceneInfo* sceneInfo) {
  auto t0 = std::chrono::steady_clock::now();

  cgltf_options options = {};
  cgltf_data* data = nullptr;

  GLTF_LOG("Loading glTF file " << filename << "...");
  cgltf_result r = cgltf_parse_file(&options, filename, &data);
  if (r != cgltf_result_success) {
    GLTF_LOG("Error: could not parse " << filename << ": " << result_str(r));
    return -1;
  }

  r = cgltf_load_buffers(&options, data, filename);
  if (r != cgltf_result_success) {
    GLTF_LOG("Error: could not load buffers (is the .bin next to the .gltf?): "
             << result_str(r));
    cgltf_free(data);
    return -1;
  }

  r = cgltf_validate(data);
  if (r != cgltf_result_success) {
    GLTF_LOG("Error: file failed validation: " << result_str(r));
    cgltf_free(data);
    return -1;
  }

  // cgltf parses but does not decode compressed geometry.
  for (size_t i = 0; i < data->buffer_views_count; i++) {
    if (data->buffer_views[i].has_meshopt_compression) {
      GLTF_LOG("Error: EXT_meshopt_compression is not supported. "
               "Re-export without mesh compression.");
      cgltf_free(data);
      return -1;
    }
  }
  for (size_t m = 0; m < data->meshes_count; m++) {
    for (size_t p = 0; p < data->meshes[m].primitives_count; p++) {
      if (data->meshes[m].primitives[p].has_draco_mesh_compression) {
        GLTF_LOG("Error: KHR_draco_mesh_compression is not supported. "
                 "Re-export without Draco compression.");
        cgltf_free(data);
        return -1;
      }
    }
  }

  std::string path(filename);
  size_t slash = path.find_last_of("/\\");
  std::string base_dir = slash == std::string::npos ? "" : path.substr(0, slash + 1);

  Loader loader(sceneInfo, data, &options, base_dir);
  const cgltf_scene* sc = data->scene ? data->scene
                        : (data->scenes_count ? &data->scenes[0] : nullptr);
  if (sc) {
    for (size_t i = 0; i < sc->nodes_count; i++) loader.visit(sc->nodes[i]);
    loader.add_area_lights(sc->extras);
  } else {
    // No scene object: render every root node.
    for (size_t i = 0; i < data->nodes_count; i++)
      if (!data->nodes[i].parent) loader.visit(&data->nodes[i]);
  }
  loader.finish();  // decodes images, so it must run before cgltf_free
  cgltf_free(data);

  if (loader.n_mesh_nodes == 0) {
    GLTF_LOG("Error: no triangle geometry found in " << filename);
    return -1;
  }

  double ms = std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - t0).count();
  GLTF_LOG("Loaded in " << ms << " ms");
  return 0;
}

void release(SceneInfo* sceneInfo) {
  // Instance has no virtual destructor, so delete through the concrete type.
  // Several nodes can share one TriMeshInfo, and meshes share MaterialInfos.
  std::unordered_set<Instance*> instances;
  std::unordered_set<MaterialInfo*> mats;
  for (Node& n : sceneInfo->nodes) {
    if (!n.instance) continue;
    if (n.instance->type == Instance::TRIMESH) {
      TriMeshInfo* tm = static_cast<TriMeshInfo*>(n.instance);
      if (tm->material) mats.insert(tm->material);
    }
    instances.insert(n.instance);
    n.instance = nullptr;
  }
  for (Instance* inst : instances) {
    switch (inst->type) {
      case Instance::TRIMESH: delete static_cast<TriMeshInfo*>(inst); break;
      case Instance::CAMERA:  delete static_cast<CameraInfo*>(inst);  break;
      case Instance::LIGHT:   delete static_cast<LightInfo*>(inst);   break;
      default: break;  // the glTF loader creates no other node types
    }
  }
  for (MaterialInfo* m : mats) delete m;  // BSDFs intentionally kept
  sceneInfo->nodes.clear();
  // sceneInfo->textures is a shared_ptr; the GL scene keeps its own reference.
}

} // namespace GLTF
} // namespace CGL
