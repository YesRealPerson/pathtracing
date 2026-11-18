#include "sphere.h"

#include <cmath>

#include "pathtracer/bsdf.h"
#include "util/sphere_drawing.h"

namespace CGL {
namespace SceneObjects {

bool Sphere::test(const Ray &r, double &t1, double &t2) const {

  // TODO (Part 1.4):
  // Implement ray - sphere intersection test.
  // Return true if there are intersections and writing the
  // smaller of the two intersection times in t1 and the larger in t2.
  double interior = dot(r.d, r.o-this->o);
  double nabla = interior * interior;
  double second = (r.o-this->o).norm();
  second *= second;
  second -= this->r2;
  nabla -= second;
  if(nabla < 0) {
    return false;
  }
  double a = -interior + sqrt(nabla);
  double b = -interior - sqrt(nabla);
  t1 = min(a,b);
  t2 = max(a,b);
  return true;
}

bool Sphere::has_intersection(const Ray &r) const {

  // TODO (Part 1.4):
  // Implement ray - sphere intersection.
  // Note that you might want to use the the Sphere::test helper here.
  double a,b;
  if(!this->test(r, a,b)) return false;
  if (a >= r.min_t && a <= r.max_t) return true;
  if (b >= r.min_t && b <= r.max_t) return true;

  return false;
}

bool Sphere::intersect(const Ray &r, Intersection *i) const {

  // TODO (Part 1.4):
  // Implement ray - sphere intersection.
  // Note again that you might want to use the the Sphere::test helper here.
  // When an intersection takes place, the Intersection data should be updated
  // correspondingly.
  double t0, t1;
  if (!this->test(r, t0, t1)) return false;
  i->t = t0; // Choose lower t
  // If lower t out of bounds
  if(t0 < r.min_t || t0 > r.max_t) {
    i->t = t1;
    // Ensure upper t in bounds
    if(t1 < r.min_t || t1 > r.max_t) return false;
  }
  // it is the normalized vector pointing from the sphere center to the intersection point.
  i->n = (r.o + i->t * r.d - this->o).unit();
  i->primitive = this;
  i->bsdf = this->get_bsdf();
  r.max_t = i->t; //  "you should update max_t accordingly" I'm dumb
  return true;
}

void Sphere::draw(const Color &c, float alpha) const {
  Misc::draw_sphere_opengl(o, r, c);
}

void Sphere::drawOutline(const Color &c, float alpha) const {
  // Misc::draw_sphere_opengl(o, r, c);
}

} // namespace SceneObjects
} // namespace CGL
