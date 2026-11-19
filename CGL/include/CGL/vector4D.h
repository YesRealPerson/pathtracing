#ifndef CGL_VECTOR4D_H
#define CGL_VECTOR4D_H

#include "CGL.h"
#include "vector3D.h"

#include <ostream>
#include <cmath>

namespace CGL {

/**
 * Defines 4D standard vectors.
 */
class Vector4D {
public:

  // components
  union {
    struct {
      float x, y, z, w;
    };
    struct {
      float r, g, b, a;
    };
  };

  /**
   * Constructor.
   * Initializes tp vector (0,0,0, 0).
   */
  CGL_HD Vector4D() : x( 0.0f ), y( 0.0f ), z( 0.0f ), w( 0.0f ) { }

  /**
   * Constructor.
   * Initializes to vector (x,y,z,w).
   */
  CGL_HD Vector4D( float x, float y, float z, float w) : x( x ), y( y ), z( z ), w( w ) { }

  /**
   * Constructor.
   * Initializes to vector (x,y,z,0).
   */
  CGL_HD Vector4D( float x, float y, float z) : x( x ), y( y ), z( z ), w( 0.0f ) { }


  /**
   * Constructor.
   * Initializes to vector (c,c,c,c)
   */
  CGL_HD Vector4D(float c) : x(c), y(c), z(c), w(c) {}

  /**
   * Constructor.
   * Initializes from existing vector4D.
   */
  Vector4D( const Vector4D& v ) = default;
  Vector4D& operator=( const Vector4D& v ) = default;

  /**
   * Constructor.
   * Initializes from existing vector3D.
   */
  CGL_HD Vector4D( const Vector3D& v ) : x( v.x ), y( v.y ), z( v.z ), w( 0.0f ) { }


  /**
   * Constructor.
   * Initializes from existing vector3D and w value.
   */
  CGL_HD Vector4D( const Vector3D& v, float w ) : x( v.x ), y( v.y ), z( v.z ), w( w ) { }

  // returns reference to the specified component (0-based indexing: x, y, z)
  CGL_INLINE float& operator[] ( const int& index ) {
    return ( &x )[ index ];
  }

  // returns const reference to the specified component (0-based indexing: x, y, z)
  CGL_INLINE const float& operator[] ( const int& index ) const {
    return ( &x )[ index ];
  }

  // negation
  CGL_INLINE Vector4D operator-( void ) const {
    return Vector4D(-x, -y, -z, -w);
  }

  // addition
  CGL_INLINE Vector4D operator+( const Vector4D& v ) const {
    return Vector4D(x + v.x, y + v.y, z + v.z, w + v.w);
  }

  // subtraction
  CGL_INLINE Vector4D operator-( const Vector4D& v ) const {
    return Vector4D(x - v.x, y - v.y, z - v.z, w - v.w);
  }

  // right scalar multiplication
  CGL_INLINE Vector4D operator*( const float& c ) const {
    return Vector4D(x * c, y * c, z * c, w * c);
  }

  // scalar division
  CGL_INLINE Vector4D operator/( const float& c ) const {
    const float rc = 1.0f/c;
    return Vector4D(rc * x, rc * y, rc * z, rc * w);
  }

  // addition / assignment
  CGL_INLINE void operator+=( const Vector4D& v ) {
    x += v.x;
    y += v.y;
    z += v.z;
    w += v.w;
  }

  // subtraction / assignment
  CGL_INLINE void operator-=( const Vector4D& v ) {
    x -= v.x;
    y -= v.y;
    z -= v.z;
    w -= v.w;
  }

  // scalar multiplication / assignment
  CGL_INLINE void operator*=( const float& c ) {
    x *= c;
    y *= c;
    z *= c;
    w *= c;
  }

  // scalar division / assignment
  CGL_INLINE void operator/=( const float& c ) {
    (*this) *= (1.f / c);
  }

  /**
   * Returns per entry reciprocal
   */
  CGL_INLINE Vector4D rcp(void) const {
    return Vector4D(1.0f / x, 1.0f / y, 1.0f / z, 1.0f / w);
  }

  /**
   * Returns Euclidean distance metric extended to 4 dimensions.
   */
  CGL_INLINE float norm( void ) const {
    return sqrtf(norm2());
  }

  /**
   * Returns Euclidean length squared.
   */
  CGL_INLINE float norm2( void ) const {
    return x*x + y*y + z*z + w*w;
  }

  /**
   * Returns unit vector. (returns the normalized copy of this vector.)
   */
  CGL_INLINE Vector4D unit( void ) const {
    float rNorm = 1.f / sqrtf( x*x + y*y + z*z + w*w);
    return Vector4D( rNorm*x, rNorm*y, rNorm*z );
  }

  /**
   * Divides by Euclidean length.
   * This vector will be of unit length i.e. "normalized" afterwards.
   */
  CGL_INLINE void normalize( void ) {
    (*this) /= norm();
  }

  /**
   * Converts this vector to a 3D vector ignoring the w component.
   */
  CGL_INLINE Vector3D to3D() const {
    return Vector3D(x, y, z);
  }

  /**
   * Converts this vector to a 3D vector by dividing x, y, and z by w.
   */
  CGL_INLINE Vector3D projectTo3D() const {
    float invW = 1.0f / w;
    return Vector3D(x * invW, y * invW, z * invW);
  }

}; // class Vector4D

// left scalar multiplication
CGL_INLINE Vector4D operator* ( const float& c, const Vector4D& v ) {
  return Vector4D( c * v.x, c * v.y, c * v.z, c*v.w );
}

// dot product (a.k.a. inner or scalar product)
CGL_INLINE float dot( const Vector4D& u, const Vector4D& v ) {
  return u.x*v.x + u.y*v.y + u.z*v.z + u.w*v.w;
}

// prints components
std::ostream& operator<<( std::ostream& os, const Vector4D& v );

} // namespace CGL

#endif // CGL_VECTOR4D_H
