#ifndef CGL_GLTF_LOADER_H
#define CGL_GLTF_LOADER_H

#include <string>

#include "scene/collada/collada_info.h"

namespace CGL { namespace GLTF {

/**
 * True if the path ends in .glb or .gltf (case-insensitive).
 */
bool is_gltf_path(const std::string& path);

/**
 * Load a glTF 2.0 scene (.glb, or .gltf + .bin) into the same SceneInfo the
 * COLLADA parser produces, so Application::load() works unchanged.
 *
 *   meshes     -> TRIMESH nodes (one per mesh primitive, world transform in
 *                 Node::transform; instanced meshes share one TriMeshInfo)
 *   materials  -> MaterialInfo with a BSDF picked from the PBR parameters
 *   cameras    -> one CAMERA node (first perspective camera) with an exact pose
 *   lights     -> LIGHT nodes from KHR_lights_punctual (baked to world space)
 *
 * Returns 0 on success, -1 on failure (a message is printed).
 */
int load(const char* filename, Collada::SceneInfo* sceneInfo);

/**
 * Free the Instance objects (and their MaterialInfos) that load() allocated.
 * BSDFs are NOT freed: the GL/static scenes keep pointers to them.
 * Call after Application::load(), before deleting sceneInfo.
 */
void release(Collada::SceneInfo* sceneInfo);

} // namespace GLTF
} // namespace CGL

#endif // CGL_GLTF_LOADER_H
