#ifndef CGL_COLLADA_LIGHTINFO_H
#define CGL_COLLADA_LIGHTINFO_H

#include "CGL/vector3D.h"

#include "collada_info.h"

namespace CGL { namespace Collada {

// For some reason enum classes aren't working for me here; not sure why.
namespace LightType {
  enum T {
    NONE,
    AMBIENT,
    DIRECTIONAL,
    AREA,
    POINT,
    SPOT
  };
}

class LightInfo : public Instance {
 public:
  
  LightInfo();

  LightType::T light_type;  ///< type 

  Vector3D spectrum;        ///< spectrum 

  Vector3D position;        ///< position
  Vector3D direction;       ///< direction
  Vector3D up;              ///< up

  float falloff_deg;        ///< degree of fall off angle
  float falloff_exp;        ///< fall out exponent

  float constant_att;       ///< constant attenuation factor
  float linear_att;         ///< linear attenuation factor
  float quadratic_att;      ///< quadratic attenuation factor

  // Point / spot extras (set by the glTF loader; COLLADA keeps the defaults,
  // i.e. the old behaviour of no distance falloff).
  bool inverse_square = false; ///< spectrum is intensity, falls off as 1/d^2
  float range = 0;             ///< > 0: fade to zero at this distance
  float spot_inner = 0;        ///< spot cone inner half-angle (radians)
  float spot_outer = 0.7853982f; ///< spot cone outer half-angle (radians)

  // Area light given directly in world space (glTF extras): position is the
  // center, direction the unit emission direction, dim_x / dim_y the edges.
  bool explicit_area = false;
  Vector3D dim_x, dim_y;

}; // struct Light

std::ostream& operator<<( std::ostream& os, const LightInfo& light );

} // namespace Collada
} // namespace CGL

#endif // CGL_COLLADA_LIGHTINFO_H
