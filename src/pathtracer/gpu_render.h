#ifndef CGL_GPU_RENDER_H
#define CGL_GPU_RENDER_H

// Host-callable entry points for GPU rendering. Safe to include from .cpp.

#include "gpu_scene.h"

namespace CGL {
void render(const GPUSceneView& scene, const GPUCamera cam, Vector3D* host_out, int w, int h);

} // namespace CGL

#endif // CGL_GPU_RENDER_H
