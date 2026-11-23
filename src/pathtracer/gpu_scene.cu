#include "gpu_scene.h"

#include "scene/scene.h"
#include "scene/primitive.h"
#include "scene/triangle.h"
#include "scene/sphere.h"
#include "scene/bbox.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <unordered_map>

namespace CGL
{

  using namespace SceneObjects;

  namespace
  {
    
    struct BVHBuilder
    {
      const std::vector<BBox> &bboxes;
      std::vector<int> &order;
      std::vector<GPUBVHNode_t> &nodes;
      size_t max_leaf_size;
      int max_depth = 0;

      int build(size_t start, size_t end, int depth)
      {
        max_depth = std::max(max_depth, depth);

        BBox bbox;
        for (size_t i = start; i < end; i++)
          bbox.expand(bboxes[order[i]]);
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

        BBox cbox;
        for (size_t i = start; i < end; i++)
          cbox.expand(bboxes[order[i]].centroid());
        float e[3] = {cbox.extent.x, cbox.extent.y, cbox.extent.z};
        int axis = std::max_element(e, e + 3) - e;
        float split = (cbox.min[axis] + cbox.max[axis]) / 2;

        std::vector<int> left, right;
        for (size_t i = start; i < end; i++)
        {
          int p = order[i];
          if (bboxes[p].centroid()[axis] < split)
            left.push_back(p);
          else
            right.push_back(p);
        }

        if (left.empty() || right.empty())
        {
          left.assign(order.begin() + start, order.begin() + end);
          std::sort(left.begin(), left.end(), [&](int a, int b) {
            return bboxes[a].centroid()[axis] < bboxes[b].centroid()[axis];
          });
          right.assign(left.begin() + left.size() / 2, left.end());
          left.resize(left.size() / 2);
        }
        auto temp = std::copy(left.begin(), left.end(), order.begin() + start);
        std::copy(right.begin(), right.end(), temp);
        size_t mid = start + left.size();

        int l = build(start, mid, depth + 1);
        int r = build(mid, end, depth + 1);
        nodes[idx].left = l;
        nodes[idx].right = r;
        nodes[idx].start = 0;
        nodes[idx].count = 0;
        return idx;
      }
    };
  }

  void GPUScene::upload(const std::vector<Primitive *> &primitives,
                        size_t max_leaf_size)
  {
    // frees any primitives that were in the GPU
    this->free();
    if (primitives.empty())
      return;

    //build the BVH
    std::vector<BBox> bboxes;
    bboxes.reserve(primitives.size());
    for (Primitive *p : primitives)
      bboxes.push_back(p->get_bbox());

    std::vector<int> order(primitives.size());
    for (size_t i = 0; i < order.size(); i++)
      order[i] = (int)i;

    std::vector<GPUBVHNode_t> nodes;
    BVHBuilder builder{bboxes, order, nodes, max_leaf_size};
    builder.build(0, order.size(), 0);

    if (builder.max_depth + 1 > GPU_BVH_STACK_SIZE)
    {
      fprintf(stderr, "[GPUScene] BVH depth %d exceeds traversal stack size %d\n",
              builder.max_depth, GPU_BVH_STACK_SIZE);
      exit(1);
    }

    // Build GPU prim list based on BVH order 
    std::vector<GPUPrimitive_t> GPUprims;
    int nextId = 0;
    std::unordered_map<BSDF *, int> uniqueBSDF;
    GPUprims.reserve(primitives.size());
    for (int i : order)
    {
      Primitive *p = primitives[i];
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

    error = cudaMalloc(&this->nodes, sizeof(GPUBVHNode_t) * nodes.size());
    if (error != cudaSuccess)
    {
      fprintf(stderr, "cudaMalloc failed in upload (BVH nodes)\n");
      exit(1);
    }
    error = cudaMemcpy(this->nodes, nodes.data(), sizeof(GPUBVHNode_t) * nodes.size(), cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
    {
      fprintf(stderr, "cudaMemcpy failed in upload (BVH nodes)\n");
      exit(1);
    }
    this->num_nodes = nodes.size();

    fprintf(stdout, "Uploaded %zu primitives, %zu BVH nodes (max depth %d)\n",
            this->num_primitives, this->num_nodes, builder.max_depth);
  }

  // Collect all primitives in scene and upload
  void GPUScene::upload(const Scene *scene, size_t max_leaf_size)
  {
    std::vector<Primitive *> primitives;
    for (SceneObject *obj : scene->objects)
    {
      const vector<Primitive *> &obj_prims = obj->get_primitives();
      primitives.reserve(primitives.size() + obj_prims.size());
      primitives.insert(primitives.end(), obj_prims.begin(), obj_prims.end());
    }
    this->upload(primitives, max_leaf_size);
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

} // namespace CGL
