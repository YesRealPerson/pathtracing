#include "gpu_render.h"
#include "gpu_intersect.cuh"
#include "CGL/vector2D.h"

#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <curand.h>

#include <cstdio>
#include <cstdlib>

// From CUDA docs
#define CUDA_CALL(x)                                  \
  do                                                  \
  {                                                   \
    if ((x) != cudaSuccess)                           \
    {                                                 \
      printf("Error at %s:%d\n", __FILE__, __LINE__); \
      exit(1);                                        \
    }                                                 \
  } while (0)
// large float offset
#define EPS_F2 1e-4f

namespace CGL
{

  // See Camera::generate_ray and GPU Camera
  __device__ inline GPURay camera_ray(const GPUCamera &c, float x, float y)
  {
    float xc = (2.f * x - 1.f) * c.tan_half_hfov;
    float yc = (2.f * y - 1.f) * c.tan_half_vfov;
    Vector3D d = (c.c2w * Vector3D(xc, yc, -1.f)).unit();
    return make_gpu_ray(c.pos, d, c.nclip, c.fclip);
  }

  // see UniformGridSampler2D
  __device__ inline Vector2D gpu_sample_grid_2d(curandStatePhilox4_32_10_t *rng)
  {
    return Vector2D(curand_uniform(rng), curand_uniform(rng));
  }

  // see CosineWeightedHemisphereSampler3D
  __device__ inline Vector3D gpu_sample_cosine_hemisphere(curandStatePhilox4_32_10_t *rng, float *pdf)
  {
    float Xi1 = curand_uniform(rng);
    float Xi2 = curand_uniform(rng);
    float r = sqrtf(Xi1);
    float theta = 2.f * PI_F * Xi2;
    float z = sqrtf(fmaxf(0.f, 1.f - Xi1));
    float s, c;
    sincosf(theta, &s, &c);
    *pdf = z / PI_F;
    return Vector3D(r * c, r * s, z);
  }

  // see UniformHemisphereSampler3D (pdf = 1 / 2pi)
  __device__ inline Vector3D gpu_sample_uniform_hemisphere(curandStatePhilox4_32_10_t *rng)
  {
    float z = curand_uniform(rng); // cos(theta)
    float sinTheta = sqrtf(fmaxf(0.f, 1.f - z * z));
    float phi = 2.f * PI_F * curand_uniform(rng);
    float s, c;
    sincosf(phi, &s, &c);
    return Vector3D(c * sinTheta, s * sinTheta, z);
  }

  // See SceneLight::sample_L implementations in light.cpp
  // p is the hit point (world space); wi is returned in world space
  __device__ inline Vector3D sample_L(const GPULight &L, curandStatePhilox4_32_10_t *rng,
                                      const Vector3D &p, Vector3D *wi,
                                      float *distToLight, float *pdf)
  {
    switch (L.type)
    {
    case GPU_LIGHT_POINT:
    {
      Vector3D d = L.position - p;
      *distToLight = d.norm();
      *wi = d / *distToLight;
      *pdf = 1.f;
      return L.radiance;
    }
    case GPU_LIGHT_DIRECTIONAL:
    {
      *wi = L.direction;
      *distToLight = INFINITY;
      *pdf = 1.f;
      return L.radiance;
    }
    case GPU_LIGHT_AREA:
    {
      Vector2D s = gpu_sample_grid_2d(rng) - Vector2D(0.5f, 0.5f);
      Vector3D d = L.position + s.x * L.dim_x + s.y * L.dim_y - p;
      float cosTheta = dot(d, L.direction);
      float sqDist = d.norm2();
      float dist = sqrtf(sqDist);
      *wi = d / dist;
      *distToLight = dist;
      *pdf = sqDist / (L.area * fabsf(cosTheta));
      return cosTheta < 0.f ? L.radiance : Vector3D(0.f);
    }
    case GPU_LIGHT_HEMISPHERE:
    {
      Vector3D s = gpu_sample_uniform_hemisphere(rng);
      *wi = s.x * L.dim_x + s.y * L.dim_y + s.z * L.direction;
      *distToLight = INFINITY;
      *pdf = 1.f / (2.f * PI_F);
      return L.radiance;
    }
    default: // Bad light
      *wi = Vector3D(0.f);
      *distToLight = 0.f;
      *pdf = 1.f;
      return Vector3D(0.f);
    }
  }
  // Note function signature needs to change if we add other materials
  // For sample_f too
  __device__ inline Vector3D bsdf_f(GPUBSDF bsdf)
  {
    switch (bsdf.type)
    {
    case EMISSIVE:
      return Vector3D(0.f);
    case DIFFUSE:
      return bsdf.reflectance / PI_F;
    default:
      return Vector3D(0.f);
    }
  }

  __device__ Vector3D bsdf_sample_f(GPUBSDF bsdf, curandStatePhilox4_32_10_t *rng,
                                    Vector3D *wi, float *pdf)
  {
    Vector3D f = bsdf_f(bsdf);
    switch (bsdf.type)
    {
    case EMISSIVE:
    case DIFFUSE:
      *wi = gpu_sample_cosine_hemisphere(rng, pdf);
      return bsdf_f(bsdf);
    default:
      return Vector3D(0.f);
    }
  }

  // See bsdf.cpp make_coord_space
  __device__ void gpu_make_coord_space(Matrix3x3 &o2w, const Vector3D n)
  {
    Vector3D z = Vector3D(n.x, n.y, n.z);
    Vector3D h = z;
    if (fabsf(h.x) <= fabsf(h.y) && fabsf(h.x) <= fabsf(h.z))
      h.x = 1.0;
    else if (fabsf(h.y) <= fabsf(h.x) && fabsf(h.y) <= fabsf(h.z))
      h.y = 1.0;
    else
      h.z = 1.0;

    z.normalize();
    Vector3D y = cross(h, z);
    y.normalize();
    Vector3D x = cross(z, y);
    x.normalize();

    o2w[0] = x;
    o2w[1] = y;
    o2w[2] = z;
  }
  // Returns luminance at this point in the scene
  __device__ Vector3D bounce(GPUSceneView scene, curandStatePhilox4_32_10_t *rng,
                             GPURay r, GPUIntersect hit)
  {
    // hit is garaunteed to be properly filled
    Vector3D L_out(0.f);
    // One bounce
    const Vector3D hit_p = r.o + r.d * hit.t;
    Vector3D fr = bsdf_f(scene.bsdfs[hit.bsdf_id]);
    for (int j = 0; j < scene.num_lights; j++)
    {
      const GPULight light = scene.lights[j];
      if (light.type == GPU_LIGHT_UNKNOWN)
        continue;
      Vector3D out, wi;
      float dist, pdf;
      int n = scene.ns_area_light;
      if (light.type == GPU_LIGHT_POINT || light.type == GPU_LIGHT_DIRECTIONAL)
        n = 1;
      for (int i = 0; i < n; i++)
      {
        Vector3D L = sample_L(light, rng, hit_p, &wi, &dist, &pdf);
        GPURay i2l = make_gpu_ray(hit_p + EPS_F2 * hit.n, wi, 0.f, dist - EPS_F2); // origin at hit point, random direction
        // Nothign blocking th elight
        if (!has_intersection_scene(scene, i2l))
        {
          // both need to be in object coords
          float theta = dot(hit.n, wi);
          if (theta < 0)
            continue; // light is behind the surface
          out += fr * L * theta / pdf;
        }
      }
      L_out += out / n;
    }
    return L_out;
  }
  __global__ void render_kernel(
      GPUSceneView scene, GPUCamera cam,
      Vector3D *accum, int w, int h,
      curandStatePhilox4_32_10_t *states)
  {
    // thread id
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= w || y >= h)
      return;
    int id = y * w + x;
    curandStatePhilox4_32_10_t rng = states[id];

    float xOfst = curand_uniform(&rng);
    float yOfst = curand_uniform(&rng);
    GPURay r = camera_ray(cam, (x + xOfst) / w, (y + yOfst) / h);
    int depth = scene.max_ray_depth;
    Vector3D L_out = Vector3D(0.f);
    GPUIntersect hit;
    Vector3D fr = Vector3D(1.f);

    while (depth > 0)
    {
      if (!intersect_scene(scene, r, hit))
        break;

      if (depth == scene.max_ray_depth) L_out += scene.bsdfs[hit.bsdf_id].radiance;
      if (scene.bsdfs[hit.bsdf_id].type == EMISSIVE) break;

      // Light at hit point, initial hit has fr of 1
      L_out += fr * bounce(scene, &rng, r, hit);

      // Change of Basis stuff
      Matrix3x3 o2w;
      gpu_make_coord_space(o2w, hit.n);
      Matrix3x3 w2o = o2w.T();

      Vector3D hit_p = r.o + r.d * hit.t;
      Vector3D w_out = w2o * (-r.d);

      // Setup for next iter
      Vector3D wi;
      float pdf;
      Vector3D nextf = bsdf_sample_f(scene.bsdfs[hit.bsdf_id], &rng, &wi, &pdf);
      Vector3D wiw = o2w * wi;
      float theta = dot(hit.n, wiw);
      fr = fr* nextf * theta / pdf;
      r = make_gpu_ray(hit_p + EPS_F2 * wiw, wiw, 0.f, INF_F);
      depth--;
    }
    accum[id] += L_out;
    states[id] = rng;
  }

  // Setup randoms per pixel
  __global__ void curand_setup(curandStatePhilox4_32_10_t *states, int w, int h)
  {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= w || y >= h)
      return;
    int id = y * w + x;
    // seed, subsequence(?), offset, state
    curand_init(184, id, 0, &states[id]);
  }

  // 16x16 block of 256 threads
  // Each "warp" is 32 threads, so blocks should be a multiple of 32
  // GPUs cap resident threads, 4080 super caps at 1536
  // largest same size block would be 16x16
  static const dim3 kBlock(16, 16);

  // Number of blocks needed to cover the entire image
  static inline dim3 grid_for(int w, int h)
  {
    return dim3((w + kBlock.x - 1) / kBlock.x, (h + kBlock.y - 1) / kBlock.y);
  }

  void render_free(GPURenderBuffers &buf)
  {
    if (buf.states)
      CUDA_CALL(cudaFree(buf.states));
    if (buf.d_out)
      CUDA_CALL(cudaFree(buf.d_out));
    buf.states = nullptr;
    buf.d_out = nullptr;
    buf.w = buf.h = 0;
  }

  void render_init(GPURenderBuffers &buf, int w, int h)
  {
    if (w <= 0 || h <= 0)
      return;
    // Already set up at this size: keep the RNG streams going, just reset
    // the accumulator for a fresh render
    if (buf.states && buf.d_out && buf.w == w && buf.h == h)
    {
      CUDA_CALL(cudaMemset(buf.d_out, 0, (size_t)w * h * sizeof(Vector3D)));
      return;
    }

    render_free(buf);
    size_t pixels = (size_t)w * h;
    CUDA_CALL(cudaMalloc(&buf.states, pixels * sizeof(curandStatePhilox4_32_10_t)));
    CUDA_CALL(cudaMalloc(&buf.d_out, pixels * sizeof(Vector3D)));
    CUDA_CALL(cudaMemset(buf.d_out, 0, pixels * sizeof(Vector3D)));
    buf.w = w;
    buf.h = h;

    curand_setup<<<grid_for(w, h), kBlock>>>(buf.states, w, h);
    CUDA_CALL(cudaGetLastError());
    CUDA_CALL(cudaDeviceSynchronize());
  }

  void render(const GPUSceneView &scene, const GPUCamera cam, GPURenderBuffers &buf)
  {
    if (!buf.states || !buf.d_out || scene.num_nodes == 0)
      return;
    render_kernel<<<grid_for(buf.w, buf.h), kBlock>>>(scene, cam, buf.d_out,
                                                      buf.w, buf.h, buf.states);
    CUDA_CALL(cudaGetLastError());
    CUDA_CALL(cudaDeviceSynchronize());
  }

  void render_readback(const GPURenderBuffers &buf, Vector3D *host_out, int num_samples)
  {
    if (!buf.d_out || num_samples <= 0)
      return;
    size_t pixels = (size_t)buf.w * buf.h;
    CUDA_CALL(cudaMemcpy(host_out, buf.d_out, pixels * sizeof(Vector3D),
                         cudaMemcpyDeviceToHost));
    float inv = 1.f / num_samples;
    for (size_t i = 0; i < pixels; i++)
      host_out[i] *= inv;
  }

} // namespace CGL
