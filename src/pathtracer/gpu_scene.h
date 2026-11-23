#ifndef CGL_GPU_SCENE_H
#define CGL_GPU_SCENE_H

#include "CGL/CGL.h"
#include "CGL/vector3D.h"

#include <vector>

namespace CGL {

class BSDF;
namespace SceneObjects { class Primitive; struct Scene; }

typedef enum PrimType {
  GPU_PRIM_TRIANGLE = 0,
  GPU_PRIM_SPHERE   = 1,
} PrimType_t;

// Reusable primitive struct
// Triangle a,b,c = p1,p2,p3   na,nb,nc = n1,n2,n3
// Sphere a = origin  b.x = r, b.y = r^2
typedef struct GPUPrimitive {
  PrimType_t type;
  Vector3D a, b, c;
  Vector3D na, nb, nc;
  int bsdf_id; // index for BSDF list
} GPUPrimitive_t;

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
  Vector3D c2w0, c2w1, c2w2;   // columns of the camera-to-world rotation
  float tan_half_hfov, tan_half_vfov;
  float nclip, fclip;
} GPUCamera_t;

// Pointers/information needed for GPU kernels
typedef struct GPUSceneView {
  const GPUPrimitive* primitives;
  const GPUBVHNode*   nodes;
  int num_primitives;
  int num_nodes;
} GPUSceneView_t;

struct GPUScene {
  GPUPrimitive* primitives = nullptr;
  size_t num_primitives = 0;

  GPUBVHNode* nodes = nullptr;
  size_t num_nodes = 0;

  // For now evaluate BSDF on host after intersection
  // I lied this idea sucks
  std::vector<BSDF*> host_bsdfs;

  GPUScene() = default;
  ~GPUScene() { free(); }
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

  // Convert primitives to GPU equivalent, build the BVH, and send both to device
  // Primitives are uploaded in BVH order so each leaf is a contiguous range
  void upload(const std::vector<SceneObjects::Primitive*>& primitives,
              size_t max_leaf_size = 4);

  /// Convenience: collect every primitive in `scene` and upload.
  void upload(const SceneObjects::Scene* scene, size_t max_leaf_size = 4);

  GPUSceneView view() const {
    return { primitives, nodes, (int)num_primitives, (int)num_nodes };
  }

  void free();
};

} // namespace CGL

#endif // CGL_GPU_SCENE_H
