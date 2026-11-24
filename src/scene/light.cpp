#include "light.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "pathtracer/sampler.h"

namespace CGL { namespace SceneObjects {

// Directional Light //

DirectionalLight::DirectionalLight(const Vector3D rad,
                                   const Vector3D lightDir)
    : radiance(rad) {
  dirToLight = -lightDir.unit();
}

Vector3D DirectionalLight::sample_L(const Vector3D p, Vector3D* wi,
                                    double* distToLight, double* pdf) const {
  *wi = dirToLight;
  *distToLight = INF_D;
  *pdf = 1.0;
  return radiance;
}

void DirectionalLight::createGPU(GPULight_t* light) const {
  *light = GPULight_t{};
  light->type = GPU_LIGHT_DIRECTIONAL;
  light->radiance = this->radiance;
  light->direction = this->dirToLight;
}

// Infinite Hemisphere Light //

InfiniteHemisphereLight::InfiniteHemisphereLight(const Vector3D rad)
    : radiance(rad) {
  sampleToWorld[0] = Vector3D(1,  0,  0);
  sampleToWorld[1] = Vector3D(0,  0, -1);
  sampleToWorld[2] = Vector3D(0,  1,  0);
}

Vector3D InfiniteHemisphereLight::sample_L(const Vector3D p, Vector3D* wi,
                                           double* distToLight,
                                           double* pdf) const {
  Vector3D dir = sampler.get_sample();
  *wi = sampleToWorld* dir;
  *distToLight = INF_D;
  *pdf = 1.0 / (2.0 * PI);
  return radiance;
}

void InfiniteHemisphereLight::createGPU(GPULight_t* light) const {
  *light = GPULight_t{};
  light->type = GPU_LIGHT_HEMISPHERE;
  light->radiance = this->radiance;
  light->dim_x = this->sampleToWorld[0];
  light->dim_y = this->sampleToWorld[1];
  light->direction = this->sampleToWorld[2];
}

// Point Light //

// 1/d^2 with glTF's range window; 1 when inverse_square is off.
static double punctual_attenuation(bool inverse_square, float range, double d2) {
  if (!inverse_square) return 1.0;
  double att = 1.0 / std::max(d2, 1e-8);
  if (range > 0) {
    double q = d2 / ((double)range * range);
    double w = std::min(std::max(1.0 - q * q, 0.0), 1.0);
    att *= w * w;
  }
  return att;
}

PointLight::PointLight(const Vector3D rad, const Vector3D pos,
                       bool inverse_square, float range) :
  radiance(rad), position(pos), inverse_square(inverse_square), range(range) { }

Vector3D PointLight::sample_L(const Vector3D p, Vector3D* wi,
                             double* distToLight,
                             double* pdf) const {
  Vector3D d = position - p;
  *wi = d.unit();
  *distToLight = d.norm();
  *pdf = 1.0;
  return radiance * punctual_attenuation(inverse_square, range, d.norm2());
}

void PointLight::createGPU(GPULight_t* light) const {
  *light = GPULight_t{};
  light->type = GPU_LIGHT_POINT;
  light->radiance = this->radiance;
  light->position = this->position;
  light->inv_square = this->inverse_square ? 1 : 0;
  light->range = this->range;
}


// Spot Light //

SpotLight::SpotLight(const Vector3D rad, const Vector3D pos, const Vector3D dir,
                     float inner, float outer, bool inverse_square, float range)
  : radiance(rad), position(pos), direction(dir.unit()),
    cos_inner(cosf(inner)), cos_outer(cosf(outer)),
    inverse_square(inverse_square), range(range) { }

Vector3D SpotLight::sample_L(const Vector3D p, Vector3D* wi,
                             double* distToLight, double* pdf) const {
  Vector3D d = position - p;
  *wi = d.unit();
  *distToLight = d.norm();
  *pdf = 1.0;
  double cd = -dot(direction, *wi);
  double t = (cd - cos_outer) / std::max(cos_inner - cos_outer, 1e-4f);
  t = std::min(std::max(t, 0.0), 1.0);
  return radiance * (t * t * punctual_attenuation(inverse_square, range, d.norm2()));
}

void SpotLight::createGPU(GPULight_t* light) const {
  *light = GPULight_t{};
  light->type = GPU_LIGHT_SPOT;
  light->radiance = this->radiance;
  light->position = this->position;
  light->direction = this->direction;
  light->cos_inner = this->cos_inner;
  light->cos_outer = this->cos_outer;
  light->inv_square = this->inverse_square ? 1 : 0;
  light->range = this->range;
}


// Area Light //

AreaLight::AreaLight(const Vector3D rad, 
                     const Vector3D pos,   const Vector3D dir, 
                     const Vector3D dim_x, const Vector3D dim_y)
  : radiance(rad), position(pos), direction(dir),
    dim_x(dim_x), dim_y(dim_y), area(dim_x.norm() * dim_y.norm()) { }

Vector3D AreaLight::sample_L(const Vector3D p, Vector3D* wi, 
                             double* distToLight, double* pdf) const {

  Vector2D sample = sampler.get_sample() - Vector2D(0.5f, 0.5f);
  Vector3D d = position + sample.x * dim_x + sample.y * dim_y - p;
  double cosTheta = dot(d, direction);
  double sqDist = d.norm2();
  double dist = sqrt(sqDist);
  *wi = d / dist;
  *distToLight = dist;
  *pdf = sqDist / (area * fabs(cosTheta));
  return cosTheta < 0 ? radiance : Vector3D();
};

void AreaLight::createGPU(GPULight_t* light) const {
  *light = GPULight_t{};
  light->type = GPU_LIGHT_AREA;
  light->radiance = this->radiance;
  light->position = this->position;
  light->direction = this->direction;
  light->dim_x = this->dim_x;
  light->dim_y = this->dim_y;
  light->area = (float)this->area;
}


// Sphere Light //

SphereLight::SphereLight(const Vector3D rad, const SphereObject* sphere) {

}

Vector3D SphereLight::sample_L(const Vector3D p, Vector3D* wi, 
                               double* distToLight, double* pdf) const {

  return Vector3D();
}

void SphereLight::createGPU(GPULight_t* light) const {
  *light = GPULight_t{};
  light->type = GPU_LIGHT_UNKNOWN;
}

// Mesh Light

MeshLight::MeshLight(const Vector3D rad, const Mesh* mesh) {

}

Vector3D MeshLight::sample_L(const Vector3D p, Vector3D* wi, 
                             double* distToLight, double* pdf) const {
  return Vector3D();
}

void MeshLight::createGPU(GPULight_t* light) const {
  *light = GPULight_t{};
  light->type = GPU_LIGHT_UNKNOWN;
}

} // namespace SceneObjects
} // namespace CGL
