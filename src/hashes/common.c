#include "../internal.h"
#include <math.h>
#include <stddef.h> // For size_t
#include <stdint.h>

// Include intrinsics based on detected architecture
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#elif defined(__SSE4_2__)
#include <nmmintrin.h>
#endif

PH_API int ph_hamming_distance(uint64_t hash1, uint64_t hash2) {
    uint64_t x = hash1 ^ hash2;
#if defined(__GNUC__) || defined(__clang__)
    // GCC/Clang built-in for 64-bit popcount
    return __builtin_popcountll(x);
#elif defined(_MSC_VER)
    // MSVC intrinsic for 64-bit popcount
    return (int)__popcnt64(x);
#else
    // Fallback: Kernighan's bit counting algorithm
    int count = 0;
    while (x) {
        x &= (x - 1);
        count++;
    }
    return count;
#endif
}

PH_API int ph_hamming_distance_digest(const ph_digest_t *a, const ph_digest_t *b) {
    if (!a || !b || a->size != b->size)
        return -1;

    size_t len = a->size;
    int total = 0;
    size_t i = 0;

    // --- Optimization 1: AVX2 (x86) ---
#if defined(__AVX2__)
    const uint8_t *a_ptr = a->data;
    const uint8_t *b_ptr = b->data;
    size_t len32 = len / 32;

    for (; i < len32; i++) {
        __m256i v_a = _mm256_loadu_si256((const __m256i *)&a_ptr[i * 32]);
        __m256i v_b = _mm256_loadu_si256((const __m256i *)&b_ptr[i * 32]);
        __m256i vxor = _mm256_xor_si256(v_a, v_b);
        uint64_t v0 = _mm256_extract_epi64(vxor, 0);
        uint64_t v1 = _mm256_extract_epi64(vxor, 1);
        uint64_t v2 = _mm256_extract_epi64(vxor, 2);
        uint64_t v3 = _mm256_extract_epi64(vxor, 3);
        
#if defined(__GNUC__) || defined(__clang__)
        total += __builtin_popcountll(v0) + __builtin_popcountll(v1) + 
                 __builtin_popcountll(v2) + __builtin_popcountll(v3);
#else
        total += (int)(_mm_popcnt_u64(v0) + _mm_popcnt_u64(v1) + 
                       _mm_popcnt_u64(v2) + _mm_popcnt_u64(v3));
#endif
    }
    i *= 32; // Advance byte index
#endif

    // --- Optimization 1b: SSE4.2 (x86) ---
#if defined(__SSE4_2__) || defined(__AVX2__)
    // AVX2 implies SSE4.2. We use 'i < len / 8' so it gracefully covers remainders of 32
    const uint64_t *a64 = (const uint64_t *)a->data;
    const uint64_t *b64 = (const uint64_t *)b->data;
    size_t len8 = len / 8;

    for (; i < len8; i++) {
        uint64_t x = a64[i] ^ b64[i];
#if defined(__GNUC__) || defined(__clang__)
        total += __builtin_popcountll(x);
#else
        total += (int)_mm_popcnt_u64(x);
#endif
    }
    i *= 8; // Advance byte index
#endif

    // --- Optimization 2: NEON (ARM) ---
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    // Process in 16-byte chunks (uint8x16_t)
    uint16x8_t v_sum = vdupq_n_u16(0);

    for (; i + 16 <= len; i += 16) {
        uint8x16_t va = vld1q_u8(&a->data[i]);
        uint8x16_t vb = vld1q_u8(&b->data[i]);
        uint8x16_t vxor = veorq_u8(va, vb);
        uint8x16_t vcnt = vcntq_u8(vxor); // Byte-wise popcount
        v_sum = vpadalq_u8(v_sum, vcnt);  // Accumulate 8-bit counts into 16-bit
    }
    // Final reduction of the 16-bit vector sum
    total += (int)vaddlvq_u16(v_sum);
#endif

    // --- Fallback/Remainder Loop ---
    for (; i < len; i++) {
        uint8_t x = a->data[i] ^ b->data[i];

        // Use generic built-in popcount if available for the remainder
#if defined(__GNUC__) || defined(__clang__)
        total += __builtin_popcount(x);
#else
        // Fallback: Kernighan's algorithm for the remaining bytes
        while (x) {
            x &= (x - 1);
            total++;
        }
#endif
    }
    return total;
}

PH_API double ph_l2_distance(const ph_digest_t *a, const ph_digest_t *b) {
    if (!a || !b || a->size != b->size)
        return -1.0;

    double sum = 0;
    for (int i = 0; i < a->size; i++) {
        double diff = (double)a->data[i] - (double)b->data[i];
        sum += diff * diff;
    }
    return sqrt(sum);
}
