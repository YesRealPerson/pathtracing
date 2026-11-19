#include "matrix3x3.h"

#include <iostream>

namespace CGL {

  // All other Matrix3x3 members are defined inline (host + device) in
  // matrix3x3.h.

  std::ostream& operator<<( std::ostream& os, const Matrix3x3& A ) {
    for( int i = 0; i < 3; i++ )
    {
       os << "[ ";

       for( int j = 0; j < 3; j++ )
       {
          os << A(i,j) << " ";
       }

       os << "]" << std::endl;
    }

    return os;
  }

} // namespace CGL
