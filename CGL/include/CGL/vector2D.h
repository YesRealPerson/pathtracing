#ifndef CGL_VECTOR2D_H
#define CGL_VECTOR2D_H

#include "CGL.h"

#include <ostream>
#include <cmath>

namespace CGL {

/**
 * Defines 2D vectors.
 */
class Vector2D {
 public:

  // components
  float x, y;

  /**
   * Constructor.
   * Initializes to vector (0,0).
   */
  CGL_HD Vector2D() : x( 0.0f ), y( 0.0f ) { }

  /**
   * Constructor.
   * Initializes to vector (a,b).
   */
  CGL_HD Vector2D( float x, float y ) : x( x ), y( y ) { }

  /**
   * Constructor.
   * Copy constructor. Creates a copy of the given vector.
   */
  Vector2D( const Vector2D& v ) = default;
  Vector2D& operator=( const Vector2D& v ) = default;

  // returns reference to the specified component (0-based indexing: x, y)
  CGL_INLINE float& operator[] ( const int& index ) {
    return ( &x )[ index ];
  }

  // returns const reference to the specified component (0-based indexing: x, y)
  CGL_INLINE const float& operator[] ( const int& index ) const {
    return ( &x )[ index ];
  }

  // additive inverse
  CGL_INLINE Vector2D operator-( void ) const {
    return Vector2D( -x, -y );
  }

  // addition
  CGL_INLINE Vector2D operator+( const Vector2D& v ) const {
    Vector2D u = *this;
    u += v;
    return u;
  }

  // subtraction
  CGL_INLINE Vector2D operator-( const Vector2D& v ) const {
    Vector2D u = *this;
    u -= v;
    return u;
  }

  // right scalar multiplication
  CGL_INLINE Vector2D operator*( float r ) const {
    Vector2D vr = *this;
    vr *= r;
    return vr;
  }

  // scalar division
  CGL_INLINE Vector2D operator/( float r ) const {
    Vector2D vr = *this;
    vr /= r;
    return vr;
  }

  // add v
  CGL_INLINE void operator+=( const Vector2D& v ) {
    x += v.x;
    y += v.y;
  }

  // subtract v
  CGL_INLINE void operator-=( const Vector2D& v ) {
    x -= v.x;
    y -= v.y;
  }

  // scalar multiply by r
  CGL_INLINE void operator*=( float r ) {
    x *= r;
    y *= r;
  }

  // scalar divide by r
  CGL_INLINE void operator/=( float r ) {
    x /= r;
    y /= r;
  }

  /**
   * Returns norm.
   */
  CGL_INLINE float norm( void ) const {
    return sqrtf( x*x + y*y );
  }

  /**
   * Returns norm squared.
   */
  CGL_INLINE float norm2( void ) const {
    return x*x + y*y;
  }

  /**
   * Returns unit vector parallel to this one.
   */
  CGL_INLINE Vector2D unit( void ) const {
    return *this / this->norm();
  }


}; // clasd Vector2D

// left scalar multiplication
CGL_INLINE Vector2D operator*( float r, const Vector2D& v ) {
   return v*r;
}

// inner product
CGL_INLINE float dot( const Vector2D& v1, const Vector2D& v2 ) {
  return v1.x*v2.x + v1.y*v2.y;
}

// cross product
CGL_INLINE float cross( const Vector2D& v1, const Vector2D& v2 ) {
  return v1.x*v2.y - v1.y*v2.x;
}

// prints components
std::ostream& operator<<( std::ostream& os, const Vector2D& v );

} // namespace CGL

#endif // CGL_VECTOR2D_H
