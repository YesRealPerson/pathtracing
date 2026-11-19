#ifndef CGL_VECTOR3D_H
#define CGL_VECTOR3D_H

#include "CGL.h"
#include "color.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <ostream>
#include <new>


namespace CGL {

/**
 * Defines 3D vectors.
 */
class Vector3D {
public:

  // components
  union {
    struct {
      float x, y, z;
    };
    struct {
      float r, g, b;
    };
  };

  /**
   * Constructor.
   * Initializes tp vector (0,0,0).
   */
  CGL_HD Vector3D() : x(0.0f), y(0.0f), z(0.0f) { }

  /**
   * Constructor.
   * Initializes to vector (x,y,z).
   */
  CGL_HD Vector3D( float x, float y, float z) : x( x ), y( y ), z( z ) { }

  /**
   * Constructor.
   * Initializes to vector (c,c,c)
   */
  CGL_HD Vector3D( float c ) : x( c ), y( c ), z( c ) { }


  /**
   * Constructor.
   * Initializes from existing vector
   */
  Vector3D( const Vector3D& v ) = default;
  Vector3D& operator=( const Vector3D& v ) = default;

  // returns reference to the specified component (0-based indexing: x, y, z)
  CGL_INLINE float& operator[] ( const int& index ) {
    return ( &x )[ index ];
  }

  // returns const reference to the specified component (0-based indexing: x, y, z)
  CGL_INLINE const float& operator[] ( const int& index ) const {
    return ( &x )[ index ];
  }

  CGL_INLINE bool operator==( const Vector3D& v) const {
    return v.x == x && v.y == y && v.z == z;
  }

  // negation
  CGL_INLINE Vector3D operator-( void ) const {
    return Vector3D( -x, -y, -z );
  }

  // addition
  CGL_INLINE Vector3D operator+( const Vector3D& v ) const {
    return Vector3D(x + v.x, y + v.y, z + v.z);
  }

  // subtraction
  CGL_INLINE Vector3D operator-( const Vector3D& v ) const {
    return Vector3D( x - v.x, y - v.y, z - v.z );
  }

  // element wise multiplication
  CGL_INLINE Vector3D operator*(const Vector3D& v) const {
    return Vector3D(x * v.x, y * v.y, z * v.z);
  }
  
  // element wise division
  CGL_INLINE Vector3D operator/(const Vector3D& v) const {
    return Vector3D(x / v.x, y / v.y, z / v.z);
  }

  // right scalar multiplication
  CGL_INLINE Vector3D operator*( const float& c ) const {
    return Vector3D( x * c, y * c, z * c );
  }

  // scalar division
  CGL_INLINE Vector3D operator/( const float& c ) const {
    const float rc = 1.0f / c;
    return Vector3D( rc * x, rc * y, rc * z );
  }

  // addition / assignment
  CGL_INLINE void operator+=( const Vector3D& v ) {
    x += v.x; y += v.y; z += v.z;
  }

  // subtraction / assignment
  CGL_INLINE void operator-=( const Vector3D& v ) {
    x -= v.x; y -= v.y; z -= v.z;
  }

  // scalar multiplication / assignment
  CGL_INLINE void operator*=( const float& c ) {
    x *= c; y *= c; z *= c;
  }

  // scalar division / assignment
  CGL_INLINE void operator/=( const float& c ) {
    (*this) *= ( 1.f/c );
  }

  /**
   * Returns per entry reciprocal
   */
  CGL_INLINE Vector3D rcp(void) const {
    return Vector3D(1.0f / x, 1.0f / y, 1.0f / z);
  }

  /**
   * Returns Euclidean length.
   */
  CGL_INLINE float norm( void ) const {
    return sqrtf(x * x + y * y + z * z);
  }

  /**
   * Returns Euclidean length squared.
   */
  CGL_INLINE float norm2( void ) const {
    return x * x + y * y + z * z;
  }

  /**
   * Returns unit vector.
   */
  CGL_INLINE Vector3D unit( void ) const {
    float rNorm = 1.f / norm();
    return (*this) * rNorm;
  }

  /**
   * Divides by Euclidean length.
   */
  CGL_INLINE void normalize( void ) {
    (*this) /= norm();
  }

  // Color is a host-only type.
  inline Color toColor() const {
    return Color(r, g, b);
  }

  CGL_INLINE float illum() const {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
  }

  // Color is a host-only type.
  static Vector3D fromColor(const Color& c) {
    return Vector3D(c.r, c.g, c.b);
  }

}; // class Vector3D

// left scalar multiplication
CGL_INLINE Vector3D operator* ( const float& c, const Vector3D& v ) {
  return Vector3D( c * v.x, c * v.y, c * v.z );
}

// left scalar divide
CGL_INLINE Vector3D operator/(const float &c, const Vector3D &v) {
  return Vector3D(c / v.x, c / v.y, c / v.z);
}

// dot product (a.k.a. inner or scalar product)
CGL_INLINE float dot( const Vector3D& u, const Vector3D& v ) {
  return u.x * v.x + u.y * v.y + u.z * v.z;
}

// cross product
CGL_INLINE Vector3D cross( const Vector3D& u, const Vector3D& v ) {
  return Vector3D( u.y*v.z - u.z*v.y,
                   u.z*v.x - u.x*v.z,
                   u.x*v.y - u.y*v.x );
}

// prints components
std::ostream& operator<<( std::ostream& os, const Vector3D& v );

} // namespace CGL

#endif // CGL_VECTOR3D_H
