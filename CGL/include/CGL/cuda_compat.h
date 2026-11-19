#ifndef CGL_CUDA_COMPAT_H
#define CGL_CUDA_COMPAT_H

/*
 * Portability macros so CGL math types can be used from both host code and
 * CUDA device (GPU) code.
 *
 *   CGL_HD       - function callable on host and device
 *   CGL_HOST     - host-only function
 *   CGL_DEVICE   - device-only function
 *   CGL_INLINE   - forced inline, host + device
 *
 * Outside of nvcc these expand to plain C++ so the headers still compile with
 * the regular host compiler.
 */

#if defined(__CUDACC__)
  #define CGL_HD     __host__ __device__
  #define CGL_HOST   __host__
  #define CGL_DEVICE __device__
  #define CGL_INLINE __forceinline__ __host__ __device__
#else
  #define CGL_HD
  #define CGL_HOST
  #define CGL_DEVICE
  #define CGL_INLINE inline
#endif

#endif // CGL_CUDA_COMPAT_H
