#ifndef CGL_STATICSCENE_OBJECT_H
#define CGL_STATICSCENE_OBJECT_H

#include <cstdint>
#include <memory>
#include <vector>

#include "CGL/vector2D.h"
#include "util/halfEdgeMesh.h"
#include "scene.h"

namespace CGL { namespace SceneObjects {

/**
 * World-space indexed triangle data. Immutable once built and shared (via
 * shared_ptr) between the OpenGL preview object and every static Mesh made
 * from it, so a mesh's vertices exist once on the host no matter how often
 * the static scene is rebuilt.
 */
struct MeshData {
  std::vector<Vector3D> positions;
  std::vector<Vector3D> normals;    ///< same length as positions
  std::vector<Vector2D> uvs;        ///< same length as positions, or empty
  std::vector<uint32_t> indices;    ///< 3 per triangle

  size_t num_triangles() const { return indices.size() / 3; }
};

/**
 * A triangle mesh object.
 */
class Mesh : public SceneObject {
 public:

  /**
   * Constructor.
   * Construct a static mesh for rendering from halfedge mesh used in editing.
   * Note that this converts the input halfedge mesh into a collection of
   * world-space triangle primitives.
   */
  Mesh(const HalfedgeMesh& mesh, BSDF* bsdf);

  /**
   * Constructor.
   * Share already-built world-space triangle data (no copy). Used by
   * GLScene::TriMesh (glTF scenes), which skips the halfedge mesh entirely.
   */
  Mesh(std::shared_ptr<const MeshData> data, BSDF* bsdf);

  /**
   * Get all the primitives (Triangle) in the mesh.
   * Note that Triangle reference the mesh for the actual data.
   * Only the CPU BVH (visualizer / autofocus) uses this; the GPU upload reads
   * data() directly instead of allocating one Triangle per triangle.
   * \return all the primitives in the mesh
   */
  vector<Primitive*> get_primitives() const;

  /**
   * Get the BSDF of the surface material of the mesh.
   * \return BSDF of the surface material of the mesh
   */
  BSDF* get_bsdf() const;

  /** The mesh's triangle data (world space). */
  const MeshData& data() const { return *mesh_data; }

  const Vector3D *positions;  ///< == data().positions.data()
  const Vector3D *normals;    ///< == data().normals.data()

 private:

  BSDF* bsdf; ///< BSDF of surface material

  std::shared_ptr<const MeshData> mesh_data;

};

/**
 * A sphere object.
 */
class SphereObject : public SceneObject {
 public:

  /**
  * Constructor.
  * Construct a static sphere for rendering from given parameters
  */
  SphereObject(const Vector3D o, double r, BSDF* bsdf);

  /**
  * Get all the primitives (Sphere) in the sphere object.
  * Note that Sphere reference the sphere object for the actual data.
  * \return all the primitives in the sphere object
  */
  std::vector<Primitive*> get_primitives() const;

  /**
   * Get the BSDF of the surface material of the sphere.
   * \return BSDF of the surface material of the sphere
   */
  BSDF* get_bsdf() const;

  Vector3D o; ///< origin
  double r;   ///< radius

private:

  BSDF* bsdf; ///< BSDF of the sphere objects' surface material

}; // class SphereObject


} // namespace SceneObjects
} // namespace CGL

#endif // CGL_STATICSCENE_OBJECT_H
