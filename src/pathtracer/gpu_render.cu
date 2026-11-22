#include "gpu_render.h"
#include "gpu_intersect.cuh"

#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>

#define GPU_RENDER_CHECK(call)                                               \
  do {                                                                       \
    cudaError_t err_ = (call);                                               \
    if (err_ != cudaSuccess) {                                               \
      fprintf(stderr, "[CUDA] %s failed at %s:%d: %s\n", #call, __FILE__,    \
              __LINE__, cudaGetErrorString(err_));                           \
      std::exit(EXIT_FAILURE);                                               \
    }                                                                        \
  } while (0)

namespace CGL {

// See Camera::generate_ray and GPU Camera
__device__ inline GPURay camera_ray(const GPUCamera& c, float x, float y) {
  float xc = (2.f * x - 1.f) * c.tan_half_hfov;
  float yc = (2.f * y - 1.f) * c.tan_half_vfov;
  Vector3D d = (c.c2w0 * xc + c.c2w1 * yc - c.c2w2).unit();
  return make_gpu_ray(c.pos, d, c.nclip, c.fclip);
}

// PathTracer::debug_shading / normal_shading (pathtracer.h) except on device.
__device__ inline Vector3D debug_shading(const Vector3D& d) {
  return Vector3D(fabsf(d.r), fabsf(d.g), 0.f).unit();
}

__device__ inline Vector3D normal_shading(const Vector3D& n) {
  return n * 0.5f + Vector3D(0.5f);
}

/* TEMPORARY CLAUDE CODE NEEDS TO BE EDITED OR REPLACED ENTIRELY */
__global__ void render_normals_kernel(GPUSceneView scene, GPUCamera cam,
                                      Vector3D* out, int w, int h) {
  int x = blockIdx.x * blockDim.x + threadIdx.x;
  int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= w || y >= h) return;   // the grid is rounded up past the image edge

  GPURay r = camera_ray(cam, (x + 0.5f) / w, (y + 0.5f) / h);

  GPUIntersect hit;
  Vector3D L_out(0.f);
  if (intersect_scene(scene, r, hit)) {
    L_out = (hit.t == INFINITY) ? debug_shading(r.d) : normal_shading(hit.n);
  }
  out[x + y * w] = L_out;   // same layout as HDRImageBuffer::update_pixel
}



void gpu_render_normals(const GPUSceneView& scene, const GPUCamera& cam,
                        Vector3D* host_out, int w, int h) {
  if (w <= 0 || h <= 0 || scene.num_nodes == 0) return;

  size_t bytes = sizeof(Vector3D) * (size_t)w * (size_t)h;
  Vector3D* d_out = nullptr;
  GPU_RENDER_CHECK(cudaMalloc(&d_out, bytes));

  // 16x16 = 256 threads per block: a multiple of the 32-thread warp, and
  // neighbouring pixels in a warp follow similar paths through the BVH.
  dim3 block(16, 16);
  dim3 grid((w + block.x - 1) / block.x, (h + block.y - 1) / block.y);
  render_normals_kernel<<<grid, block>>>(scene, cam, d_out, w, h);

  // Launches return nothing: check for launch errors, then for errors raised
  // while the kernel ran (e.g. an out-of-bounds access).
  GPU_RENDER_CHECK(cudaGetLastError());
  GPU_RENDER_CHECK(cudaDeviceSynchronize());

  GPU_RENDER_CHECK(cudaMemcpy(host_out, d_out, bytes, cudaMemcpyDeviceToHost));
  GPU_RENDER_CHECK(cudaFree(d_out));
}
/* END TEMPORARY CLAUDE CODE */

} // namespace CGL
