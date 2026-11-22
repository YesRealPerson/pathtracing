#ifndef CGL_GPU_RENDER_H
#define CGL_GPU_RENDER_H

// Host-callable entry points for GPU rendering. Safe to include from .cpp.

#include "gpu_scene.h"

namespace CGL {
/* TEMPORARY CLAUDE CODE NEEDS TO BE EDITED */
/**
 * Render one ray per pixel through the pixel center, shading each hit by its
 * normal (n * 0.5 + 0.5) and misses black. Blocks until done.
 *
 * \param host_out  w*h Vector3D buffer on the HOST, row-major (x + y*w), e.g.
 *                  PathTracer::sampleBuffer.data.data()
 */
void gpu_render_normals(const GPUSceneView &scene, const GPUCamera &cam,
                        Vector3D *host_out, int w, int h);

/* END TEMPORARY CLAUDE CODE */

} // namespace CGL

#endif // CGL_GPU_RENDER_H
