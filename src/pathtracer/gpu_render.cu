#include "gpu_render.h"
#include "gpu_intersect.cuh"
#include "CGL/vector2D.h"

#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <curand.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

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

  __device__ inline bool gpu_coin_flip(curandStatePhilox4_32_10_t *rng, float chance)
  {
    return curand_uniform(rng) < chance;
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
    case GPU_LIGHT_SPOT:
    {
      Vector3D d = L.position - p;
      float d2 = d.norm2();
      *distToLight = sqrtf(d2);
      *wi = d / *distToLight;
      *pdf = 1.f;
      Vector3D rad = L.radiance;
      if (L.inv_square)
      {
        // radiance holds intensity (glTF candela * color): E = I / d^2,
        // windowed to zero at `range` as the glTF spec recommends.
        float att = 1.f / fmaxf(d2, 1e-8f);
        if (L.range > 0.f)
        {
          float q = d2 / (L.range * L.range); // (d / range)^2
          float w = fminf(fmaxf(1.f - q * q, 0.f), 1.f);
          att *= w * w;
        }
        rad = rad * att;
      }
      if (L.type == GPU_LIGHT_SPOT)
      {
        // glTF smooth cone falloff between the inner and outer angle
        float cd = -dot(L.direction, *wi);
        float t = (cd - L.cos_outer) / fmaxf(L.cos_inner - L.cos_outer, 1e-4f);
        t = fminf(fmaxf(t, 0.f), 1.f);
        rad = rad * (t * t);
      }
      return rad;
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
  // ---- Textures --------------------------------------------------------------

  // sRGB byte -> linear float, filled by init_srgb_lut().
  __constant__ float c_srgb_to_linear[256];

  __device__ inline Vector3D texel_linear(uint32_t t)
  {
    return Vector3D(c_srgb_to_linear[t & 0xff],
                    c_srgb_to_linear[(t >> 8) & 0xff],
                    c_srgb_to_linear[(t >> 16) & 0xff]);
  }

  // Bilinear lookup with the slot's KHR_texture_transform applied. Filtering
  // happens in linear space. (No mipmaps: the many samples per pixel already
  // average the texture over each pixel's footprint.)
  __device__ inline Vector3D sample_texture(const GPUSceneView &scene, const GPUTexRef &ref,
                                            float u, float v)
  {
    const GPUTexture &tex = scene.textures[ref.tex];
    float tu = ref.m[0] * u + ref.m[1] * v + ref.m[2];
    float tv = ref.m[3] * u + ref.m[4] * v + ref.m[5];
    if (!isfinite(tu) || !isfinite(tv))
      return Vector3D(1.f);

    float x = tu * tex.w - 0.5f, y = tv * tex.h - 0.5f;
    float fx = floorf(x), fy = floorf(y);
    float ax = x - fx, ay = y - fy;
    // keep the integer math in range for huge (tiled) UVs
    int ix = (int)fmodf(fx, (float)(tex.w * 1024)), iy = (int)fmodf(fy, (float)(tex.h * 1024));
    int x0 = wrap_texel(ix, tex.w, tex.wrap_s), x1 = wrap_texel(ix + 1, tex.w, tex.wrap_s);
    int y0 = wrap_texel(iy, tex.h, tex.wrap_t), y1 = wrap_texel(iy + 1, tex.h, tex.wrap_t);

    const uint32_t *px = scene.texels + tex.offset;
    Vector3D c00 = texel_linear(px[(size_t)y0 * tex.w + x0]);
    Vector3D c10 = texel_linear(px[(size_t)y0 * tex.w + x1]);
    Vector3D c01 = texel_linear(px[(size_t)y1 * tex.w + x0]);
    Vector3D c11 = texel_linear(px[(size_t)y1 * tex.w + x1]);
    return (c00 * (1.f - ax) + c10 * ax) * (1.f - ay) + (c01 * (1.f - ax) + c11 * ax) * ay;
  }

  // ---- Materials -------------------------------------------------------------

  // A BSDF evaluated at one hit point (textures applied).
  struct GPUShade
  {
    Vector3D albedo;   // diffuse reflectance
    Vector3D emission; // emitted radiance
    bool emitter_only; // EMISSIVE: the path stops here
    bool emit_indirect; // emission also counts on bounce rays
  };

  __device__ inline GPUShade shade_hit(const GPUSceneView &scene, const GPUIntersect &hit)
  {
    const GPUBSDF &b = scene.bsdfs[hit.bsdf_id];
    GPUShade s;
    s.emitter_only = b.type == EMISSIVE;
    s.emit_indirect = b.emit_indirect != 0;
    s.albedo = s.emitter_only ? Vector3D(0.f) : b.reflectance;
    s.emission = b.radiance;
    if (b.base_tex.tex >= 0 && !s.emitter_only)
      s.albedo = s.albedo * sample_texture(scene, b.base_tex, hit.u, hit.v);
    if (b.emit_tex.tex >= 0)
      s.emission = s.emission * sample_texture(scene, b.emit_tex, hit.u, hit.v);
    return s;
  }

  // Diffuse only (emitters have zero albedo, so this is 0 for them).
  __device__ inline Vector3D bsdf_f(const GPUShade &s)
  {
    return s.albedo / PI_F;
  }

  __device__ Vector3D bsdf_sample_f(const GPUShade &s, curandStatePhilox4_32_10_t *rng,
                                    Vector3D *wi, float *pdf)
  {
    *wi = gpu_sample_cosine_hemisphere(rng, pdf);
    return bsdf_f(s);
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
                             GPURay r, GPUIntersect hit, Vector3D fr)
  {
    // hit is garaunteed to be properly filled; fr is the BSDF value there
    Vector3D L_out(0.f);
    // One bounce
    const Vector3D hit_p = r.o + r.d * hit.t;
    for (int j = 0; j < scene.num_lights; j++)
    {
      const GPULight light = scene.lights[j];
      if (light.type == GPU_LIGHT_UNKNOWN)
        continue;
      Vector3D out, wi;
      float dist, pdf;
      int n = scene.ns_area_light;
      if (light.type == GPU_LIGHT_POINT || light.type == GPU_LIGHT_DIRECTIONAL ||
          light.type == GPU_LIGHT_SPOT)
        n = 1;
      for (int i = 0; i < n; i++)
      {
        Vector3D L = sample_L(light, rng, hit_p, &wi, &dist, &pdf);
        // light is behind the surface (geometric check stops light leaking
        // through thin double-sided walls; shading check for the cosine)
        float theta = dot(hit.n, wi);
        if (theta <= 0.f || dot(hit.ng, wi) <= 0.f)
          continue;
        // Offset along the geometric normal, on the side the ray came from
        GPURay i2l = make_gpu_ray(hit_p + EPS_F2 * hit.ng, wi, 0.f, dist - EPS_F2);
        // Nothign blocking th elight
        if (!has_intersection_scene(scene, i2l))
          out += fr * L * theta / pdf;
      }
      L_out += out / n;
    }
    return L_out;
  }
  __global__ void render_kernel(
      GPUSceneView scene, GPUCamera cam,
      Vector3D *accum, Vector3D *s12, bool *converge,
      int w, int h,
      curandStatePhilox4_32_10_t *states)
  {
    // thread id
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= w || y >= h)
      return;
    int id = y * w + x;
    if (converge[id])
      return;
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

      const GPUShade shade = shade_hit(scene, hit);
      if (scene.debug_albedo)
      {
        L_out = shade.albedo; // texture debugging: base color, no lighting
        break;
      }
      if (depth == scene.max_ray_depth)
        L_out += shade.emission;          // seen directly (fr == 1 here)
      else if (shade.emit_indirect)
        L_out += fr * shade.emission;     // reached by a bounce: lights the scene
      if (shade.emitter_only)
        break;

      // Russian Roulette

      if (depth < scene.max_ray_depth - 1)
      {
        if (gpu_coin_flip(&rng, 0.35f))
          break;
        fr = fr / 0.65f;
      }

      // Light at hit point, initial hit has fr of 1
      L_out += fr * bounce(scene, &rng, r, hit, bsdf_f(shade));

      // Change of Basis stuff
      Matrix3x3 o2w;
      gpu_make_coord_space(o2w, hit.n);

      Vector3D hit_p = r.o + r.d * hit.t;

      // Setup for next iter
      Vector3D wi;
      float pdf;
      Vector3D nextf = bsdf_sample_f(shade, &rng, &wi, &pdf);
      Vector3D wiw = o2w * wi;
      float theta = dot(hit.n, wiw);
      // pdf == 0 happens when curand_uniform returns exactly 1.0 (grazing
      // sample); 0 / 0 would put a NaN (black pixel) in the accumulator.
      // A direction below the actual surface would go through it.
      if (pdf <= 0.f || dot(hit.ng, wiw) <= 0.f)
        break;
      fr = fr *nextf *theta / pdf;
      r = make_gpu_ray(hit_p + EPS_F2 * hit.ng, wiw, 0.f, INF_F);
      depth--;
    }
    accum[id] += L_out;
    Vector3D sigma = s12[id]; // (s1, s2, # of itrs)
    float lum = L_out.illum();
    sigma += Vector3D(lum, lum * lum, 1);
    float n = sigma.z;
    if (((int)n) % scene.samples_per_batch == 0 && n > 1)
    {
      float s1 = sigma.x;
      float s2 = sigma.y;
      float s = (s2 - s1 * s1 / n) / (n - 1);
      s = fmaxf(s, 0.f);
      s = sqrtf(s);
      float mu = s1 / n;
      float I = 1.96f * (s / sqrt(n));
      // All samples zero so far: the CI test is 0 <= 0 and would stop a pixel
      // whose first batch was unlucky (thin geometry, partial coverage) at
      // black. Use the "rule of three" instead: after n zero samples the 95%
      // upper bound on the chance of a nonzero sample is 3/n, so stop once
      // that is within the tolerance. Truly black pixels still stop early.
      bool done = mu > 0.f ? I <= scene.maxTolerance * mu
                           : n * scene.maxTolerance >= 3.f;
      if (done)
      {
        converge[id] = true;
      }
    }
    s12[id] = sigma;
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
    if (buf.s12)
      CUDA_CALL(cudaFree(buf.s12));
    if (buf.converge)
      CUDA_CALL(cudaFree(buf.converge));
    buf.states = nullptr;
    buf.d_out = nullptr;
    buf.s12 = nullptr;
    buf.converge = nullptr;
    buf.w = buf.h = 0;
  }

  // Fills c_srgb_to_linear (once per process).
  static void init_srgb_lut()
  {
    static bool done = false;
    if (done)
      return;
    float lut[256];
    for (int i = 0; i < 256; i++)
    {
      float c = i / 255.f;
      lut[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    }
    CUDA_CALL(cudaMemcpyToSymbol(c_srgb_to_linear, lut, sizeof(lut)));
    done = true;
  }

  void render_init(GPURenderBuffers &buf, int w, int h)
  {
    if (w <= 0 || h <= 0)
      return;
    init_srgb_lut();
    // Already set up at this size: keep the RNG streams going, just reset
    // the accumulator for a fresh render
    size_t pixels = (size_t)w * h;
    if (buf.states && buf.d_out && buf.w == w && buf.h == h)
    {
      CUDA_CALL(cudaMemset(buf.d_out, 0, pixels * sizeof(Vector3D)));
      CUDA_CALL(cudaMemset(buf.s12, 0, pixels * sizeof(Vector3D)));
      CUDA_CALL(cudaMemset(buf.converge, 0, pixels * sizeof(bool)));
      return;
    }

    render_free(buf);
    CUDA_CALL(cudaMalloc(&buf.states, pixels * sizeof(curandStatePhilox4_32_10_t)));
    CUDA_CALL(cudaMalloc(&buf.d_out, pixels * sizeof(Vector3D)));
    CUDA_CALL(cudaMemset(buf.d_out, 0, pixels * sizeof(Vector3D)));
    CUDA_CALL(cudaMalloc(&buf.s12, pixels * sizeof(Vector3D)));
    CUDA_CALL(cudaMemset(buf.s12, 0, pixels * sizeof(Vector3D)));
    CUDA_CALL(cudaMalloc(&buf.converge, pixels * sizeof(bool)));
    CUDA_CALL(cudaMemset(buf.converge, 0, pixels * sizeof(bool)));
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
    render_kernel<<<grid_for(buf.w, buf.h), kBlock>>>(scene, cam, buf.d_out, buf.s12, buf.converge,
                                                      buf.w, buf.h, buf.states);
    CUDA_CALL(cudaGetLastError());
    CUDA_CALL(cudaDeviceSynchronize());
  }

  void render_readback(const GPURenderBuffers &buf, Vector3D *host_out)
  {
    if (!buf.d_out || !buf.s12)
      return;
    size_t pixels = (size_t)buf.w * buf.h;
    CUDA_CALL(cudaMemcpy(host_out, buf.d_out, pixels * sizeof(Vector3D),
                         cudaMemcpyDeviceToHost));
    // s12 lives on the device; copy it back to read per-pixel sample counts
    std::vector<Vector3D> s12(pixels);
    CUDA_CALL(cudaMemcpy(s12.data(), buf.s12, pixels * sizeof(Vector3D),
                         cudaMemcpyDeviceToHost));
    for (size_t i = 0; i < pixels; i++)
    {
      float n = s12[i].z; // number of samples actually taken
      host_out[i] = n > 0.f ? host_out[i] * (1.f / n) : Vector3D(0.f);
    }
  }

} // namespace CGL
