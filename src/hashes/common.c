#include "../internal.h"
#include <math.h>
#include <stddef.h> // For size_t
#include <stdint.h>

// Include intrinsics based on detected architecture
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
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

    // --- Optimization 1: SSE4.2 (x86) ---
#if defined(__SSE4_2__) && !defined(__GNUC__)
    // Use 64-bit chunks for maximum efficiency with hardware popcount
    const uint64_t *a64 = (const uint64_t *)a->data;
    const uint64_t *b64 = (const uint64_t *)b->data;
    size_t len64 = len / 8;

    for (; i < len64; i++) {
        uint64_t x = a64[i] ^ b64[i];
        total += (int)_mm_popcnt_u64(x);
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
