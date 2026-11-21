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

/**
 * Device-side copy of the scene geometry. Owns its device memory;
 * call free() (or let the destructor) release it.
 */
struct GPUScene {
  GPUPrimitive* primitives = nullptr;  ///< device pointer
  size_t num_primitives = 0;

  // For now evaluate BSDF on host after intersection
  std::vector<BSDF*> host_bsdfs;

  GPUScene() = default;
  ~GPUScene() { free(); }
  GPUScene(const GPUScene&) = delete;
  GPUScene& operator=(const GPUScene&) = delete;

  // Convert primitives to GPU equivalent and send to device
  void upload(const std::vector<SceneObjects::Primitive*>& primitives);

  /// Convenience: collect every primitive in `scene` and upload.
  void upload(const SceneObjects::Scene* scene);

  void free();
};

} // namespace CGL

#endif // CGL_GPU_SCENE_H
