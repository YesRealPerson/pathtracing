#include "vector4D.h"

namespace CGL {

  std::ostream& operator<<( std::ostream& os, const Vector4D& v ) {
    os << "(" << v.x << "," << v.y << "," << v.z << "," << v.w << ")";
    return os;
  }

} // namespace CGL
