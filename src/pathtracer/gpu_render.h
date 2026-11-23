#ifndef CGL_GPU_RENDER_H
#define CGL_GPU_RENDER_H

// Host-callable entry points for GPU rendering. Safe to include from .cpp.

#include "gpu_scene.h"

// Forward declaration so .cpp files don't need curand_kernel.h
// (curandStatePhilox4_32_10_t is a typedef of this struct).
struct curandStatePhilox4_32_10;

namespace CGL {

// Device buffers that persist across render() calls.
struct GPURenderBuffers {
  curandStatePhilox4_32_10 *states = nullptr; // one RNG state per pixel
  Vector3D *d_out = nullptr;                  // device frame buffer
  int w = 0, h = 0;
};

// Prepares buf for a new w x h render: allocates the accumulation buffer +
// RNG states and seeds them if the size changed, and always zeroes the
// accumulator. At an unchanged size the RNG streams keep advancing.
void render_init(GPURenderBuffers &buf, int w, int h);

// Frees everything render_init allocated. Safe to call on empty buffers.
void render_free(GPURenderBuffers &buf);

// Traces one sample per pixel and adds it into buf.d_out. Blocks until done.
void render(const GPUSceneView &scene, const GPUCamera cam, GPURenderBuffers &buf);

// Copies the accumulator to host_out (w*h entries) divided by num_samples.
void render_readback(const GPURenderBuffers &buf, Vector3D *host_out, int num_samples);

} // namespace CGL

#endif // CGL_GPU_RENDER_H
