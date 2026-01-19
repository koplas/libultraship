#pragma once

// Cross-platform SIMD utilities for vertex processing
// Supports SSE2, SSE4.1, AVX, and ARM NEON

#include <cmath>
#include <algorithm>

// Detect SIMD capabilities
#if defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64)))
    #define SIMD_SSE2_ENABLED 1
    #include <emmintrin.h>  // SSE2
#endif

#if defined(__SSE4_1__) || (defined(_MSC_VER) && defined(__AVX__))
    #define SIMD_SSE41_ENABLED 1
    #include <smmintrin.h>  // SSE4.1
#endif

#if defined(__AVX__)
    #define SIMD_AVX_ENABLED 1
    #include <immintrin.h>  // AVX
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #define SIMD_NEON_ENABLED 1
    #include <arm_neon.h>
#endif

namespace Fast {
namespace SIMD {

// Helper functions for matrix-vector multiplication using SIMD

#if defined(SIMD_SSE2_ENABLED)

// Multiply a 3D position (x, y, z) by a 4x4 matrix (treating position as [x, y, z, 1])
// Returns [x', y', z', w']
inline __m128 MatrixVecMul4x4(float x, float y, float z, const float matrix[4][4]) {
    // Load matrix columns (transposed for efficiency)
    // Each column represents one row of output
    __m128 m0 = _mm_loadu_ps(matrix[0]);
    __m128 m1 = _mm_loadu_ps(matrix[1]);
    __m128 m2 = _mm_loadu_ps(matrix[2]);
    __m128 m3 = _mm_loadu_ps(matrix[3]);

    // Broadcast each component
    __m128 vx = _mm_set1_ps(x);
    __m128 vy = _mm_set1_ps(y);
    __m128 vz = _mm_set1_ps(z);

    // result = x * m0 + y * m1 + z * m2 + m3
    __m128 result = _mm_add_ps(
        _mm_add_ps(_mm_mul_ps(vx, m0), _mm_mul_ps(vy, m1)),
        _mm_add_ps(_mm_mul_ps(vz, m2), m3)
    );

    return result;
}

// Compute 3-component dot product: result = a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
inline float DotProduct3(float a0, float a1, float a2, float b0, float b1, float b2) {
    __m128 va = _mm_setr_ps(a0, a1, a2, 0.0f);
    __m128 vb = _mm_setr_ps(b0, b1, b2, 0.0f);
    __m128 mul = _mm_mul_ps(va, vb);

    // Horizontal add
    __m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 sums = _mm_add_ps(mul, shuf);
    shuf = _mm_movehl_ps(shuf, sums);
    sums = _mm_add_ss(sums, shuf);

    return _mm_cvtss_f32(sums);
}

// Compute 3-component dot product where first vector is signed char
inline float DotProduct3Char(signed char a0, signed char a1, signed char a2, float b0, float b1, float b2) {
    __m128 va = _mm_setr_ps((float)a0, (float)a1, (float)a2, 0.0f);
    __m128 vb = _mm_setr_ps(b0, b1, b2, 0.0f);
    __m128 mul = _mm_mul_ps(va, vb);

    // Horizontal add
    __m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 sums = _mm_add_ps(mul, shuf);
    shuf = _mm_movehl_ps(shuf, sums);
    sums = _mm_add_ss(sums, shuf);

    return _mm_cvtss_f32(sums);
}

// Compute distance squared between two 3D points
inline float DistanceSquared3(float ax, float ay, float az, float bx, float by, float bz) {
    __m128 va = _mm_setr_ps(ax, ay, az, 0.0f);
    __m128 vb = _mm_setr_ps(bx, by, bz, 0.0f);
    __m128 diff = _mm_sub_ps(vb, va);
    __m128 sq = _mm_mul_ps(diff, diff);

    // Extract components for custom calculation (z is multiplied by 2)
    float result[4];
    _mm_storeu_ps(result, sq);
    return result[0] + result[1] + result[2] * 2.0f; // Note: z component is *2 as per original code
}

// Subtract two 3D vectors
inline void VectorSub3(float result[3], float ax, float ay, float az, float bx, float by, float bz) {
    __m128 va = _mm_setr_ps(ax, ay, az, 0.0f);
    __m128 vb = _mm_setr_ps(bx, by, bz, 0.0f);
    __m128 diff = _mm_sub_ps(vb, va);
    _mm_storeu_ps(result, diff);
}

#elif defined(SIMD_NEON_ENABLED)

// ARM NEON implementations

inline float32x4_t MatrixVecMul4x4(float x, float y, float z, const float matrix[4][4]) {
    float32x4_t m0 = vld1q_f32(matrix[0]);
    float32x4_t m1 = vld1q_f32(matrix[1]);
    float32x4_t m2 = vld1q_f32(matrix[2]);
    float32x4_t m3 = vld1q_f32(matrix[3]);

    float32x4_t vx = vdupq_n_f32(x);
    float32x4_t vy = vdupq_n_f32(y);
    float32x4_t vz = vdupq_n_f32(z);

    float32x4_t result = vmlaq_f32(m3, vx, m0);
    result = vmlaq_f32(result, vy, m1);
    result = vmlaq_f32(result, vz, m2);

    return result;
}

inline float DotProduct3(float a0, float a1, float a2, float b0, float b1, float b2) {
    float32x4_t va = {a0, a1, a2, 0.0f};
    float32x4_t vb = {b0, b1, b2, 0.0f};
    float32x4_t mul = vmulq_f32(va, vb);

    // Horizontal add (pairwise)
    float32x2_t sum_pairs = vadd_f32(vget_low_f32(mul), vget_high_f32(mul));
    return vget_lane_f32(vpadd_f32(sum_pairs, sum_pairs), 0);
}

inline float DotProduct3Char(signed char a0, signed char a1, signed char a2, float b0, float b1, float b2) {
    float32x4_t va = {(float)a0, (float)a1, (float)a2, 0.0f};
    float32x4_t vb = {b0, b1, b2, 0.0f};
    float32x4_t mul = vmulq_f32(va, vb);

    float32x2_t sum_pairs = vadd_f32(vget_low_f32(mul), vget_high_f32(mul));
    return vget_lane_f32(vpadd_f32(sum_pairs, sum_pairs), 0);
}

inline float DistanceSquared3(float ax, float ay, float az, float bx, float by, float bz) {
    float32x4_t va = {ax, ay, az, 0.0f};
    float32x4_t vb = {bx, by, bz, 0.0f};
    float32x4_t diff = vsubq_f32(vb, va);
    float32x4_t sq = vmulq_f32(diff, diff);

    float result[4];
    vst1q_f32(result, sq);
    return result[0] + result[1] + result[2] * 2.0f;
}

inline void VectorSub3(float result[3], float ax, float ay, float az, float bx, float by, float bz) {
    float32x4_t va = {ax, ay, az, 0.0f};
    float32x4_t vb = {bx, by, bz, 0.0f};
    float32x4_t diff = vsubq_f32(vb, va);
    vst1q_f32(result, diff);
}

#else

// Scalar fallback implementations

inline void MatrixVecMul4x4_Scalar(float result[4], float x, float y, float z, const float matrix[4][4]) {
    result[0] = x * matrix[0][0] + y * matrix[1][0] + z * matrix[2][0] + matrix[3][0];
    result[1] = x * matrix[0][1] + y * matrix[1][1] + z * matrix[2][1] + matrix[3][1];
    result[2] = x * matrix[0][2] + y * matrix[1][2] + z * matrix[2][2] + matrix[3][2];
    result[3] = x * matrix[0][3] + y * matrix[1][3] + z * matrix[2][3] + matrix[3][3];
}

inline float DotProduct3(float a0, float a1, float a2, float b0, float b1, float b2) {
    return a0 * b0 + a1 * b1 + a2 * b2;
}

inline float DotProduct3Char(signed char a0, signed char a1, signed char a2, float b0, float b1, float b2) {
    return (float)a0 * b0 + (float)a1 * b1 + (float)a2 * b2;
}

inline float DistanceSquared3(float ax, float ay, float az, float bx, float by, float bz) {
    float dx = bx - ax;
    float dy = by - ay;
    float dz = bz - az;
    return dx * dx + dy * dy + dz * dz * 2.0f;
}

inline void VectorSub3(float result[3], float ax, float ay, float az, float bx, float by, float bz) {
    result[0] = bx - ax;
    result[1] = by - ay;
    result[2] = bz - az;
}

#endif

} // namespace SIMD
} // namespace Fast
