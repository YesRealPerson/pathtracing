#include "pathtracer.h"

#include "scene/light.h"
#include "scene/sphere.h"
#include "scene/triangle.h"


using namespace CGL::SceneObjects;

namespace CGL {

PathTracer::PathTracer() {
  gridSampler = new UniformGridSampler2D();
  hemisphereSampler = new UniformHemisphereSampler3D();

  tm_gamma = 2.2f;
  tm_level = 1.0f;
  tm_key = 0.18;
  tm_wht = 5.0f;
}

PathTracer::~PathTracer() {
  delete gridSampler;
  delete hemisphereSampler;
}

void PathTracer::set_frame_size(size_t width, size_t height) {
  sampleBuffer.resize(width, height);
  sampleCountBuffer.resize(width * height);
}

void PathTracer::clear() {
  bvh = NULL;
  scene = NULL;
  camera = NULL;
  sampleBuffer.clear();
  sampleCountBuffer.clear();
  sampleBuffer.resize(0, 0);
  sampleCountBuffer.resize(0, 0);
}

void PathTracer::write_to_framebuffer(ImageBuffer &framebuffer, size_t x0,
                                      size_t y0, size_t x1, size_t y1) {
  sampleBuffer.toColor(framebuffer, x0, y0, x1, y1);
}

Vector3D
PathTracer::estimate_direct_lighting_hemisphere(const Ray &r,
                                                const Intersection &isect) {
  // Estimate the lighting from this intersection coming directly from a light.
  // For this function, sample uniformly in a hemisphere.

  // Note: When comparing Cornel Box (CBxxx.dae) results to importance sampling, you may find the "glow" around the light source is gone.
  // This is totally fine: the area lights in importance sampling has directionality, however in hemisphere sampling we don't model this behaviour.

  // make a coordinate system for a hit point
  // with N aligned with the Z direction.
  Matrix3x3 o2w;
  make_coord_space(o2w, isect.n);
  Matrix3x3 w2o = o2w.T();

  // w_out points towards the source of the ray (e.g.,
  // toward the camera if this is a primary ray)
  const Vector3D hit_p = r.o + r.d * isect.t;
  const Vector3D w_out = w2o * (-r.d);

  // This is the same number of total samples as
  // estimate_direct_lighting_importance (outside of delta lights). We keep the
  // same number of samples for clarity of comparison.
  int num_samples = scene->lights.size() * ns_area_light;
  Vector3D L_out;

  // TODO (Part 3): Write your sampling loop here
  // TODO BEFORE YOU BEGIN
  // UPDATE `est_radiance_global_illumination` to return direct lighting instead of normal shading 
  
  /*
  for each sample from hemisphere and throw ray
  if ray hits a zero bounce do math
  add to running sum
  average
  */
  for (int i = 0; i < num_samples; i++) {
      Vector3D wi = this->hemisphereSampler->get_sample(); // object coords, if it were a normal it would be 0,0,1 DUMBASS
      Vector3D wiw = o2w * wi; // world coords
      double pdf = 1.0 / (2 * PI);
      Ray x = Ray(hit_p + EPS_F*wiw, wiw); // origin at hit point, random direction
      Intersection temp;
      // Try to intersect
      if (this->bvh->intersect(x, &temp)) {
          Vector3D L = temp.bsdf->get_emission();
          // both need to be in object coords
          Vector3D fr = isect.bsdf->f(w_out, wi);
          double theta = dot(isect.n, wiw);
          L_out += fr*L*theta/pdf;
      }
  }
  return L_out / num_samples;
}

Vector3D
PathTracer::estimate_direct_lighting_importance(const Ray &r,
                                                const Intersection &isect) {
  // Estimate the lighting from this intersection coming directly from a light.
  // To implement importance sampling, sample only from lights, not uniformly in
  // a hemisphere.

  // make a coordinate system for a hit point
  // with N aligned with the Z direction.
  Matrix3x3 o2w;
  make_coord_space(o2w, isect.n);
  Matrix3x3 w2o = o2w.T();

  // w_out points towards the source of the ray (e.g.,
  // toward the camera if this is a primary ray)
  const Vector3D hit_p = r.o + r.d * isect.t;
  const Vector3D w_out = w2o * (-r.d);
  Vector3D L_out;

  for (SceneLight *light : this->scene->lights) {
      Vector3D out;
      Vector3D wi;
      double dist = -1;
      double pdf;
      int n = this->ns_area_light;
      if (light->is_delta_light()) n = 1;
      for (int i = 0; i < n; i++) {
          Vector3D L = light->sample_L(hit_p, &wi, &dist, &pdf); // WORLD ~~SPACE~~
          //if (dist < 0) break; // behind us
          Ray x = Ray(hit_p + EPS_F * wi, wi, dist - EPS_F, 0); // origin at hit point, random direction
          Intersection temp;
          // Nothign blocking th elight
          if (!this->bvh->has_intersection(x)) {
              // both need to be in object coords
              Vector3D fr = isect.bsdf->f(w_out, w2o*wi);
              double theta = dot(isect.n, wi);
              if (theta < 0) continue; // light is behind the surface
              out += fr * L * theta / pdf;
          }
      }
      if (dist < 0) continue;
      L_out += out/n;
  }
  return L_out;

}

Vector3D PathTracer::zero_bounce_radiance(const Ray &r,
                                          const Intersection &isect) {
  // TODO: Part 3, Task 2
  // Returns the light that results from no bounces of light
    // non-emissive bsdfs will just return an empty vector or something
  return isect.bsdf->get_emission();


}

Vector3D PathTracer::one_bounce_radiance(const Ray &r,
                                         const Intersection &isect) {
  // TODO: Part 3, Task 3
  // Returns either the direct illumination by hemisphere or importance sampling
  // depending on `direct_hemisphere_sample`

    return direct_hemisphere_sample ? this->estimate_direct_lighting_hemisphere(r, isect)
                                    : this->estimate_direct_lighting_importance(r, isect);
    

}

Vector3D PathTracer::at_least_one_bounce_radiance(const Ray &r,
                                                  const Intersection &isect) {
  Matrix3x3 o2w;
  make_coord_space(o2w, isect.n);
  Matrix3x3 w2o = o2w.T();

  Vector3D hit_p = r.o + r.d * isect.t;
  Vector3D w_out = w2o * (-r.d);

  Vector3D L_out(0, 0, 0);

  // TODO: Part 4, Task 2
  // Returns the one bounce radiance + radiance from extra bounces at this point.
  // Should be called recursively to simulate extra bounces.

  if (r.depth == 0) return L_out;
  int depth = r.depth - 1;
  // russian roulette
  // at least one in direct bounce
  if (depth < this->max_ray_depth - 1 && coin_flip(0.35)) {
      return L_out;
  }
  // At last iter always collect bounce info
  // if accum bounces always collect
  if(isAccumBounces || r.depth == 1) L_out = this->one_bounce_radiance(r, isect);
  Vector3D wi;
  double pdf;
  Vector3D fr = isect.bsdf->sample_f(w_out, &wi, &pdf);
  Vector3D wiw = o2w * wi;
  Ray x = Ray(hit_p + EPS_F * wiw, wiw, INF_D, depth);
  Intersection temp;
  if (this->bvh->intersect(x, &temp)) {
      double theta = dot(isect.n, wiw);
      // L_out += fr*at_least_one_bounce_radiance(x, temp)*theta/pdf;
      L_out += fr * at_least_one_bounce_radiance(x, temp) * theta / pdf / 0.65;

  }
  return L_out;
}

Vector3D PathTracer::est_radiance_global_illumination(const Ray &r) {
  Intersection isect;
  Vector3D L_out;

  // You will extend this in assignment 3-2.
  // If no intersection occurs, we simply return black.
  // This changes if you implement hemispherical lighting for extra credit.
  // TODO (Part 3): Return the direct illumination.
  if (!bvh->intersect(r, &isect)) // if we don't intersect
      // Return the environment light or nothing if there is no environment light
      return envLight ? envLight->sample_dir(r) : L_out;
  switch (PART) {
  case 5:
  case 4:
      // Either we're accumulating all bounces or we're only sampling the first bounce
      if (this->max_ray_depth == 0 || isAccumBounces) L_out = this->zero_bounce_radiance(r, isect);
      L_out += this->at_least_one_bounce_radiance(r, isect);
      break;
  case 3:
      L_out = this->zero_bounce_radiance(r, isect) + this->one_bounce_radiance(r, isect);
      break;
  case 2:
  case 1:
      L_out = (isect.t == INF_D) ? debug_shading(r.d) : normal_shading(isect.n);
      break;
  }
  


  // TODO (Part 4): Accumulate the "direct" and "indirect"
  // parts of global illumination into L_out rather than just direct

  return L_out;
}

void PathTracer::raytrace_pixel(size_t x, size_t y) {
  // TODO (Part 1.2):
  // Make a loop that generates num_samples camera rays and traces them
  // through the scene. Return the average Vector3D.
  // You should call est_radiance_global_illumination in this function.

  // TODO (Part 5):
  // Modify your implementation to include adaptive sampling.
  // Use the command line parameters "samplesPerBatch" and "maxTolerance"
  int num_samples = ns_aa;          // total samples to evaluate
  Vector2D origin = Vector2D(x, y); // bottom left corner of the pixel
  Vector3D result;
  float s1 = 0.0f;
  float s2 = 0.0f;
  int n = num_samples;
  for (int i = 0; i < n; i++) {
        Vector2D random = origin+this->gridSampler->get_sample();
        random.x /= this->sampleBuffer.w;
        random.y /= this->sampleBuffer.h;
        Ray r = this->camera->generate_ray(random.x, random.y);
        r.depth = this->max_ray_depth;
        Vector3D out = this->est_radiance_global_illumination(r);
        result += out;
        float ill = out.illum();
        s1 += ill;
        s2 += ill*ill;

        if ((i+1) % this->samplesPerBatch == 0){
          // n = i+1
          float s = (s2-s1*s1/(i+1))/i;
          s = sqrt(s);
          float mu = s1/(i+1);
          float I = 1.96f * (s/sqrt(i+1));
          if(I <= this->maxTolerance*mu) {
            n = i+1;
            break;
          }
        }
  }
  result /= n;
  sampleBuffer.update_pixel(result, x, y);
  sampleCountBuffer[x + y * sampleBuffer.w] = n;
}

void PathTracer::autofocus(Vector2D loc) {
  Ray r = camera->generate_ray(loc.x / sampleBuffer.w, loc.y / sampleBuffer.h);
  Intersection isect;

  bvh->intersect(r, &isect);

  camera->focalDistance = isect.t;
}

} // namespace CGL
