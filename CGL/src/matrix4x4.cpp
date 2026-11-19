#include "matrix4x4.h"

#include <iostream>

namespace CGL {

// All other Matrix4x4 members are defined inline (host + device) in
// matrix4x4.h.

std::ostream &operator<<(std::ostream &os, const Matrix4x4 &A) {
  for (int i = 0; i < 4; i++) {
    os << "[ ";

    for (int j = 0; j < 4; j++) {
      os << A(i, j) << " ";
    }

    os << "]" << std::endl;
  }

  return os;
}

} // namespace CGL
