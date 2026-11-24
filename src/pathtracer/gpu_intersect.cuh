#ifndef CGL_GPU_INTERSECT_CUH
#define CGL_GPU_INTERSECT_CUH

#include "gpu_scene.h"

namespace CGL
{

  // Device-friendly ray. (CGL::Ray's constructors aren't __device__, and the
  // ray needs nothing more than this.)
  struct GPURay
  {
    Vector3D o, d;
    Vector3D inv_d; // 1/d, component-wise -- precompute for the slab test
    float min_t, max_t;
  };

  // Replaces CGL::Intersection, which holds Primitive*/BSDF* host pointers.
  // Traversal only records t, the barycentrics and prim_id; the rest is
  // filled once for the closest hit by finish_hit().
  struct GPUIntersect
  {
    float t;
    float b1, b2; // barycentrics of the hit (triangles)
    int prim_id;  // index into scene.primitives (-1 = miss)

    // Filled by finish_hit():
    Vector3D n;   // shading normal, flipped to the side the ray came from
    Vector3D ng;  // geometric normal, same side as n
    float u, v;   // interpolated texture coordinates (0 if the mesh has none)
    int bsdf_id;  // copied from the primitive
  };

  __device__ inline GPURay make_gpu_ray(Vector3D o, Vector3D d,
                                        float min_t = 0.f, float max_t = INFINITY)
  {
    GPURay r;
    r.o = o;
    r.d = d;
    r.inv_d = Vector3D(1.f / d.x, 1.f / d.y, 1.f / d.z);
    r.min_t = min_t;
    r.max_t = max_t;
    return r;
  }

  // ---- Alpha (cut-out) testing ----------------------------------------------

  __device__ inline int wrap_texel(int i, int n, int mode)
  {
    if (mode == TEX_WRAP_CLAMP)
      return min(max(i, 0), n - 1);
    if (mode == TEX_WRAP_MIRROR)
    {
      int period = 2 * n;
      int k = i % period;
      if (k < 0)
        k += period;
      return k < n ? k : period - 1 - k;
    }
    int k = i % n; // repeat
    return k < 0 ? k + n : k;
  }

  // Bilinear lookup of a texture's alpha channel (stored linear, 0..255).
  __device__ inline float sample_alpha(const GPUSceneView &scene, const GPUTexRef &ref,
                                       float u, float v)
  {
    const GPUTexture &tex = scene.textures[ref.tex];
    float tu = ref.m[0] * u + ref.m[1] * v + ref.m[2];
    float tv = ref.m[3] * u + ref.m[4] * v + ref.m[5];
    if (!isfinite(tu) || !isfinite(tv))
      return 1.f;
    float x = tu * tex.w - 0.5f, y = tv * tex.h - 0.5f;
    float fx = floorf(x), fy = floorf(y);
    float ax = x - fx, ay = y - fy;
    int ix = (int)fmodf(fx, (float)(tex.w * 1024)), iy = (int)fmodf(fy, (float)(tex.h * 1024));
    int x0 = wrap_texel(ix, tex.w, tex.wrap_s), x1 = wrap_texel(ix + 1, tex.w, tex.wrap_s);
    int y0 = wrap_texel(iy, tex.h, tex.wrap_t), y1 = wrap_texel(iy + 1, tex.h, tex.wrap_t);
    const uint32_t *px = scene.texels + tex.offset;
    float a00 = (float)(px[(size_t)y0 * tex.w + x0] >> 24);
    float a10 = (float)(px[(size_t)y0 * tex.w + x1] >> 24);
    float a01 = (float)(px[(size_t)y1 * tex.w + x0] >> 24);
    float a11 = (float)(px[(size_t)y1 * tex.w + x1] >> 24);
    return ((a00 * (1.f - ax) + a10 * ax) * (1.f - ay) + (a01 * (1.f - ax) + a11 * ax) * ay) *
           (1.f / 255.f);
  }

  __device__ inline uint32_t hash_u32(uint32_t x)
  {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
  }

  // True if this candidate hit falls on a transparent texel and the ray should
  // pass through. BLEND uses a hash of the ray and triangle as its random
  // number: every query with the same ray (closest hit or shadow) agrees, and
  // jittered camera rays / new bounce directions average to the coverage.
  __device__ inline bool alpha_skips_hit(const GPUSceneView &scene, const GPUPrimitive &p,
                                         int prim_id, const GPURay &r, float b1, float b2)
  {
    const GPUBSDF &b = scene.bsdfs[p.bsdf_id];
    if (b.alpha_mode == GPU_ALPHA_OPAQUE)
      return false;
    float alpha = b.alpha_factor;
    if (b.base_tex.tex >= 0 && scene.uvs)
    {
      const float a = 1.f - b1 - b2;
      const GPUFloat2 t0 = scene.uvs[p.v0], t1 = scene.uvs[p.v1], t2 = scene.uvs[p.v2];
      alpha *= sample_alpha(scene, b.base_tex, a * t0.u + b1 * t1.u + b2 * t2.u,
                            a * t0.v + b1 * t1.v + b2 * t2.v);
    }
    if (b.alpha_mode == GPU_ALPHA_MASK)
      return alpha < b.alpha_cutoff;
    if (alpha >= 1.f)
      return false;
    uint32_t h = hash_u32((uint32_t)prim_id * 0x9e3779b9u);
    h = hash_u32(h ^ __float_as_uint(r.o.x));
    h = hash_u32(h ^ __float_as_uint(r.o.y));
    h = hash_u32(h ^ __float_as_uint(r.o.z));
    h = hash_u32(h ^ __float_as_uint(r.d.x));
    h = hash_u32(h ^ __float_as_uint(r.d.y) ^ (__float_as_uint(r.d.z) << 7));
    return (h >> 8) * (1.f / 16777216.f) >= alpha;
  }

  __device__ inline bool intersect_triangle(const GPUSceneView &scene,
                                            const GPUPrimitive &p, int prim_id,
                                            GPURay &r, GPUIntersect &hit)
  {
    constexpr float epsilon = 1e-6f; // floating point bs
    const Vector3D pa = scene.positions[p.v0];
    Vector3D E1 = scene.positions[p.v1] - pa;
    Vector3D E2 = scene.positions[p.v2] - pa;
    Vector3D S = r.o - pa; // b in Ax=b
    Vector3D S1 = cross(r.d, E2);
    Vector3D S2 = cross(S, E1);

    float parallel = dot(S1, E1);
    if (fabsf(parallel) < epsilon)
      return false;
    float det = 1.0f / parallel;
    float t = dot(S2, E2) * det;
    if (t < r.min_t || t > r.max_t)
      return false;
    float b1 = dot(S1, S) * det;
    if (b1 < 0)
      return false;
    float b2 = dot(S2, r.d) * det;
    if (b2 < 0 || b2 + b1 > 1)
      return false;
    if (alpha_skips_hit(scene, p, prim_id, r, b1, b2))
      return false;
    hit.t = t;
    hit.b1 = b1;
    hit.b2 = b2;
    hit.prim_id = prim_id;
    r.max_t = hit.t;

    return true;
  }

  __device__ inline bool intersect_sphere(const GPUSceneView &scene,
                                          const GPUPrimitive &p, int prim_id,
                                          GPURay &r, GPUIntersect &hit)
  {
    // TODO: scene.spheres[p.v0] holds the origin and radius.
    // finish_hit() also needs a sphere branch for n / ng when this is written.
    return false;
  }

  __device__ inline bool intersect_primitive(const GPUSceneView &scene, int prim_id,
                                             GPURay &r, GPUIntersect &hit)
  {
    const GPUPrimitive &p = scene.primitives[prim_id];
    if (p.v1 == GPU_PRIM_SPHERE_TAG)
      return intersect_sphere(scene, p, prim_id, r, hit);
    return intersect_triangle(scene, p, prim_id, r, hit);
  }

  // Shading data for the closest hit: interpolated normal and UVs. Both
  // normals are flipped to face the incoming ray, so double-sided surfaces
  // (all glTF exports from Blender) shade correctly from either side.
  __device__ inline void finish_hit(const GPUSceneView &scene, const GPURay &r,
                                    GPUIntersect &hit)
  {
    const GPUPrimitive &p = scene.primitives[hit.prim_id];
    hit.bsdf_id = p.bsdf_id;
    hit.u = hit.v = 0.f;

    const Vector3D pa = scene.positions[p.v0];
    Vector3D ng = cross(scene.positions[p.v1] - pa, scene.positions[p.v2] - pa);
    float ng_len = ng.norm();
    ng = ng_len > 0.f ? ng / ng_len : Vector3D(0.f, 1.f, 0.f);
    if (dot(ng, r.d) > 0.f)
      ng = -ng;

    const float a = 1.f - hit.b1 - hit.b2;
    Vector3D n = a * scene.normals[p.v0] + hit.b1 * scene.normals[p.v1] +
                 hit.b2 * scene.normals[p.v2];
    float n_len = n.norm();
    n = n_len > 0.f ? n / n_len : ng;
    if (dot(n, ng) < 0.f)
      n = -n;

    hit.n = n;
    hit.ng = ng;

    if (scene.uvs)
    {
      const GPUFloat2 t0 = scene.uvs[p.v0], t1 = scene.uvs[p.v1], t2 = scene.uvs[p.v2];
      hit.u = a * t0.u + hit.b1 * t1.u + hit.b2 * t2.u;
      hit.v = a * t0.v + hit.b1 * t1.v + hit.b2 * t2.v;
    }
  }

  __device__ inline bool intersect_bbox(const Vector3D &bb_min, const Vector3D &bb_max,
                                        const GPURay &r, float t0, float t1)
  {

    float tx_lo = (bb_min.x - r.o.x) * r.inv_d.x;
    float tx_hi = (bb_max.x - r.o.x) * r.inv_d.x;
    float ty_lo = (bb_min.y - r.o.y) * r.inv_d.y;
    float ty_hi = (bb_max.y - r.o.y) * r.inv_d.y;
    float tz_lo = (bb_min.z - r.o.z) * r.inv_d.z;
    float tz_hi = (bb_max.z - r.o.z) * r.inv_d.z;

    float t_enter = fmaxf(fmaxf(fminf(tx_lo, tx_hi), fminf(ty_lo, ty_hi)),
                          fmaxf(fminf(tz_lo, tz_hi), t0));
    float t_exit = fminf(fminf(fmaxf(tx_lo, tx_hi), fmaxf(ty_lo, ty_hi)),
                         fminf(fmaxf(tz_lo, tz_hi), t1));
    return t_enter <= t_exit;
  }


  __device__ inline bool intersect_scene(const GPUSceneView &scene,
                                         GPURay r, GPUIntersect &hit)
  {
    hit.prim_id = -1;
    hit.t = r.max_t;
    if (scene.num_nodes == 0)
      return false;
    const GPURay ray_in = r; // r.max_t shrinks during traversal

    int stack[GPU_BVH_STACK_SIZE];
    int sp = 0;
    stack[sp++] = 0; // root

    while (sp > 0)
    {
      const GPUBVHNode &node = scene.nodes[stack[--sp]];
      if (!intersect_bbox(node.bb_min, node.bb_max, r, r.min_t, r.max_t))
        continue;

      if (node.count > 0) // leaf
      {
        int end = node.start + node.count;
        for (int i = node.start; i < end; i++)
          intersect_primitive(scene, i, r, hit);
      }
      else
      {
        stack[sp++] = node.right;
        stack[sp++] = node.left;
      }
    }

    if (hit.prim_id < 0)
      return false;
    finish_hit(scene, ray_in, hit);
    return true;
  }

  // Any-hit query for shadow rays (mirrors BVHAccel::has_intersection).
  // Returns as soon as any primitive is hit in [r.min_t, r.max_t].
  __device__ inline bool has_intersection_scene(const GPUSceneView &scene, GPURay r)
  {
    if (scene.num_nodes == 0)
      return false;

    GPUIntersect tmp;
    tmp.prim_id = -1;
    tmp.t = r.max_t;

    int stack[GPU_BVH_STACK_SIZE];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0)
    {
      const GPUBVHNode &node = scene.nodes[stack[--sp]];
      if (!intersect_bbox(node.bb_min, node.bb_max, r, r.min_t, r.max_t))
        continue;

      if (node.count > 0)
      {
        int end = node.start + node.count;
        for (int i = node.start; i < end; i++)
          if (intersect_primitive(scene, i, r, tmp))
            return true;
      }
      else
      {
        stack[sp++] = node.right;
        stack[sp++] = node.left;
      }
    }

    return false;
  }


} // namespace CGL

#endif // CGL_GPU_INTERSECT_CUH
