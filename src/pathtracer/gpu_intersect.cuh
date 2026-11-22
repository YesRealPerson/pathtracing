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
  struct GPUIntersect
  {
    float t;
    Vector3D n;  // shading normal at the hit
    int prim_id; // index into scene.primitives (-1 = miss)
    int bsdf_id; // copied from the primitive
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

  __device__ inline bool intersect_triangle(const GPUPrimitive &p, int prim_id,
                                            GPURay &r, GPUIntersect &hit)
  {
    constexpr float epsilon = 1e-6f; // floating point bs
    Vector3D E1 = p.b - p.a;
    Vector3D E2 = p.c - p.a;
    Vector3D S = r.o - p.a; // b in Ax=b
    Vector3D S1 = cross(r.d, E2);
    Vector3D S2 = cross(S, E1);

    float parallel = dot(S1, E1);
    if (fabsf(parallel) < epsilon)
      return false;
    float det = 1.0 / parallel;
    float t = dot(S2, E2) * det;
    if (t < r.min_t || t > r.max_t)
      return false;
    float b1 = dot(S1, S) * det;
    if (b1 < 0)
      return false;
    float b2 = dot(S2, r.d) * det;
    if (b2 < 0 || b2 + b1 > 1)
      return false;
    hit.t = t;
    float alpha = 1 - b1 - b2;
    hit.n = (alpha * p.na + b1 * p.nb + b2 * p.nc).unit();
    hit.prim_id = prim_id;
    hit.bsdf_id = p.bsdf_id;
    r.max_t = hit.t;

    return true;
  }

  __device__ inline bool intersect_sphere(const GPUPrimitive &p, int prim_id,
                                          GPURay &r, GPUIntersect &hit)
  {
    // TODO
    return false;
  }

  __device__ inline bool intersect_primitive(const GPUSceneView &scene, int prim_id,
                                             GPURay &r, GPUIntersect &hit)
  {
    const GPUPrimitive &p = scene.primitives[prim_id];
    switch (p.type)
    {
    case GPU_PRIM_TRIANGLE:
      return intersect_triangle(p, prim_id, r, hit);
    case GPU_PRIM_SPHERE:
      return intersect_sphere(p, prim_id, r, hit);
    }
    return false;
  }

  __device__ inline bool intersect_bbox(const Vector3D &bb_min, const Vector3D &bb_max,
                                        const GPURay &r, float t0, float t1)
  {
    // TODO
    return true; // "always hit" is a safe placeholder: it just disables culling
  }

/* UNREVIEWED CLAUDE CODE TO BE DELETED */
  // Brute force: visit every node in the flat array and test every leaf's
  // primitives. Each primitive is in exactly one leaf, so this checks all of
  // them once. Correct for any flattened BVH (it just does no culling), and
  // with the dummy BVH it is a single leaf covering the whole scene.
  // Replace with a stack-based traversal + intersect_bbox once the real BVH
  // is on the GPU.
  __device__ inline bool intersect_scene(const GPUSceneView &scene,
                                         GPURay r, GPUIntersect &hit)
  {
    hit.prim_id = -1;
    hit.t = r.max_t;

    for (int n = 0; n < scene.num_nodes; n++)
    {
      const GPUBVHNode &node = scene.nodes[n];
      if (node.count <= 0)
        continue; // interior node: holds no primitives itself

      // intersect_primitive only accepts hits closer than r.max_t and then
      // shrinks it, so the last hit written is the closest.
      int end = node.start + node.count;
      for (int i = node.start; i < end; i++)
        intersect_primitive(scene, i, r, hit);
    }

    return hit.prim_id >= 0;
  }

  // Any-hit query for shadow rays (mirrors BVHAccel::has_intersection).
  // Returns as soon as any primitive is hit in [r.min_t, r.max_t].
  __device__ inline bool has_intersection_scene(const GPUSceneView &scene, GPURay r)
  {
    GPUIntersect tmp;
    tmp.prim_id = -1;
    tmp.t = r.max_t;

    for (int n = 0; n < scene.num_nodes; n++)
    {
      const GPUBVHNode &node = scene.nodes[n];
      if (node.count <= 0)
        continue;

      int end = node.start + node.count;
      for (int i = node.start; i < end; i++)
        if (intersect_primitive(scene, i, r, tmp))
          return true; // any hit is enough -- no need to find the closest
    }

    return false;
  }

/* END UNREVIEWED CLAUDE CODE */

} // namespace CGL

#endif // CGL_GPU_INTERSECT_CUH
