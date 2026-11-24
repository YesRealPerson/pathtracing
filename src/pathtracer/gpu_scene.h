#ifndef CGL_GPU_SCENE_H
#define CGL_GPU_SCENE_H

#include "CGL/CGL.h"
#include "CGL/vector3D.h"
#include "CGL/matrix3x3.h"

#include "scene/texture.h"

#include <cstdint>
#include <vector>

namespace CGL {

class BSDF;
namespace SceneObjects { class Primitive; struct Scene; }

// Indexed primitive, 16 bytes (was 80 bytes with the vertices copied in).
// Triangle: v0, v1, v2 index GPUSceneView::positions / normals / uvs.
// Sphere:   v0 indexes GPUSceneView::spheres, v1 == v2 == GPU_PRIM_SPHERE_TAG.
#define GPU_PRIM_SPHERE_TAG (-1)
typedef struct GPUPrimitive {
  int v0, v1, v2;
  int bsdf_id; // index for BSDF list
} GPUPrimitive_t;

typedef struct GPUSphere {
  Vector3D o;
  float r;
} GPUSphere_t;

typedef struct GPUFloat2 {
  float u, v;
} GPUFloat2_t;

#define GPU_BVH_STACK_SIZE 64

// The root is always node 0.
// Flat BVH node
typedef struct GPUBVHNode {
  Vector3D bb_min, bb_max;
  int left, right; // Index of left and right child
  int start, count; // Start index and number of primitives inside node
  // count is only nonzero for leaves, leaves naturally will not have children
} GPUBVHNode_t;

// Camera information to generate new rays
typedef struct GPUCamera {
  Vector3D pos;
  Matrix3x3 c2w;
  float tan_half_hfov, tan_half_vfov;
  float nclip, fclip;
} GPUCamera_t;

// Emission & Diffuse only for now, and maybe forever
typedef enum BSDFType{
  EMISSIVE,
  DIFFUSE
} BSDFType_t;

// One texture in the scene: a w x h block of packed RGBA8 texels
// (sRGB-encoded, 0xAABBGGRR) inside GPUSceneView::texels.
typedef struct GPUTexture {
  long long offset;   // index of the first texel
  int w, h;
  int wrap_s, wrap_t; // TextureWrap
} GPUTexture_t;

// A material slot's texture reference, including KHR_texture_transform:
//   uv' = (m[0] u + m[1] v + m[2],  m[3] u + m[4] v + m[5])
// tex < 0 means "no texture".
typedef struct GPUTexRef {
  int tex;
  float m[6];
} GPUTexRef_t;

inline GPUTexRef gpu_no_texture() {
  GPUTexRef r;
  r.tex = -1;
  r.m[0] = 1; r.m[1] = 0; r.m[2] = 0;
  r.m[3] = 0; r.m[4] = 1; r.m[5] = 0;
  return r;
}

// glTF alphaMode. MASK: a hit is skipped where alpha < cutoff. BLEND: skipped
// with probability 1 - alpha (stochastic, like Cycles' dithered transparency).
enum GPUAlphaMode {
  GPU_ALPHA_OPAQUE = 0,
  GPU_ALPHA_MASK   = 1,
  GPU_ALPHA_BLEND  = 2,
};

// Only takes CosineWeightedHemisphereSampler3D
// reflectance is multiplied by base_tex, radiance by emit_tex (when present).
// A DIFFUSE BSDF may have nonzero radiance (textured emitters such as screens).
typedef struct GPUBSDF {
  BSDFType_t type;
  Vector3D reflectance, radiance;
  GPUTexRef base_tex, emit_tex;
  // 1: emission also counts when reached by an indirect (bounce) ray, so the
  // surface lights the scene. Set for glTF emitters, which have no separate
  // light. COLLADA emitters keep 0: the CS184 scenes put an area light on top
  // of their emissive quads, and counting both would double the light.
  int emit_indirect;
  int alpha_mode;      // GPUAlphaMode; alpha comes from base_tex's alpha channel
  float alpha_cutoff;  // MASK only
  float alpha_factor;  // baseColorFactor.a
} GPUBSDF_t;

typedef enum GPULightType {
  GPU_LIGHT_POINT       = 0,
  GPU_LIGHT_DIRECTIONAL = 1,
  GPU_LIGHT_AREA        = 2,
  GPU_LIGHT_HEMISPHERE  = 3,
  GPU_LIGHT_UNKNOWN     = 4,
  GPU_LIGHT_SPOT        = 5,
} GPULightType_t;

typedef struct GPULight {
  GPULightType_t type;
  Vector3D radiance;
  Vector3D position;
  Vector3D direction;   // spot: the axis the light shines along
  Vector3D dim_x, dim_y;
  float area;
  // Point / spot only. inv_square = 0 keeps the old CS184 behaviour (no
  // distance falloff); glTF lights set it. range > 0 fades the light to zero
  // at that distance (glTF's recommended window).
  int inv_square;
  float range;
  float cos_inner, cos_outer; // spot cone
} GPULight_t;

// Pointers/information needed for GPU kernels
typedef struct GPUSceneView {
  const GPUPrimitive* primitives;
  const GPUBVHNode*   nodes;
  const GPUBSDF*      bsdfs; // indexed by GPUPrimitive::bsdf_id
  const GPULight*     lights;
  const Vector3D*     positions; // per vertex
  const Vector3D*     normals;   // per vertex
  const GPUFloat2*    uvs;       // per vertex, or null if no mesh has UVs
  const GPUSphere*    spheres;
  const GPUTexture*   textures;
  const uint32_t*     texels;
  int num_primitives;
  int num_nodes;
  int num_bsdfs;
  int num_lights;

  // Render settings
  int ns_aa;          // camera rays per pixel
  int max_ray_depth;  // maximum ray depth
  int ns_area_light;  // samples per area light
  int samples_per_batch; // samples between adaptive sampling convergence checks
  float maxTolerance;    // adaptive sampling convergence tolerance

  // Debug: 1 = output the textured base color at the first hit instead of
  // lighting (set with the environment variable PT_DEBUG_ALBEDO=1).
  int debug_albedo;
} GPUSceneView_t;

struct GPUScene {
  GPUPrimitive* primitives = nullptr;
  size_t num_primitives = 0;

  GPUBVHNode* nodes = nullptr;
  size_t num_nodes = 0;

  // Shared vertex arrays the indexed primitives point into
  Vector3D* positions = nullptr;
  Vector3D* normals = nullptr;
  GPUFloat2* uvs = nullptr;
  size_t num_vertices = 0;

  GPUSphere* spheres = nullptr;
  size_t num_spheres = 0;

  size_t ns_aa = 1;          // camera rays per pixel
  size_t max_ray_depth = 1;  // maximum ray depth
  size_t ns_area_light = 1;  // samples per area light
  size_t samples_per_batch = 32; // samples between adaptive sampling convergence checks
  float maxTolerance = 0.05f;    // adaptive sampling convergence tolerance
  int debug_albedo = 0;          // see GPUSceneView::debug_albedo

  GPUBSDF* bsdfs = nullptr; // indexed by GPUPrimitive::bsdf_id
  size_t num_bsdfs = 0;

  GPULight* lights = nullptr;
  size_t num_lights = 0;

  // Textures stay on the device across upload() calls (re-entering render
  // mode rebuilds the geometry, but the textures never change).
  GPUTexture* textures = nullptr;
  size_t num_textures = 0;
  uint32_t* texels = nullptr;
  size_t num_texels = 0;
  const TextureSet* uploaded_textures = nullptr; // identity of what's on device

  GPUScene() = default;
  ~GPUScene() { free(); free_textures(); }
  // Unironically useful information
  // Copy constructor
  // GPUScene stores a lot of pointers into GPU memory
  // When we call free or destructor we free GPU memory
  // Then copy pointers become stale

  // Removing the copy constructor makes copies into compile errors
  // Such as pass by value pushing to a vector

  // If we were to pass the GPU scene into the kernel it would copy it
  // Then upon launching the kernel we copy the value passed and send it to GPU
  // So as soon as this is done we call ~GPUScene on the copy
  // which in current implmentation frees GPU memory
  // Hence why we have the view struct
  GPUScene(const GPUScene&) = delete;
  GPUScene& operator=(const GPUScene&) = delete;

  // Builds the GPU scene straight from the static scene: mesh vertex arrays
  // are appended to the shared vertex buffers and each triangle becomes a
  // 16-byte indexed primitive (no per-triangle Triangle objects). Then builds
  // the BVH, reorders primitives so each leaf is a contiguous range, and
  // uploads everything (plus lights and textures).
  void upload(const SceneObjects::Scene* scene, size_t max_leaf_size = 4);

  // Uploads a texture set unless it is already on the device. Releases the
  // host texel copy afterwards (the device copy is kept for the lifetime of
  // this GPUScene), so pass the same TextureSet object on later uploads.
  void upload_textures(TextureSet* set);

  GPUSceneView view() const {
    GPUSceneView v;
    v.primitives = primitives;
    v.nodes = nodes;
    v.bsdfs = bsdfs;
    v.lights = lights;
    v.positions = positions;
    v.normals = normals;
    v.uvs = uvs;
    v.spheres = spheres;
    v.textures = textures;
    v.texels = texels;
    v.num_primitives = (int)num_primitives;
    v.num_nodes = (int)num_nodes;
    v.num_bsdfs = (int)num_bsdfs;
    v.num_lights = (int)num_lights;
    v.ns_aa = (int)ns_aa;
    v.max_ray_depth = (int)max_ray_depth;
    v.ns_area_light = (int)ns_area_light;
    v.samples_per_batch = (int)samples_per_batch;
    v.maxTolerance = maxTolerance;
    v.debug_albedo = debug_albedo;
    return v;
  }

  void free();          // geometry, BVH, BSDFs, lights
  void free_textures(); // textures
};

} // namespace CGL

#endif // CGL_GPU_SCENE_H
