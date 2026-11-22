#include "gpu_scene.h"

#include "scene/scene.h"
#include "scene/primitive.h"
#include "scene/triangle.h"
#include "scene/sphere.h"

#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <unordered_map>

namespace CGL
{

  using namespace SceneObjects;

  void GPUScene::upload(const std::vector<Primitive *> &primitives)
  {
    // Free existing primitives
    this->free();
    // Build GPU prim list
    std::vector<GPUPrimitive_t> GPUprims;
    int nextId = 0;
    std::unordered_map<BSDF *, int> uniqueBSDF;
    GPUprims.reserve(primitives.size());
    for (Primitive *p : primitives)
    {
      GPUPrimitive_t gp;
      p->createGPU(&gp); // Fills in vectors and type
      BSDF *BSDFp = p->get_bsdf();
      auto id = uniqueBSDF.find(BSDFp);
      // If new bsdf
      if (id == uniqueBSDF.end())
      {
        // Insert it with next available id
        uniqueBSDF.insert({BSDFp, nextId});
        // Set primitive bsdf id
        gp.bsdf_id = nextId++;
        // Push to host_bsdf
        // Index should match with id
        this->host_bsdfs.push_back(BSDFp);
      }
      else
      {
        // otherwise just get id and set it
        gp.bsdf_id = id->second;
      }
      GPUprims.push_back(gp);
    }
    cudaError_t error;
    error = cudaMalloc(&this->primitives, sizeof(GPUPrimitive_t)*GPUprims.size());
    if(error != cudaSuccess){
      fprintf(stderr, "cudaMalloc failed in upload\n");
      exit(1);
    }
    error = cudaMemcpy(this->primitives, GPUprims.data(), sizeof(GPUPrimitive_t)*GPUprims.size(), cudaMemcpyHostToDevice);
    if(error != cudaSuccess){
      fprintf(stderr, "cudaMemcpy failed in upload\n");
      exit(1);
    }
    
    this->num_primitives = GPUprims.size();
  }

  // Collect all primitives in scene and upload
  void GPUScene::upload(const Scene *scene)
  {
    std::vector<Primitive *> primitives;
    for (SceneObject *obj : scene->objects)
    {
      const vector<Primitive *> &obj_prims = obj->get_primitives();
      primitives.reserve(primitives.size() + obj_prims.size());
      primitives.insert(primitives.end(), obj_prims.begin(), obj_prims.end());
    }
    this->upload(primitives);
    for (Primitive* p : primitives) delete p;
  }

  void GPUScene::free()
  {
    cudaError_t error = cudaFree(this->primitives);
    if (error != cudaSuccess)
    {
      fprintf(stderr, "Calling cudaFree failed at GPUScene::free\n");
    }
    error = cudaFree(this->nodes);
    if (error != cudaSuccess)
    {
      fprintf(stderr, "Calling cudaFree failed at GPUScene::free\n");
    }
    this->primitives = nullptr;
    this->num_primitives = 0;
    this->nodes = nullptr;
    this->num_nodes = 0;
    this->host_bsdfs.clear();
  }

  /* UNREVIEWED CLAUDE CODE TO BE DELETED */
  /* UNREVIEWED CLAUDE CODE THAT JUST UPLOADS EVERY PRIMITIVE AS A LEAF INTO THE BVH */
  void GPUScene::upload_dummy_bvh()
  {
    // Single leaf covering every primitive = brute-force intersection, but
    // through the same node layout the real BVH will use.
    if (this->nodes)
    {
      cudaFree(this->nodes);
      this->nodes = nullptr;
      this->num_nodes = 0;
    }
    if (this->num_primitives == 0)
      return;

    // Compute real scene bounds (finite bounds keep the slab test NaN-free).
    // The host copy from upload() is gone, so pull the primitives back once.
    // It's a one-time cost per scene load, and it goes away with the real BVH,
    // whose root node already has a bbox.
    std::vector<GPUPrimitive_t> host_prims(this->num_primitives);
    cudaError_t error = cudaMemcpy(host_prims.data(), this->primitives,
                                   sizeof(GPUPrimitive_t) * this->num_primitives,
                                   cudaMemcpyDeviceToHost);
    if (error != cudaSuccess)
    {
      fprintf(stderr, "cudaMemcpy (device->host) failed in upload_dummy_bvh: %s\n",
              cudaGetErrorString(error));
      exit(1);
    }

    Vector3D bb_min(INF_F), bb_max(-INF_F);
    auto expand = [&](const Vector3D &lo, const Vector3D &hi) {
      bb_min = Vector3D(fminf(bb_min.x, lo.x), fminf(bb_min.y, lo.y), fminf(bb_min.z, lo.z));
      bb_max = Vector3D(fmaxf(bb_max.x, hi.x), fmaxf(bb_max.y, hi.y), fmaxf(bb_max.z, hi.z));
    };
    for (const GPUPrimitive_t &p : host_prims)
    {
      if (p.type == GPU_PRIM_TRIANGLE)
      {
        expand(p.a, p.a);
        expand(p.b, p.b);
        expand(p.c, p.c);
      }
      else if (p.type == GPU_PRIM_SPHERE)
      {
        Vector3D r(p.b.x);  // b.x = radius
        expand(p.a - r, p.a + r);
      }
    }

    GPUBVHNode_t root;
    root.bb_min = bb_min;
    root.bb_max = bb_max;
    root.left = -1;
    root.right = -1;
    root.start = 0;
    root.count = (int)this->num_primitives;

    error = cudaMalloc(&this->nodes, sizeof(GPUBVHNode_t));
    if (error != cudaSuccess)
    {
      fprintf(stderr, "cudaMalloc failed in upload_dummy_bvh: %s\n",
              cudaGetErrorString(error));
      exit(1);
    }
    error = cudaMemcpy(this->nodes, &root, sizeof(GPUBVHNode_t),
                       cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
    {
      fprintf(stderr, "cudaMemcpy failed in upload_dummy_bvh: %s\n",
              cudaGetErrorString(error));
      exit(1);
    }
    this->num_nodes = 1;
  }
  /* END UNREVIEWED CLAUDE CODE */

} // namespace CGL
