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

// Emission & Diffuse only for now, and maybe forever
typedef enum BSDFType{
  EMISSIVE,
  DIFFUSE
} BSDFType_t;


// Only takes CosineWeightedHemisphereSampler3D
typedef struct GPUBSDF {
  BSDFType_t type;
  Vector3D reflectance, radiance;
} GPUBSDF_t;

typedef enum GPULightType {
  GPU_LIGHT_POINT       = 0,
  GPU_LIGHT_DIRECTIONAL = 1,
  GPU_LIGHT_AREA        = 2,
  GPU_LIGHT_HEMISPHERE  = 3, 
  GPU_LIGHT_UNKNOWN     = 4,
} GPULightType_t;

typedef struct GPULight {
  GPULightType_t type;
  Vector3D radiance;
  Vector3D position;
  Vector3D direction;
  Vector3D dim_x, dim_y;
  float area;
} GPULight_t;

// Pointers/information needed for GPU kernels
typedef struct GPUSceneView {
  const GPUPrimitive* primitives;
  const GPUBVHNode*   nodes;
  const GPUBSDF*      bsdfs; // indexed by GPUPrimitive::bsdf_id
  const GPULight*     lights;
  int num_primitives;
  int num_nodes;
  int num_bsdfs;
  int num_lights;

  // Render settings
  int ns_aa;          // camera rays per pixel
  int max_ray_depth;  // maximum ray depth
  int ns_area_light;  // samples per area light
  int samples_per_batch; // samples between adaptive sampling convergence checks
} GPUSceneView_t;

struct GPUScene {
  GPUPrimitive* primitives = nullptr;
  size_t num_primitives = 0;

  GPUBVHNode* nodes = nullptr;
  size_t num_nodes = 0;

  size_t ns_aa = 1;          // camera rays per pixel
  size_t max_ray_depth = 1;  // maximum ray depth
  size_t ns_area_light = 1;  // samples per area light
  size_t samples_per_batch = 32; // samples between adaptive sampling convergence checks

  GPUBSDF* bsdfs = nullptr; // indexed by GPUPrimitive::bsdf_id
  size_t num_bsdfs = 0;

  GPULight* lights = nullptr;
  size_t num_lights = 0;

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
    return { primitives, nodes, bsdfs, lights,
             (int)num_primitives, (int)num_nodes, (int)num_bsdfs, (int)num_lights,
             (int)ns_aa, (int)max_ray_depth, (int)ns_area_light,
             (int)samples_per_batch };
  }

  void free();
};

} // namespace CGL

#endif // CGL_GPU_SCENE_H
