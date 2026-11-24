#ifndef CGL_COLLADA_TRIMESHINFO_H
#define CGL_COLLADA_TRIMESHINFO_H

#include <cstdint>
#include <vector>

#include "CGL/vector2D.h"
#include "CGL/vector3D.h"

#include "collada_info.h"
#include "material_info.h"

namespace CGL { namespace Collada {

/*
  A flat, indexed triangle mesh in object space.

  Unlike PolymeshInfo this does not go through the HalfedgeMesh used by
  MeshEdit, so it can hold non-manifold / inconsistently wound geometry (which
  most real-world scenes contain) and it avoids the half-edge memory overhead.
  Produced by the glTF loader; consumed by GLScene::TriMesh.

  The same TriMeshInfo may be referenced by several Nodes (glTF instancing);
  each Node supplies its own transform.
*/
struct TriMeshInfo : Instance {

  std::vector<Vector3D> positions;  ///< object-space vertex positions
  std::vector<Vector3D> normals;    ///< per-vertex normals (empty = compute)
  std::vector<Vector2D> uvs;        ///< TEXCOORD_0 per vertex (empty = none)
  std::vector<uint32_t> indices;    ///< 3 indices per triangle

  MaterialInfo* material = nullptr; ///< material (nullptr = default diffuse)

}; // struct TriMeshInfo

} // namespace Collada
} // namespace CGL

#endif // CGL_COLLADA_TRIMESHINFO_H
