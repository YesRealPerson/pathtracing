#include "bbox.h"

#include "GL/glew.h"

#include <algorithm>
#include <iostream>

namespace CGL {

bool BBox::intersect(const Ray& r, double& t0, double& t1) const {

  // TODO (Part 2.2):
  // Implement ray - bounding box intersection test
  // If the ray intersected the bouding box within the range given by
  // t0, t1, update t0 and t1 with the new intersection times.

  auto o = r.o;// origin
  auto d = r.d; //directoin 

  //x axis
  auto x1 = min.x;
  auto x2 = max.x;
  auto tx1 = (x1-o.x) / d.x;
  auto tx2 = (x2-o.x) / d.x;
  double tminx = std::min(tx1,tx2);
  double tmaxx = std::max(tx1,tx2);

  //y axis
  auto y1 = min.y;
  auto y2 = max.y;
  auto ty1 = (y1-o.y) / d.y;
  auto ty2 = (y2-o.y) / d.y;
  double tminy = std::min(ty1,ty2);
  double tmaxy = std::max(ty1,ty2);


  //z axis
  auto z1 = min.z;
  auto z2 = max.z;
  auto tz1 = (z1-o.z) / d.z;
  auto tz2 = (z2-o.z) / d.z;
  double tminz = std::min(tz1,tz2);
  double tmaxz = std::max(tz1,tz2);

  auto enter = std::max({tminx, tminy, tminz, t0});
  auto exit = std::min({tmaxx, tmaxy, tmaxz, t1});
  if(enter > exit) {
    return false;
  }
  t0 = enter;
  t1 = exit;
  return true;





  return true;

}

void BBox::draw(Color c, float alpha) const {

  glColor4f(c.r, c.g, c.b, alpha);

  // top
  glBegin(GL_LINE_STRIP);
  glVertex3d(max.x, max.y, max.z);
  glVertex3d(max.x, max.y, min.z);
  glVertex3d(min.x, max.y, min.z);
  glVertex3d(min.x, max.y, max.z);
  glVertex3d(max.x, max.y, max.z);
  glEnd();

  // bottom
  glBegin(GL_LINE_STRIP);
  glVertex3d(min.x, min.y, min.z);
  glVertex3d(min.x, min.y, max.z);
  glVertex3d(max.x, min.y, max.z);
  glVertex3d(max.x, min.y, min.z);
  glVertex3d(min.x, min.y, min.z);
  glEnd();

  // side
  glBegin(GL_LINES);
  glVertex3d(max.x, max.y, max.z);
  glVertex3d(max.x, min.y, max.z);
  glVertex3d(max.x, max.y, min.z);
  glVertex3d(max.x, min.y, min.z);
  glVertex3d(min.x, max.y, min.z);
  glVertex3d(min.x, min.y, min.z);
  glVertex3d(min.x, max.y, max.z);
  glVertex3d(min.x, min.y, max.z);
  glEnd();

}

std::ostream& operator<<(std::ostream& os, const BBox& b) {
  return os << "BBOX(" << b.min << ", " << b.max << ")";
}

} // namespace CGL
