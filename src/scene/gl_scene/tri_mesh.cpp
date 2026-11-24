#include "tri_mesh.h"

#include <cmath>

#include "CGL/matrix3x3.h"

#include "pathtracer/bsdf.h"
#include "application/visual_debugger.h"

namespace CGL { namespace GLScene {

// glVertexPointer / glNormalPointer read the arrays directly.
static_assert(sizeof(Vector3D) == 3 * sizeof(float),
              "Vector3D must be tightly packed floats for GL vertex arrays");

static Vector3D safe_unit(const Vector3D& v, const Vector3D& fallback) {
  double n = v.norm();
  return n > 1e-20 ? v / n : fallback;
}

TriMesh::TriMesh(const Collada::TriMeshInfo& info, const Matrix4x4& transform) {
  auto d = std::make_shared<SceneObjects::MeshData>();
  d->indices = info.indices;

  // Positions: object -> world.
  d->positions.resize(info.positions.size());
  for (size_t i = 0; i < d->positions.size(); i++) {
    d->positions[i] = (transform * Vector4D(info.positions[i], 1)).projectTo3D();
    bbox.expand(d->positions[i]);
  }

  if (info.normals.size() == info.positions.size()) {
    // Normals transform by the inverse transpose of the linear part, so
    // non-uniform scales don't skew them.
    Matrix3x3 M;
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++)
        M(r, c) = transform(r, c);
    Matrix3x3 N = std::fabs(M.det()) > 1e-20 ? M.inv().T() : M;

    d->normals.resize(info.normals.size());
    for (size_t i = 0; i < d->normals.size(); i++)
      d->normals[i] = safe_unit(N * info.normals[i], Vector3D(0, 1, 0));
  } else {
    // No normals in the file: area-weighted smooth vertex normals (the
    // un-normalized cross product is proportional to triangle area).
    const std::vector<Vector3D>& P = d->positions;
    d->normals.assign(P.size(), Vector3D(0, 0, 0));
    for (size_t t = 0; t + 2 < d->indices.size(); t += 3) {
      uint32_t a = d->indices[t], b = d->indices[t + 1], c = d->indices[t + 2];
      Vector3D fn = cross(P[b] - P[a], P[c] - P[a]);
      d->normals[a] += fn;
      d->normals[b] += fn;
      d->normals[c] += fn;
    }
    for (Vector3D& n : d->normals) n = safe_unit(n, Vector3D(0, 1, 0));
  }

  // UVs are not affected by the node transform.
  if (info.uvs.size() == info.positions.size()) d->uvs = info.uvs;

  data = std::move(d);
  bsdf = (info.material && info.material->bsdf)
             ? info.material->bsdf
             : new DiffuseBSDF(Vector3D(0.5f, 0.5f, 0.5f));
}

void TriMesh::render_in_opengl() const {
  if (data->indices.empty()) return;

  // These are client-side arrays, so no buffer object may be bound: with a VBO
  // bound, GL treats the pointers below as byte offsets into it. CGL's
  // OSDText leaves its VBO bound to GL_ARRAY_BUFFER after drawing text.
  GLint prev_array = 0, prev_elements = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array);
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prev_elements);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  glEnable(GL_LIGHTING);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_NORMAL_ARRAY);
  glVertexPointer(3, GL_FLOAT, sizeof(Vector3D), data->positions.data());
  glNormalPointer(GL_FLOAT, sizeof(Vector3D), data->normals.data());
  glDrawElements(GL_TRIANGLES, (GLsizei)data->indices.size(), GL_UNSIGNED_INT,
                 data->indices.data());
  glDisableClientState(GL_NORMAL_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);

  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev_array);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prev_elements);
}

void TriMesh::render_debugger_node() {
  if (ImGui::TreeNode(this, "TriMesh %p (%zu tris)", (void*)this, num_triangles())) {
    if (bsdf) bsdf->render_debugger_node();
    ImGui::TreePop();
  }
}

SceneObjects::SceneObject *TriMesh::get_static_object() {
  // Shares `data`; no per-render copy of the vertices.
  return new SceneObjects::Mesh(data, bsdf);
}

} // namespace GLScene
} // namespace CGL
