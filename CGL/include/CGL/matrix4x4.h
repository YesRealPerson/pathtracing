#ifndef CGL_MATRIX4X4_H
#define CGL_MATRIX4X4_H

#include "CGL.h"
#include "vector4D.h"

#include <iosfwd>

namespace CGL {

/**
 * Defines a 4x4 matrix.
 * 4x4 matrices are also extremely useful in computer graphics.
 * Written by Bryce Summers on 9/10/2015.f
 * Adapted from the Matrix3x3 class.
 *
 * EXTEND_ME : It might be nice to add some combined operations
 *             such as multiplying then adding,
 *             etc to increase arithmetic intensity.
 * I have taken the liberty of removing cross product functionality form 4D Matrices and Vectors.
 *
 * All members are defined inline in this header and marked host/device so the
 * class can be used from both CPU code and CUDA kernels.
 */
class Matrix4x4 {

  public:


  // The default constructor. (All entries zero.)
  CGL_HD Matrix4x4(void) { }

  // Constructor for row major form data.
  // Transposes to the internal column major form.
  // REQUIRES: data should be of size 16.f
  CGL_HD Matrix4x4(const float * data)
  {
    for( int i = 0; i < 4; i++ )
    for( int j = 0; j < 4; j++ )
    {
	  // Transpostion happens within the () query.
	  (*this)(i,j) = data[i*4 + j];
    }

  }

  Matrix4x4( const Matrix4x4& B ) = default;
  Matrix4x4& operator=( const Matrix4x4& B ) = default;

  /**
   * Sets all elements to val.
   */
  CGL_INLINE void zero(float val = 0.0f) {
    entries[0] = Vector4D(val);
    entries[1] = Vector4D(val);
    entries[2] = Vector4D(val);
    entries[3] = Vector4D(val);
  }

  /**
   * Returns the determinant of A.
   */
  CGL_INLINE float det( void ) const {
    const Matrix4x4 &A(*this);

    return A(0, 3) * A(1, 2) * A(2, 1) * A(3, 0) -
           A(0, 2) * A(1, 3) * A(2, 1) * A(3, 0) -
           A(0, 3) * A(1, 1) * A(2, 2) * A(3, 0) +
           A(0, 1) * A(1, 3) * A(2, 2) * A(3, 0) +
           A(0, 2) * A(1, 1) * A(2, 3) * A(3, 0) -
           A(0, 1) * A(1, 2) * A(2, 3) * A(3, 0) -
           A(0, 3) * A(1, 2) * A(2, 0) * A(3, 1) +
           A(0, 2) * A(1, 3) * A(2, 0) * A(3, 1) +
           A(0, 3) * A(1, 0) * A(2, 2) * A(3, 1) -
           A(0, 0) * A(1, 3) * A(2, 2) * A(3, 1) -
           A(0, 2) * A(1, 0) * A(2, 3) * A(3, 1) +
           A(0, 0) * A(1, 2) * A(2, 3) * A(3, 1) +
           A(0, 3) * A(1, 1) * A(2, 0) * A(3, 2) -
           A(0, 1) * A(1, 3) * A(2, 0) * A(3, 2) -
           A(0, 3) * A(1, 0) * A(2, 1) * A(3, 2) +
           A(0, 0) * A(1, 3) * A(2, 1) * A(3, 2) +
           A(0, 1) * A(1, 0) * A(2, 3) * A(3, 2) -
           A(0, 0) * A(1, 1) * A(2, 3) * A(3, 2) -
           A(0, 2) * A(1, 1) * A(2, 0) * A(3, 3) +
           A(0, 1) * A(1, 2) * A(2, 0) * A(3, 3) +
           A(0, 2) * A(1, 0) * A(2, 1) * A(3, 3) -
           A(0, 0) * A(1, 2) * A(2, 1) * A(3, 3) -
           A(0, 1) * A(1, 0) * A(2, 2) * A(3, 3) +
           A(0, 0) * A(1, 1) * A(2, 2) * A(3, 3);
  }

  /**
   * Returns the Frobenius norm of A.
   */
  CGL_INLINE float norm( void ) const {
    return sqrtf(entries[0].norm2() + entries[1].norm2() + entries[2].norm2() +
                entries[3].norm2());
  }

  /**
   * Returns a fresh 4x4 identity matrix.
   */
  CGL_INLINE static Matrix4x4 identity( void ) {
    Matrix4x4 B;

    for( int i = 0; i < 4; i++ )
    for( int j = 0; j < 4; j++ )
    {
      B(i,j) = (i == j) ? 1.f : 0.f;
    }

    return B;
  }

  // No Cross products for 4 by 4 matrix.

  /**
   * Returns the ith column.
   */
  CGL_INLINE       Vector4D& column( int i )       { return entries[i]; }
  CGL_INLINE const Vector4D& column( int i ) const { return entries[i]; }

  /**
   * Returns the transpose of A.
   */
  CGL_INLINE Matrix4x4 T( void ) const {
    const Matrix4x4& A( *this );
    Matrix4x4 B;

    for( int i = 0; i < 4; i++ )
    for( int j = 0; j < 4; j++ )
    {
      B(i,j) = A(j,i);
    }

    return B;
  }

  /**
   * Returns the inverse of A.
   */
  CGL_INLINE Matrix4x4 inv( void ) const {
    const Matrix4x4 &A(*this);
    Matrix4x4 B;

    // Hardcoded in Fully Symbolic computation.

    B(0, 0) = A(1, 2) * A(2, 3) * A(3, 1) - A(1, 3) * A(2, 2) * A(3, 1) +
              A(1, 3) * A(2, 1) * A(3, 2) - A(1, 1) * A(2, 3) * A(3, 2) -
              A(1, 2) * A(2, 1) * A(3, 3) + A(1, 1) * A(2, 2) * A(3, 3);
    B(0, 1) = A(0, 3) * A(2, 2) * A(3, 1) - A(0, 2) * A(2, 3) * A(3, 1) -
              A(0, 3) * A(2, 1) * A(3, 2) + A(0, 1) * A(2, 3) * A(3, 2) +
              A(0, 2) * A(2, 1) * A(3, 3) - A(0, 1) * A(2, 2) * A(3, 3);
    B(0, 2) = A(0, 2) * A(1, 3) * A(3, 1) - A(0, 3) * A(1, 2) * A(3, 1) +
              A(0, 3) * A(1, 1) * A(3, 2) - A(0, 1) * A(1, 3) * A(3, 2) -
              A(0, 2) * A(1, 1) * A(3, 3) + A(0, 1) * A(1, 2) * A(3, 3);
    B(0, 3) = A(0, 3) * A(1, 2) * A(2, 1) - A(0, 2) * A(1, 3) * A(2, 1) -
              A(0, 3) * A(1, 1) * A(2, 2) + A(0, 1) * A(1, 3) * A(2, 2) +
              A(0, 2) * A(1, 1) * A(2, 3) - A(0, 1) * A(1, 2) * A(2, 3);
    B(1, 0) = A(1, 3) * A(2, 2) * A(3, 0) - A(1, 2) * A(2, 3) * A(3, 0) -
              A(1, 3) * A(2, 0) * A(3, 2) + A(1, 0) * A(2, 3) * A(3, 2) +
              A(1, 2) * A(2, 0) * A(3, 3) - A(1, 0) * A(2, 2) * A(3, 3);
    B(1, 1) = A(0, 2) * A(2, 3) * A(3, 0) - A(0, 3) * A(2, 2) * A(3, 0) +
              A(0, 3) * A(2, 0) * A(3, 2) - A(0, 0) * A(2, 3) * A(3, 2) -
              A(0, 2) * A(2, 0) * A(3, 3) + A(0, 0) * A(2, 2) * A(3, 3);
    B(1, 2) = A(0, 3) * A(1, 2) * A(3, 0) - A(0, 2) * A(1, 3) * A(3, 0) -
              A(0, 3) * A(1, 0) * A(3, 2) + A(0, 0) * A(1, 3) * A(3, 2) +
              A(0, 2) * A(1, 0) * A(3, 3) - A(0, 0) * A(1, 2) * A(3, 3);
    B(1, 3) = A(0, 2) * A(1, 3) * A(2, 0) - A(0, 3) * A(1, 2) * A(2, 0) +
              A(0, 3) * A(1, 0) * A(2, 2) - A(0, 0) * A(1, 3) * A(2, 2) -
              A(0, 2) * A(1, 0) * A(2, 3) + A(0, 0) * A(1, 2) * A(2, 3);
    B(2, 0) = A(1, 1) * A(2, 3) * A(3, 0) - A(1, 3) * A(2, 1) * A(3, 0) +
              A(1, 3) * A(2, 0) * A(3, 1) - A(1, 0) * A(2, 3) * A(3, 1) -
              A(1, 1) * A(2, 0) * A(3, 3) + A(1, 0) * A(2, 1) * A(3, 3);
    B(2, 1) = A(0, 3) * A(2, 1) * A(3, 0) - A(0, 1) * A(2, 3) * A(3, 0) -
              A(0, 3) * A(2, 0) * A(3, 1) + A(0, 0) * A(2, 3) * A(3, 1) +
              A(0, 1) * A(2, 0) * A(3, 3) - A(0, 0) * A(2, 1) * A(3, 3);
    B(2, 2) = A(0, 1) * A(1, 3) * A(3, 0) - A(0, 3) * A(1, 1) * A(3, 0) +
              A(0, 3) * A(1, 0) * A(3, 1) - A(0, 0) * A(1, 3) * A(3, 1) -
              A(0, 1) * A(1, 0) * A(3, 3) + A(0, 0) * A(1, 1) * A(3, 3);
    B(2, 3) = A(0, 3) * A(1, 1) * A(2, 0) - A(0, 1) * A(1, 3) * A(2, 0) -
              A(0, 3) * A(1, 0) * A(2, 1) + A(0, 0) * A(1, 3) * A(2, 1) +
              A(0, 1) * A(1, 0) * A(2, 3) - A(0, 0) * A(1, 1) * A(2, 3);
    B(3, 0) = A(1, 2) * A(2, 1) * A(3, 0) - A(1, 1) * A(2, 2) * A(3, 0) -
              A(1, 2) * A(2, 0) * A(3, 1) + A(1, 0) * A(2, 2) * A(3, 1) +
              A(1, 1) * A(2, 0) * A(3, 2) - A(1, 0) * A(2, 1) * A(3, 2);
    B(3, 1) = A(0, 1) * A(2, 2) * A(3, 0) - A(0, 2) * A(2, 1) * A(3, 0) +
              A(0, 2) * A(2, 0) * A(3, 1) - A(0, 0) * A(2, 2) * A(3, 1) -
              A(0, 1) * A(2, 0) * A(3, 2) + A(0, 0) * A(2, 1) * A(3, 2);
    B(3, 2) = A(0, 2) * A(1, 1) * A(3, 0) - A(0, 1) * A(1, 2) * A(3, 0) -
              A(0, 2) * A(1, 0) * A(3, 1) + A(0, 0) * A(1, 2) * A(3, 1) +
              A(0, 1) * A(1, 0) * A(3, 2) - A(0, 0) * A(1, 1) * A(3, 2);
    B(3, 3) = A(0, 1) * A(1, 2) * A(2, 0) - A(0, 2) * A(1, 1) * A(2, 0) +
              A(0, 2) * A(1, 0) * A(2, 1) - A(0, 0) * A(1, 2) * A(2, 1) -
              A(0, 1) * A(1, 0) * A(2, 2) + A(0, 0) * A(1, 1) * A(2, 2);

    // Invertable iff the determinant is not equal to zero.
    B /= det();

    return B;
  }

  // accesses element (i,j) of A using 0-based indexing
  // where (i, j) is (row, column).
  CGL_INLINE       float& operator()( int i, int j )       { return entries[j][i]; }
  CGL_INLINE const float& operator()( int i, int j ) const { return entries[j][i]; }

  // accesses the ith column of A
  CGL_INLINE       Vector4D& operator[]( int j )       { return entries[j]; }
  CGL_INLINE const Vector4D& operator[]( int j ) const { return entries[j]; }

  // increments by B
  CGL_INLINE void operator+=( const Matrix4x4& B ) {
    entries[0] += B[0];
    entries[1] += B[1];
    entries[2] += B[2];
    entries[3] += B[3];
  }

  // returns -A
  CGL_INLINE Matrix4x4 operator-( void ) const {
    Matrix4x4 B;

    B[0] = -entries[0];
    B[1] = -entries[1];
    B[2] = -entries[2];
    B[3] = -entries[3];

    return B;
  }

  // returns A-B
  CGL_INLINE Matrix4x4 operator-( const Matrix4x4& B ) const {
    Matrix4x4 C;

    C[0] = entries[0] - B[0];
    C[1] = entries[1] - B[1];
    C[2] = entries[2] - B[2];
    C[3] = entries[3] - B[3];

    return C;
  }

  // returns c*A
  CGL_INLINE Matrix4x4 operator*( float c ) const {
    Matrix4x4 B;

    B[0] = c * entries[0];
    B[1] = c * entries[1];
    B[2] = c * entries[2];
    B[3] = c * entries[3];

    return B;
  }

  // returns A*B (traditional grade school multiplication, N^3)
  CGL_INLINE Matrix4x4 operator*( const Matrix4x4& B ) const {
    const Matrix4x4& A( *this );
    Matrix4x4 C;

    for( int i = 0; i < 4; i++ )
    for( int j = 0; j < 4; j++ )
    {
      float sum = 0.f;
      for( int k = 0; k < 4; k++ ) {
        sum += A(i,k) * B(k,j);
      }
      C(i,j) = sum;
    }

    return C;
  }

  // returns A*x
  CGL_INLINE Vector4D operator*( const Vector4D& x ) const {
    return x[0] * entries[0] + // Add up products for each matrix column.
           x[1] * entries[1] + x[2] * entries[2] + x[3] * entries[3];
  }

  // divides each element by x
  CGL_INLINE void operator/=( float x ) {
    Matrix4x4& A( *this );
    float rx = 1.f / x;

    for( int i = 0; i < 4; i++ )
    for( int j = 0; j < 4; j++ )
    {
      A(i,j) *= rx;
    }
  }

  protected:

  // 4 by 4 matrices are represented by an array of 4 column vectors.
  Vector4D entries[4];

}; // class Matrix4x4

// returns the outer product of u and v.
CGL_INLINE Matrix4x4 outer( const Vector4D& u, const Vector4D& v ) {
  Matrix4x4 B;

  // Opposite of an inner product.
  for( int i = 0; i < 4; i++ )
  for( int j = 0; j < 4; j++ )
  {
    B(i,j) = u[i] * v[j];
  }

  return B;
}

// returns c*A
CGL_INLINE Matrix4x4 operator*( float c, const Matrix4x4& A ) {
  return A * c;
}

// prints entries (host only)
std::ostream& operator<<( std::ostream& os, const Matrix4x4& A );

} // namespace CGL

#endif // CGL_MATRIX4X4_H
