#pragma once

/**
 * SAGE SIMD Operations
 * Production-grade vectorized math for HFT
 */

#include <cstddef>
#include <cstdint>
#include <cmath>
#include "../core/compiler.hpp"

// Platform detection
#if defined(__AVX512F__)
    #include <immintrin.h>
    #define SAGE_HAS_AVX512 1
    #define SAGE_HAS_AVX2 1
    #define SAGE_SIMD_WIDTH 8
#elif defined(__AVX2__)
    #include <immintrin.h>
    #define SAGE_HAS_AVX2 1
    #define SAGE_SIMD_WIDTH 4
#elif defined(__SSE4_2__)
    #include <nmmintrin.h>
    #define SAGE_HAS_SSE42 1
    #define SAGE_SIMD_WIDTH 2
#else
    #define SAGE_SIMD_WIDTH 1
#endif

namespace sage {
namespace hpcm {

// ============================================================================
// Dot Product
// ============================================================================

#ifdef SAGE_HAS_AVX512

/**
 * AVX-512 dot product (8 doubles per iteration)
 */
SAGE_HOT
inline double dot_product_avx512(const double* SAGE_RESTRICT a, 
                                  const double* SAGE_RESTRICT b, 
                                  size_t n) noexcept {
    __m512d sum = _mm512_setzero_pd();
    
    size_t i = 0;
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(&a[i]);
        __m512d vb = _mm512_loadu_pd(&b[i]);
        sum = _mm512_fmadd_pd(va, vb, sum);
    }
    
    double total = _mm512_reduce_add_pd(sum);
    
    // Remainder
    for (; i < n; ++i) {
        total += a[i] * b[i];
    }
    
    return total;
}

#define dot_product dot_product_avx512

#elif defined(SAGE_HAS_AVX2)

/**
 * AVX2 dot product (4 doubles per iteration)
 * Unrolled 4x for better instruction-level parallelism
 */
SAGE_HOT
inline double dot_product_avx2(const double* SAGE_RESTRICT a, 
                                const double* SAGE_RESTRICT b, 
                                size_t n) noexcept {
    __m256d sum0 = _mm256_setzero_pd();
    __m256d sum1 = _mm256_setzero_pd();
    __m256d sum2 = _mm256_setzero_pd();
    __m256d sum3 = _mm256_setzero_pd();
    
    size_t i = 0;
    
    // Process 16 doubles per iteration (4x unroll)
    for (; i + 15 < n; i += 16) {
        __m256d va0 = _mm256_loadu_pd(&a[i]);
        __m256d vb0 = _mm256_loadu_pd(&b[i]);
        sum0 = _mm256_fmadd_pd(va0, vb0, sum0);
        
        __m256d va1 = _mm256_loadu_pd(&a[i + 4]);
        __m256d vb1 = _mm256_loadu_pd(&b[i + 4]);
        sum1 = _mm256_fmadd_pd(va1, vb1, sum1);
        
        __m256d va2 = _mm256_loadu_pd(&a[i + 8]);
        __m256d vb2 = _mm256_loadu_pd(&b[i + 8]);
        sum2 = _mm256_fmadd_pd(va2, vb2, sum2);
        
        __m256d va3 = _mm256_loadu_pd(&a[i + 12]);
        __m256d vb3 = _mm256_loadu_pd(&b[i + 12]);
        sum3 = _mm256_fmadd_pd(va3, vb3, sum3);
    }
    
    // Combine partial sums
    sum0 = _mm256_add_pd(sum0, sum1);
    sum2 = _mm256_add_pd(sum2, sum3);
    sum0 = _mm256_add_pd(sum0, sum2);
    
    // Process remaining 4-element chunks
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(&a[i]);
        __m256d vb = _mm256_loadu_pd(&b[i]);
        sum0 = _mm256_fmadd_pd(va, vb, sum0);
    }
    
    // Horizontal sum
    __m128d sum_high = _mm256_extractf128_pd(sum0, 1);
    __m128d sum_low = _mm256_castpd256_pd128(sum0);
    __m128d sum128 = _mm_add_pd(sum_high, sum_low);
    __m128d sum_dup = _mm_unpackhi_pd(sum128, sum128);
    __m128d total_vec = _mm_add_sd(sum128, sum_dup);
    double total = _mm_cvtsd_f64(total_vec);
    
    // Handle remaining elements
    for (; i < n; ++i) {
        total += a[i] * b[i];
    }
    
    return total;
}

#define dot_product dot_product_avx2

#else

/**
 * Scalar fallback with loop unrolling
 */
SAGE_HOT
inline double dot_product_scalar(const double* SAGE_RESTRICT a, 
                                  const double* SAGE_RESTRICT b, 
                                  size_t n) noexcept {
    double sum0 = 0.0, sum1 = 0.0, sum2 = 0.0, sum3 = 0.0;
    
    size_t i = 0;
    for (; i + 3 < n; i += 4) {
        sum0 += a[i] * b[i];
        sum1 += a[i+1] * b[i+1];
        sum2 += a[i+2] * b[i+2];
        sum3 += a[i+3] * b[i+3];
    }
    
    double total = sum0 + sum1 + sum2 + sum3;
    
    for (; i < n; ++i) {
        total += a[i] * b[i];
    }
    
    return total;
}

#define dot_product dot_product_scalar

#endif

// ============================================================================
// Vector Operations
// ============================================================================

/**
 * Vector add: c = a + b
 */
SAGE_HOT
inline void vector_add(const double* SAGE_RESTRICT a,
                       const double* SAGE_RESTRICT b,
                       double* SAGE_RESTRICT c,
                       size_t n) noexcept {
#ifdef SAGE_HAS_AVX2
    size_t i = 0;
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(&a[i]);
        __m256d vb = _mm256_loadu_pd(&b[i]);
        _mm256_storeu_pd(&c[i], _mm256_add_pd(va, vb));
    }
    for (; i < n; ++i) {
        c[i] = a[i] + b[i];
    }
#else
    for (size_t i = 0; i < n; ++i) {
        c[i] = a[i] + b[i];
    }
#endif
}

/**
 * Vector scale: b = a * scalar
 */
SAGE_HOT
inline void vector_scale(const double* SAGE_RESTRICT a,
                         double scalar,
                         double* SAGE_RESTRICT b,
                         size_t n) noexcept {
#ifdef SAGE_HAS_AVX2
    __m256d vs = _mm256_set1_pd(scalar);
    size_t i = 0;
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(&a[i]);
        _mm256_storeu_pd(&b[i], _mm256_mul_pd(va, vs));
    }
    for (; i < n; ++i) {
        b[i] = a[i] * scalar;
    }
#else
    for (size_t i = 0; i < n; ++i) {
        b[i] = a[i] * scalar;
    }
#endif
}

// ============================================================================
// Fixed-Point SIMD (for int64_t prices)
// ============================================================================

#ifdef SAGE_HAS_AVX2

/**
 * Compare 4 int64 values, return mask of values < threshold
 */
SAGE_HOT SAGE_ALWAYS_INLINE
uint8_t compare_lt_i64x4(const int64_t* values, int64_t threshold) noexcept {
    __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(values));
    __m256i t = _mm256_set1_epi64x(threshold);
    __m256i cmp = _mm256_cmpgt_epi64(t, v);  // threshold > values
    return static_cast<uint8_t>(_mm256_movemask_pd(_mm256_castsi256_pd(cmp)));
}

/**
 * Sum 4 int64 values
 */
SAGE_HOT SAGE_ALWAYS_INLINE
int64_t sum_i64x4(const int64_t* values) noexcept {
    __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(values));
    __m128i high = _mm256_extracti128_si256(v, 1);
    __m128i low = _mm256_castsi256_si128(v);
    __m128i sum2 = _mm_add_epi64(high, low);
    __m128i sum1 = _mm_unpackhi_epi64(sum2, sum2);
    __m128i result = _mm_add_epi64(sum2, sum1);
    return _mm_cvtsi128_si64(result);
}

#endif

} // namespace hpcm
} // namespace sage
