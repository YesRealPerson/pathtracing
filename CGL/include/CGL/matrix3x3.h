#ifndef CGL_MATRIX3X3_H
#define CGL_MATRIX3X3_H

#include "CGL.h"
#include "vector3D.h"

#include <iosfwd>

namespace CGL {

/**
 * Defines a 3x3 matrix.
 * 3x3 matrices are extremely useful in computer graphics.
 *
 * All members are defined inline in this header and marked host/device so the
 * class can be used from both CPU code and CUDA kernels.
 */
class Matrix3x3 {

  public:

  // The default constructor. Returns identity.
  CGL_HD Matrix3x3(void) {
    for( int i = 0; i < 3; i++ ) {
      for( int j = 0; j < 3; j++ ) {
        (*this)(i,j) = (i==j) ? 1.f : 0.f;
      }
    }
  }

  // Constructor for row major form data.
  // Transposes to the internal column major form.
  // REQUIRES: data should be of size 9 for a 3 by 3 matrix..
  CGL_HD Matrix3x3(const float * data)
  {
    for( int i = 0; i < 3; i++ ) {
      for( int j = 0; j < 3; j++ ) {
	      // Transpostion happens within the () query.
	      (*this)(i,j) = data[i*3 + j];
      }
    }
  }

  CGL_HD Matrix3x3(float m00, float m01, float m02,
                   float m10, float m11, float m12,
                   float m20, float m21, float m22)
  {
    (*this)(0,0) = m00; (*this)(0,1) = m01; (*this)(0,2) = m02;
    (*this)(1,0) = m10; (*this)(1,1) = m11; (*this)(1,2) = m12;
    (*this)(2,0) = m20; (*this)(2,1) = m21; (*this)(2,2) = m22;
  }

  Matrix3x3( const Matrix3x3& B ) = default;
  Matrix3x3& operator=( const Matrix3x3& B ) = default;

  /**
   * Sets all elements to val.
   */
  CGL_INLINE void zero(float val = 0.0f ) {
    entries[0] = entries[1] = entries[2] = Vector3D( val, val, val );
  }

  /**
   * Returns the determinant of A.
   */
  CGL_INLINE float det( void ) const {
    const Matrix3x3& A( *this );

    return -A(0,2)*A(1,1)*A(2,0) + A(0,1)*A(1,2)*A(2,0) +
            A(0,2)*A(1,0)*A(2,1) - A(0,0)*A(1,2)*A(2,1) -
            A(0,1)*A(1,0)*A(2,2) + A(0,0)*A(1,1)*A(2,2) ;
  }

  /**
   * Returns the Frobenius norm of A.
   */
  CGL_INLINE float norm( void ) const {
    return sqrtf( entries[0].norm2() +
                 entries[1].norm2() +
                 entries[2].norm2() );
  }

  /**
   * Returns the 3x3 identity matrix.
   */
  CGL_INLINE static Matrix3x3 identity( void ) {
    Matrix3x3 B;

    B(0,0) = 1.f; B(0,1) = 0.f; B(0,2) = 0.f;
    B(1,0) = 0.f; B(1,1) = 1.f; B(1,2) = 0.f;
    B(2,0) = 0.f; B(2,1) = 0.f; B(2,2) = 1.f;

    return B;
  }

  /**
   * Returns a matrix representing the (left) cross product with u.
   */
  CGL_INLINE static Matrix3x3 crossProduct( const Vector3D& u ) {
    Matrix3x3 B;

    B(0,0) =   0.f;  B(0,1) = -u.z;  B(0,2) =  u.y;
    B(1,0) =  u.z;  B(1,1) =   0.f;  B(1,2) = -u.x;
    B(2,0) = -u.y;  B(2,1) =  u.x;  B(2,2) =   0.f;

    return B;
  }

  /**
   * Returns the ith column.
   */
  CGL_INLINE       Vector3D& column( int i )       { return entries[i]; }
  CGL_INLINE const Vector3D& column( int i ) const { return entries[i]; }

  /**
   * Returns the transpose of A.
   */
  CGL_INLINE Matrix3x3 T( void ) const {
    const Matrix3x3& A( *this );
    Matrix3x3 B;

    for( int i = 0; i < 3; i++ )
    for( int j = 0; j < 3; j++ )
    {
       B(i,j) = A(j,i);
    }

    return B;
  }

  /**
   * Returns the inverse of A.
   */
  CGL_INLINE Matrix3x3 inv( void ) const {
    const Matrix3x3& A( *this );
    Matrix3x3 B;

    B(0,0) = -A(1,2)*A(2,1) + A(1,1)*A(2,2); B(0,1) =  A(0,2)*A(2,1) - A(0,1)*A(2,2); B(0,2) = -A(0,2)*A(1,1) + A(0,1)*A(1,2);
    B(1,0) =  A(1,2)*A(2,0) - A(1,0)*A(2,2); B(1,1) = -A(0,2)*A(2,0) + A(0,0)*A(2,2); B(1,2) =  A(0,2)*A(1,0) - A(0,0)*A(1,2);
    B(2,0) = -A(1,1)*A(2,0) + A(1,0)*A(2,1); B(2,1) =  A(0,1)*A(2,0) - A(0,0)*A(2,1); B(2,2) = -A(0,1)*A(1,0) + A(0,0)*A(1,1);

    B /= det();

    return B;
  }

  // accesses element (i,j) of A using 0-based indexing
  CGL_INLINE       float& operator()( int i, int j )       { return entries[j][i]; }
  CGL_INLINE const float& operator()( int i, int j ) const { return entries[j][i]; }

  // accesses the ith column of A
  CGL_INLINE       Vector3D& operator[]( int j )       { return entries[j]; }
  CGL_INLINE const Vector3D& operator[]( int j ) const { return entries[j]; }

  // increments by B
  CGL_INLINE void operator+=( const Matrix3x3& B ) {
    entries[0] += B[0];
    entries[1] += B[1];
    entries[2] += B[2];
  }

  // returns -A
  CGL_INLINE Matrix3x3 operator-( void ) const {
    Matrix3x3 B;

    B[0] = -entries[0];
    B[1] = -entries[1];
    B[2] = -entries[2];

    return B;
  }

  // returns A-B
  CGL_INLINE Matrix3x3 operator-( const Matrix3x3& B ) const {
    Matrix3x3 C;

    C[0] = entries[0] - B[0];
    C[1] = entries[1] - B[1];
    C[2] = entries[2] - B[2];

    return C;
  }

  // returns c*A
  CGL_INLINE Matrix3x3 operator*( float c ) const {
    Matrix3x3 B;

    B[0] = entries[0] * c;
    B[1] = entries[1] * c;
    B[2] = entries[2] * c;

    return B;
  }

  // returns A*x
  CGL_INLINE Vector3D operator*( const Vector3D& x ) const {
    return x.x * entries[0] +
           x.y * entries[1] +
           x.z * entries[2];
  }

  // returns A*B
  CGL_INLINE Matrix3x3 operator*( const Matrix3x3& B ) const {
    const Matrix3x3& A( *this );
    Matrix3x3 C;

    C[0] = A * B[0];
    C[1] = A * B[1];
    C[2] = A * B[2];

    return C;
  }

  // divides each element by x
  CGL_INLINE void operator/=( float x ) {
    float rx = 1.f/x;

    entries[0] *= rx;
    entries[1] *= rx;
    entries[2] *= rx;
  }

  protected:

  // column vectors
  Vector3D entries[3];

}; // class Matrix3x3

// returns the outer product of u and v
CGL_INLINE Matrix3x3 outer( const Vector3D& u, const Vector3D& v ) {
  Matrix3x3 B;

  for( int i = 0; i < 3; i++ )
  for( int j = 0; j < 3; j++ )
  {
    B(i,j) = u[i] * v[j];
  }

  return B;
}

// returns c*A
CGL_INLINE Matrix3x3 operator*( float c, const Matrix3x3& A ) {
  return A * c;
}

// prints entries (host only)
std::ostream& operator<<( std::ostream& os, const Matrix3x3& A );

} // namespace CGL

#endif // CGL_MATRIX3X3_H
