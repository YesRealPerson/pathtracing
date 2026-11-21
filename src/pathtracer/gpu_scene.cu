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
    this->primitives = nullptr;
    this->num_primitives = 0;
    this->host_bsdfs.clear();
  }

} // namespace CGL
