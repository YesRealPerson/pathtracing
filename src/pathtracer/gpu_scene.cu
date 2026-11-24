#include "gpu_scene.h"

#include "scene/scene.h"
#include "scene/object.h"
#include "scene/primitive.h"
#include "scene/sphere.h"
#include "scene/bbox.h"
#include "pathtracer/bsdf.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <unordered_map>

// From cuda docs
#define CUDA_CALL(x) do { if((x) != cudaSuccess) { \
    printf("Error at %s:%d\n",__FILE__,__LINE__); \
    exit(1);}} while(0)

namespace CGL
{

  using namespace SceneObjects;

  namespace
  {
    // Per-primitive bounds used only while building the BVH.
    struct PrimBounds
    {
      Vector3D lo, hi, c; // bbox min / max and centroid
    };

    // Midpoint-split BVH over PrimBounds. Partitions `order` in place, so the
    // build needs no per-node allocations; the leaves end up as contiguous
    // ranges of `order`.
    struct BVHBuilder
    {
      const std::vector<PrimBounds> &bounds;
      std::vector<int> &order;
      std::vector<GPUBVHNode_t> &nodes;
      size_t max_leaf_size;
      int max_depth = 0;

      int build(size_t start, size_t end, int depth)
      {
        max_depth = std::max(max_depth, depth);

        BBox bbox, cbox;
        for (size_t i = start; i < end; i++)
        {
          const PrimBounds &b = bounds[order[i]];
          bbox.expand(b.lo);
          bbox.expand(b.hi);
          cbox.expand(b.c);
        }
        size_t primcount = end - start;

        int idx = (int)nodes.size();
        nodes.push_back({});
        nodes[idx].bb_min = bbox.min;
        nodes[idx].bb_max = bbox.max;

        if (primcount <= max_leaf_size)
        {
          nodes[idx].left = -1;
          nodes[idx].right = -1;
          nodes[idx].start = (int)start;
          nodes[idx].count = (int)primcount;
          return idx;
        }

        float e[3] = {cbox.extent.x, cbox.extent.y, cbox.extent.z};
        int axis = (int)(std::max_element(e, e + 3) - e);
        float split = (cbox.min[axis] + cbox.max[axis]) / 2;

        auto first = order.begin() + start, last = order.begin() + end;
        size_t mid = std::partition(first, last, [&](int p) {
                       return bounds[p].c[axis] < split;
                     }) - order.begin();

        // Degenerate split (all centroids on one side): split at the median.
        if (mid == start || mid == end)
        {
          mid = start + primcount / 2;
          std::nth_element(first, order.begin() + mid, last, [&](int a, int b) {
            return bounds[a].c[axis] < bounds[b].c[axis];
          });
        }

        int l = build(start, mid, depth + 1);
        int r = build(mid, end, depth + 1);
        nodes[idx].left = l;
        nodes[idx].right = r;
        nodes[idx].start = 0;
        nodes[idx].count = 0;
        return idx;
      }
    };

    template <typename T>
    void upload_array(T **dst, const std::vector<T> &src)
    {
      *dst = nullptr;
      if (src.empty())
        return;
      CUDA_CALL(cudaMalloc(dst, sizeof(T) * src.size()));
      CUDA_CALL(cudaMemcpy(*dst, src.data(), sizeof(T) * src.size(), cudaMemcpyHostToDevice));
    }

    double mb(size_t bytes) { return bytes / (1024.0 * 1024.0); }
  }

  void GPUScene::upload(const Scene *scene, size_t max_leaf_size)
  {
    // frees geometry that was on the GPU (textures are kept)
    this->free();
    auto t0 = std::chrono::steady_clock::now();

    // ---- Gather geometry -------------------------------------------------
    size_t nv = 0, nt = 0;
    for (SceneObject *obj : scene->objects)
      if (const Mesh *m = dynamic_cast<const Mesh *>(obj))
      {
        nv += m->data().positions.size();
        nt += m->data().num_triangles();
      }

    std::vector<Vector3D> P, N;
    std::vector<GPUFloat2> UV;
    std::vector<GPUPrimitive_t> prims;
    std::vector<PrimBounds> bounds;
    std::vector<GPUSphere_t> spheres;
    P.reserve(nv);
    N.reserve(nv);
    UV.reserve(nv);
    prims.reserve(nt);
    bounds.reserve(nt);
    bool any_uv = false;

    // BSDFs are deduplicated by pointer; materials shared by many meshes
    // become one GPUBSDF.
    std::vector<GPUBSDF_t> GPUbsdfs;
    std::unordered_map<BSDF *, int> bsdf_ids;
    auto bsdf_id = [&](BSDF *b) -> int {
      auto it = bsdf_ids.find(b);
      if (it != bsdf_ids.end())
        return it->second;
      GPUBSDF_t gb{};
      b->createGPU(&gb);
      gb.base_tex = b->base_color_tex;
      gb.emit_tex = b->emission_tex;
      if (gb.type != EMISSIVE)
        gb.radiance += b->gpu_emission;
      gb.emit_indirect = b->gpu_emission_indirect ? 1 : 0;
      gb.alpha_mode = b->gpu_alpha_mode;
      gb.alpha_cutoff = b->gpu_alpha_cutoff;
      gb.alpha_factor = b->gpu_alpha_factor;
      int id = (int)GPUbsdfs.size();
      GPUbsdfs.push_back(gb);
      bsdf_ids.emplace(b, id);
      return id;
    };

    for (SceneObject *obj : scene->objects)
    {
      if (const Mesh *m = dynamic_cast<const Mesh *>(obj))
      {
        // Meshes: append the vertex arrays once and emit 16-byte indexed
        // triangles. No Triangle objects are allocated.
        const MeshData &d = m->data();
        const int base = (int)P.size();
        const int bid = bsdf_id(m->get_bsdf());
        P.insert(P.end(), d.positions.begin(), d.positions.end());
        N.insert(N.end(), d.normals.begin(), d.normals.end());
        if (d.uvs.size() == d.positions.size())
        {
          any_uv = true;
          for (const Vector2D &uv : d.uvs)
            UV.push_back({uv.x, uv.y});
        }
        else
        {
          UV.resize(UV.size() + d.positions.size(), GPUFloat2{0.f, 0.f});
        }
        for (size_t t = 0; t < d.num_triangles(); t++)
        {
          uint32_t a = d.indices[3 * t], b = d.indices[3 * t + 1], c = d.indices[3 * t + 2];
          prims.push_back({base + (int)a, base + (int)b, base + (int)c, bid});
          const Vector3D &pa = d.positions[a], &pb = d.positions[b], &pc = d.positions[c];
          PrimBounds pbnd;
          pbnd.lo = Vector3D(std::min(pa.x, std::min(pb.x, pc.x)),
                             std::min(pa.y, std::min(pb.y, pc.y)),
                             std::min(pa.z, std::min(pb.z, pc.z)));
          pbnd.hi = Vector3D(std::max(pa.x, std::max(pb.x, pc.x)),
                             std::max(pa.y, std::max(pb.y, pc.y)),
                             std::max(pa.z, std::max(pb.z, pc.z)));
          pbnd.c = (pbnd.lo + pbnd.hi) * 0.5f;
          bounds.push_back(pbnd);
        }
      }
      else
      {
        // Everything else (spheres) still goes through Primitive.
        for (Primitive *p : obj->get_primitives())
        {
          if (const Sphere *s = dynamic_cast<const Sphere *>(p))
          {
            GPUSphere_t gs;
            s->createGPU(&gs);
            spheres.push_back(gs);
            prims.push_back({(int)spheres.size() - 1, GPU_PRIM_SPHERE_TAG,
                             GPU_PRIM_SPHERE_TAG, bsdf_id(p->get_bsdf())});
            BBox bb = p->get_bbox();
            bounds.push_back({bb.min, bb.max, bb.centroid()});
          }
          else
          {
            fprintf(stderr, "[GPUScene] Unsupported primitive type skipped.\n");
          }
          delete p;
        }
      }
    }
    if (!any_uv)
      std::vector<GPUFloat2>().swap(UV); // no textures possible: skip the array

    // ---- BVH -----------------------------------------------------------------
    if (!prims.empty())
    {
      std::vector<int> order(prims.size());
      for (size_t i = 0; i < order.size(); i++)
        order[i] = (int)i;

      std::vector<GPUBVHNode_t> nodes;
      nodes.reserve(2 * prims.size() / std::max<size_t>(1, max_leaf_size) + 1);
      BVHBuilder builder{bounds, order, nodes, max_leaf_size};
      builder.build(0, order.size(), 0);
      std::vector<PrimBounds>().swap(bounds);

      if (builder.max_depth + 1 > GPU_BVH_STACK_SIZE)
      {
        fprintf(stderr, "[GPUScene] BVH depth %d exceeds traversal stack size %d\n",
                builder.max_depth, GPU_BVH_STACK_SIZE);
        exit(1);
      }

      // Primitives in BVH order, so every leaf is a contiguous range
      std::vector<GPUPrimitive_t> ordered(prims.size());
      for (size_t i = 0; i < order.size(); i++)
        ordered[i] = prims[order[i]];
      std::vector<GPUPrimitive_t>().swap(prims);

      upload_array(&this->nodes, nodes);
      this->num_nodes = nodes.size();
      upload_array(&this->primitives, ordered);
      this->num_primitives = ordered.size();

      fprintf(stdout, "[GPUScene] BVH: %zu nodes, max depth %d\n", nodes.size(), builder.max_depth);
    }

    upload_array(&this->positions, P);
    upload_array(&this->normals, N);
    upload_array(&this->uvs, UV);
    this->num_vertices = P.size();
    upload_array(&this->spheres, spheres);
    this->num_spheres = spheres.size();
    upload_array(&this->bsdfs, GPUbsdfs);
    this->num_bsdfs = GPUbsdfs.size();

    // ---- Lights --------------------------------------------------------------
    std::vector<GPULight_t> GPUlights;
    GPUlights.reserve(scene->lights.size());
    for (SceneLight *l : scene->lights)
    {
      GPULight_t gl;
      l->createGPU(&gl);
      if (gl.type == GPU_LIGHT_UNKNOWN)
        fprintf(stderr, "[GPUScene] Unimplemented GPU light!.\n");
      GPUlights.push_back(gl);
    }
    upload_array(&this->lights, GPUlights);
    this->num_lights = GPUlights.size();

    // ---- Textures ------------------------------------------------------------
    upload_textures(scene->textures.get());

    double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();
    size_t b_prims = num_primitives * sizeof(GPUPrimitive_t);
    size_t b_nodes = num_nodes * sizeof(GPUBVHNode_t);
    size_t b_verts = num_vertices * 2 * sizeof(Vector3D) + UV.size() * sizeof(GPUFloat2);
    size_t b_tex = num_texels * sizeof(uint32_t) + num_textures * sizeof(GPUTexture_t);
    fprintf(stdout,
            "[GPUScene] Uploaded %zu primitives (%.1f MB), %zu vertices (%.1f MB), "
            "BVH (%.1f MB), %zu textures (%.1f MB) in %.0f ms; total %.1f MB\n",
            num_primitives, mb(b_prims), num_vertices, mb(b_verts), mb(b_nodes),
            num_textures, mb(b_tex), ms, mb(b_prims + b_verts + b_nodes + b_tex));
  }

  void GPUScene::upload_textures(TextureSet *set)
  {
    if (set == uploaded_textures)
      return; // already resident (or both null)
    free_textures();
    if (!set || set->textures.empty())
      return;

    // One flat texel array; each image gets an offset. Several textures
    // (image + wrap mode) may point at the same image.
    std::vector<long long> image_offset(set->images.size(), 0);
    size_t total = 0;
    for (size_t i = 0; i < set->images.size(); i++)
    {
      image_offset[i] = (long long)total;
      total += set->images[i].rgba.size();
    }

    std::vector<GPUTexture_t> textures;
    for (const TextureSampler &t : set->textures)
    {
      const TextureImage &img = set->images[t.image];
      textures.push_back({image_offset[t.image], img.w, img.h, t.wrap_s, t.wrap_t});
    }

    CUDA_CALL(cudaMalloc(&this->texels, sizeof(uint32_t) * std::max<size_t>(total, 1)));
    for (size_t i = 0; i < set->images.size(); i++)
    {
      const std::vector<uint32_t> &px = set->images[i].rgba;
      if (!px.empty())
        CUDA_CALL(cudaMemcpy(this->texels + image_offset[i], px.data(),
                             sizeof(uint32_t) * px.size(), cudaMemcpyHostToDevice));
    }
    upload_array(&this->textures, textures);
    this->num_textures = textures.size();
    this->num_texels = total;

    // The device copy lives as long as this GPUScene; drop the host copy.
    for (TextureImage &img : set->images)
      std::vector<uint32_t>().swap(img.rgba);
    uploaded_textures = set;
  }

  void GPUScene::free()
  {
    CUDA_CALL(cudaFree(this->primitives));
    CUDA_CALL(cudaFree(this->nodes));
    CUDA_CALL(cudaFree(this->positions));
    CUDA_CALL(cudaFree(this->normals));
    CUDA_CALL(cudaFree(this->uvs));
    CUDA_CALL(cudaFree(this->spheres));
    CUDA_CALL(cudaFree(this->bsdfs));
    CUDA_CALL(cudaFree(this->lights));
    this->primitives = nullptr;
    this->num_primitives = 0;
    this->nodes = nullptr;
    this->num_nodes = 0;
    this->positions = nullptr;
    this->normals = nullptr;
    this->uvs = nullptr;
    this->num_vertices = 0;
    this->spheres = nullptr;
    this->num_spheres = 0;
    this->bsdfs = nullptr;
    this->num_bsdfs = 0;
    this->lights = nullptr;
    this->num_lights = 0;
  }

  void GPUScene::free_textures()
  {
    CUDA_CALL(cudaFree(this->textures));
    CUDA_CALL(cudaFree(this->texels));
    this->textures = nullptr;
    this->num_textures = 0;
    this->texels = nullptr;
    this->num_texels = 0;
    this->uploaded_textures = nullptr;
  }

} // namespace CGL
