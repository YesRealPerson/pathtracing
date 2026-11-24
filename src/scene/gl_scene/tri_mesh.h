#ifndef CGL_GLSCENE_TRIMESH_H
#define CGL_GLSCENE_TRIMESH_H

#include <memory>

#include "scene.h"

#include "scene/collada/trimesh_info.h"
#include "scene/object.h"

namespace CGL { namespace GLScene {

/**
 * A render-only triangle mesh (used for glTF scenes).
 *
 * GLScene::Mesh builds a HalfedgeMesh so it can be edited in MeshEdit, which
 * (a) exits on non-manifold or inconsistently wound input and (b) costs a lot
 * of memory per triangle. TriMesh bakes the node transform into one
 * world-space SceneObjects::MeshData, draws it with OpenGL vertex arrays, and
 * shares that same MeshData (no copy) with the static scene the path tracer
 * uploads. It cannot be selected or edited.
 */
class TriMesh : public SceneObject {
 public:
  TriMesh(const Collada::TriMeshInfo& info, const Matrix4x4& transform);

  void set_draw_styles(DrawStyle *defaultStyle, DrawStyle *hoveredStyle,
                       DrawStyle *selectedStyle) { }

  void render_in_opengl() const;

  void render_debugger_node();

  BBox get_bbox() { return bbox; }

  // Not selectable / editable.
  double test_selection(const Vector2D& p, const Matrix4x4& worldTo3DH,
                        double minW) {
    return -1;
  }
  void confirm_hover() { }
  void confirm_select() { }
  void invalidate_hover() { }
  void invalidate_selection() { }
  void get_selection_info(SelectionInfo *selectionInfo) { }
  void drag_selection(float dx, float dy, const Matrix4x4& worldTo3DH) { }
  MeshView *get_mesh_view() { return nullptr; }

  BSDF* get_bsdf() { return bsdf; }
  SceneObjects::SceneObject *get_static_object();

  size_t num_triangles() const { return data->num_triangles(); }

 private:
  std::shared_ptr<const SceneObjects::MeshData> data;  ///< world space
  BSDF* bsdf;
  BBox bbox;
};

} // namespace GLScene
} // namespace CGL

#endif // CGL_GLSCENE_TRIMESH_H
