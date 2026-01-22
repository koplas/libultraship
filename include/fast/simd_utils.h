#pragma once

// Auto-vectorization friendly utilities for vertex processing
// Written to allow compiler auto-vectorization without explicit intrinsics

#include <cmath>
#include <algorithm>

// Compiler hints for auto-vectorization
#if defined(__GNUC__) || defined(__clang__)
    #define VECMATH_INLINE __attribute__((always_inline)) inline
    #define VECMATH_RESTRICT __restrict__
    #define VECMATH_ASSUME_ALIGNED(ptr, align) __builtin_assume_aligned(ptr, align)
#elif defined(_MSC_VER)
    #define VECMATH_INLINE __forceinline
    #define VECMATH_RESTRICT __restrict
    #define VECMATH_ASSUME_ALIGNED(ptr, align) ptr
#else
    #define VECMATH_INLINE inline
    #define VECMATH_RESTRICT
    #define VECMATH_ASSUME_ALIGNED(ptr, align) ptr
#endif

namespace Fast {
namespace VecMath {

// Matrix-vector multiplication optimized for auto-vectorization
// Multiply a 3D position (x, y, z) by a 4x4 matrix, treating position as [x, y, z, 1]
// The compiler will auto-vectorize these loops with appropriate flags
VECMATH_INLINE void MatrixVecMul4x4(
    float* VECMATH_RESTRICT result,
    float x, float y, float z,
    const float (* VECMATH_RESTRICT matrix)[4]
) {
    // Written as simple loops to enable auto-vectorization
    // Most compilers will vectorize this into 4-wide SIMD operations
    #pragma GCC ivdep
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < 4; i++) {
        result[i] = x * matrix[0][i] + y * matrix[1][i] + z * matrix[2][i] + matrix[3][i];
    }
}

// 3-component dot product optimized for auto-vectorization
VECMATH_INLINE float DotProduct3(
    const float* VECMATH_RESTRICT a,
    const float* VECMATH_RESTRICT b
) {
    float sum = 0.0f;
    // Compiler will recognize this pattern and vectorize it
    #pragma GCC unroll 3
    #pragma clang loop unroll_count(3)
    for (int i = 0; i < 3; i++) {
        sum += a[i] * b[i];
    }
    return sum;
}

// 3-component dot product with signed char first operand
VECMATH_INLINE float DotProduct3Char(
    const signed char* VECMATH_RESTRICT a,
    const float* VECMATH_RESTRICT b
) {
    float sum = 0.0f;
    #pragma GCC unroll 3
    #pragma clang loop unroll_count(3)
    for (int i = 0; i < 3; i++) {
        sum += (float)a[i] * b[i];
    }
    return sum;
}

// Distance squared between two 3D points
// Note: z component is multiplied by 2 as per original implementation
VECMATH_INLINE float DistanceSquared3(
    const float* VECMATH_RESTRICT a,
    const float* VECMATH_RESTRICT b
) {
    float diff[3];
    #pragma GCC ivdep
    #pragma clang loop vectorize(enable)
    for (int i = 0; i < 3; i++) {
        diff[i] = b[i] - a[i];
    }

    // Special case: z is multiplied by 2 (from original code)
    return diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2] * 2.0f;
}

// Subtract two 3D vectors
VECMATH_INLINE void VectorSub3(
    float* VECMATH_RESTRICT result,
    const float* VECMATH_RESTRICT a,
    const float* VECMATH_RESTRICT b
) {
    #pragma GCC ivdep
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < 3; i++) {
        result[i] = b[i] - a[i];
    }
}

} // namespace VecMath
} // namespace Fast
