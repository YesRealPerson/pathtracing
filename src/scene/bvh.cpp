#include "bvh.h"

#include "CGL/CGL.h"
#include "triangle.h"

#include <iostream>
#include <stack>
#include <algorithm>

using namespace std;

namespace CGL {
namespace SceneObjects {

BVHAccel::BVHAccel(const std::vector<Primitive *> &_primitives,
                   size_t max_leaf_size) {

  primitives = std::vector<Primitive *>(_primitives);
  root = construct_bvh(primitives.begin(), primitives.end(), max_leaf_size);
}

BVHAccel::~BVHAccel() {
  if (root)
    delete root;
  primitives.clear();
}

BBox BVHAccel::get_bbox() const { return root->bb; }

void BVHAccel::draw(BVHNode *node, const Color &c, float alpha) const {
  if (node->isLeaf()) {
    for (auto p = node->start; p != node->end; p++) {
      (*p)->draw(c, alpha);
    }
  } else {
    draw(node->l, c, alpha);
    draw(node->r, c, alpha);
  }
}

void BVHAccel::drawOutline(BVHNode *node, const Color &c, float alpha) const {
  if (node->isLeaf()) {
    for (auto p = node->start; p != node->end; p++) {
      (*p)->drawOutline(c, alpha);
    }
  } else {
    drawOutline(node->l, c, alpha);
    drawOutline(node->r, c, alpha);
  }
}

BVHNode *BVHAccel::construct_bvh(std::vector<Primitive *>::iterator start,
                                 std::vector<Primitive *>::iterator end,
                                 size_t max_leaf_size) {

  // TODO (Part 2.1):
  // Construct a BVH from the given vector of primitives and maximum leaf
  // size configuration. The starter code build a BVH aggregate with a
  // single leaf node (which is also the root) that encloses all the
  // primitives.


  BBox bbox;
  int primcount = 0;
  for (auto p = start; p != end; p++) {
    BBox bb = (*p)->get_bbox();
    bbox.expand(bb);
    primcount++;
  }

  if(primcount <= max_leaf_size) {
    BVHNode *node = new BVHNode(bbox);
    node->start = start;
    node->end = end;

    return node;
  }
  BBox cbox;
  for (auto p = start; p != end; p++) {
    cbox.expand((*p)->get_bbox().centroid());
  }
  float e[3] = {cbox.extent.x, cbox.extent.y, cbox.extent.z};
  int axis = std::max_element(e, e + 3) - e;
  float split = (cbox.min[axis] + cbox.max[axis]) / 2;

  //define a left and right vector of primitives
  std::vector<Primitive*> left, right;
  //after defining a split point I sort the primatives into these 2 vectors
  for (auto p = start; p != end; p++) {

    if ((*p)->get_bbox().centroid()[axis] < split) {
      left.push_back(*p);
    } else {
      right.push_back(*p);
    }
  }
  // then recursively call construct_bvh

  BVHNode *node = new BVHNode(bbox);
  if (left.empty() || right.empty()) {
    left.assign(start, end);
    std::sort(left.begin(), left.end(), [&](Primitive *a, Primitive *b) {
      return a->get_bbox().centroid()[axis] < b->get_bbox().centroid()[axis];
    });
    right.assign(left.begin() + left.size() / 2, left.end());
    left.resize(left.size() / 2);
  }
  auto temp = std::copy(left.begin(), left.end(), start);
  std::copy(right.begin(), right.end(), temp);
  auto mid = start + left.size();
  node->l = construct_bvh(start, mid, max_leaf_size);
  node->r = construct_bvh(mid, end, max_leaf_size);
  return node;





}

bool BVHAccel::has_intersection(const Ray &ray, BVHNode *node) const {
  // TODO (Part 2.3):
  // Fill in the intersect function.
  // Take note that this function has a short-circuit that the
  // Intersection version cannot, since it returns as soon as it finds
  // a hit, it doesn't actually have to find the closest hit.

  float tmin = ray.min_t;
  float tmax = ray.max_t;
  if (!node->bb.intersect(ray, tmin, tmax)) {
    return false;
  }
  if (node->isLeaf()) {

    for (auto p = node->start; p != node->end; p++) {
      total_isects++;
      if((*p)->has_intersection(ray)) {

        return true;
      }

    }
    return false;
  }
  return has_intersection(ray, node->l) || has_intersection(ray, node->r);
}

bool BVHAccel::intersect(const Ray &ray, Intersection *i, BVHNode *node) const {
  // TODO (Part 2.3):
  // Fill in the intersect function.


  float tmin = ray.min_t;
  floate tmax = ray.max_t;
  if (!node->bb.intersect(ray, tmin, tmax)) {
    return false;
  }
  bool does_intr = false;
  if (node->isLeaf()) {

    for (auto p = node->start; p != node->end; p++) {
      total_isects++;
      if((*p)->intersect(ray,i)) {
        
        does_intr = true;
      }

    }
    return does_intr;
  }
  bool l = intersect(ray, i,node->l) ;
  bool r = intersect(ray,i, node->r);
  return  l || r;
}

} // namespace SceneObjects
} // namespace CGL
