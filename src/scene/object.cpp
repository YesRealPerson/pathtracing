#include "object.h"
#include "sphere.h"
#include "triangle.h"

#include <vector>
#include <iostream>
#include <unordered_map>

using std::vector;
using std::unordered_map;

namespace CGL { namespace SceneObjects {

// Mesh object //

Mesh::Mesh(const HalfedgeMesh& mesh, BSDF* bsdf) {

  unordered_map<const Vertex *, int> vertexLabels;
  auto data = std::make_shared<MeshData>();

  int vertexI = 0;
  for (VertexCIter it = mesh.verticesBegin(); it != mesh.verticesEnd(); it++) {
    const Vertex *v = &*it;
    vertexLabels[v] = vertexI++;
    data->positions.push_back(v->position);
    data->normals.push_back(v->normal);
  }

  for (FaceCIter f = mesh.facesBegin(); f != mesh.facesEnd(); f++) {
    HalfedgeCIter h = f->halfedge();
    data->indices.push_back(vertexLabels[&*h->vertex()]);
    data->indices.push_back(vertexLabels[&*h->next()->vertex()]);
    data->indices.push_back(vertexLabels[&*h->next()->next()->vertex()]);
  }

  mesh_data = std::move(data);
  positions = mesh_data->positions.data();
  normals   = mesh_data->normals.data();
  this->bsdf = bsdf;

}

Mesh::Mesh(std::shared_ptr<const MeshData> data, BSDF* bsdf)
    : bsdf(bsdf), mesh_data(std::move(data)) {
  positions = mesh_data->positions.data();
  normals   = mesh_data->normals.data();
}

vector<Primitive*> Mesh::get_primitives() const {

  const vector<uint32_t>& indices = mesh_data->indices;
  vector<Primitive*> primitives;
  size_t num_triangles = indices.size() / 3;
  primitives.reserve(num_triangles);
  for (size_t i = 0; i < num_triangles; ++i) {
    Triangle* tri = new Triangle(this, indices[i * 3],
                                       indices[i * 3 + 1],
                                       indices[i * 3 + 2]);
    primitives.push_back(tri);
  }
  return primitives;
}

BSDF* Mesh::get_bsdf() const {
  return bsdf;
}

// Sphere object //

SphereObject::SphereObject(const Vector3D o, double r, BSDF* bsdf) {

  this->o = o;
  this->r = r;
  this->bsdf = bsdf;
  
}

std::vector<Primitive*> SphereObject::get_primitives() const {
  std::vector<Primitive*> primitives;
  primitives.push_back(new Sphere(this,o,r));
  return primitives;
}

BSDF* SphereObject::get_bsdf() const {
  return bsdf;
}


} // namespace SceneObjects
} // namespace CGL
