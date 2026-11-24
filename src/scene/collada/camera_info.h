#ifndef CGL_COLLADA_CAMERAINFO_H
#define CGL_COLLADA_CAMERAINFO_H

#include "collada_info.h"

namespace CGL { namespace Collada {

/*
  Note that hFov_ and vFov_ are expected to be in DEGREES.
*/
struct CameraInfo : public Instance {

	Vector3D view_dir;
	Vector3D up_dir;

  float hFov, vFov, nClip, fClip;

  // Optional exact world-space pose. The COLLADA path never sets this and keeps
  // the old "orbit the scene bbox" placement. The glTF loader sets it so the
  // render starts from the camera saved in the file (important for large
  // scenes where the bbox orbit would put the camera outside the building).
  bool has_world_pose = false;
  Vector3D world_position;
  Vector3D world_view_dir;  ///< unit vector the camera looks along
};

std::ostream& operator<<(std::ostream& os, const CameraInfo& camera);

} // namespace Collada
} // namespace CGL

#endif // CGL_COLLADA_CAMERAINFO_H
