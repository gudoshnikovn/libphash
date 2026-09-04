#include "internal.h"
#include <math.h>
#include <stddef.h> // For size_t
#include <stdint.h>
#include <string.h>

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
    if (!ph_digests_comparable_as(a, b, PH_DIGEST_KIND_BITS))
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
        total += __builtin_popcountll(v0) + __builtin_popcountll(v1) + __builtin_popcountll(v2) +
                 __builtin_popcountll(v3);
#else
        total += (int)(_mm_popcnt_u64(v0) + _mm_popcnt_u64(v1) + _mm_popcnt_u64(v2) +
                       _mm_popcnt_u64(v3));
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
    if (!ph_digests_comparable_as(a, b, PH_DIGEST_KIND_VECTOR))
        return -1.0;

    double sum = 0;
    for (int i = 0; i < a->size; i++) {
        double diff = (double)a->data[i] - (double)b->data[i];
        sum += diff * diff;
    }
    return sqrt(sum);
}

PH_API double ph_similarity(uint64_t a, uint64_t b) {
    int dist = ph_hamming_distance(a, b);
    return 1.0 - ((double)dist / 64.0);
}

PH_API double ph_similarity_digest(const ph_digest_t *a, const ph_digest_t *b) {
    if (!ph_digests_comparable_as(a, b, PH_DIGEST_KIND_BITS))
        return -1.0;

    int dist = ph_hamming_distance_digest(a, b);
    if (dist < 0)
        return -1.0;

    double total_bits = (double)a->size * 8.0;
    return 1.0 - ((double)dist / total_bits);
}

/* Peak of cross-correlation, the comparison the radial hash's source specifies.
 *
 * Zauner 3.2.3 gives it as pHash's own ph_crosscorr(): the Pearson correlation of the two
 * digests is computed at every cyclic shift of the second one, and the largest of those
 * is the score. The maximisation over shifts is the whole point -- a rotation of the
 * image cyclically shifts the radial variance vector, and taking the best shift is what
 * turns that into a match instead of a mismatch.
 *
 * Worth stating plainly, because the maths does not quite say what the name suggests: the
 * shift is applied to the DCT coefficients, and a cyclic shift of a vector is not a cyclic
 * shift of its DCT. What that buys in practice is tolerance to a few degrees of rotation
 * and an exact match on a half turn -- not invariance to an arbitrary one. Measured in
 * tests/src/test_radial.c and docs/algorithm-provenance.md section 7.
 *
 * Degenerate input: a digest whose bytes are all equal has zero variance and no Pearson
 * correlation is defined against it. Two such digests are reported as a perfect match
 * (1.0) -- the radial hash emits an all-zero digest for a flat image, and two flat images
 * are the same picture as far as this descriptor can tell -- and a constant against a
 * varying one as no match at all (0.0).
 */
PH_API ph_error_t ph_radial_similarity(const ph_digest_t *a, const ph_digest_t *b,
                                       double *out_pcc) {
    if (!out_pcc || !ph_digests_comparable_as(a, b, PH_DIGEST_KIND_COEFFICIENTS))
        return PH_ERR_INVALID_ARGUMENT;

    const int n = a->size;
    double sum_a = 0.0, sum_b = 0.0;
    for (int i = 0; i < n; i++) {
        sum_a += (double)a->data[i];
        sum_b += (double)b->data[i];
    }
    const double mean_a = sum_a / n;
    const double mean_b = sum_b / n;

    /* The denominators do not depend on the shift: a cyclic shift permutes the terms of
     * each sum of squares without changing it. pHash recomputes them inside the shift
     * loop; the result is identical and this way the loop is O(n^2) multiplications
     * rather than three times that. */
    double var_a = 0.0, var_b = 0.0;
    for (int i = 0; i < n; i++) {
        double da = (double)a->data[i] - mean_a;
        double db = (double)b->data[i] - mean_b;
        var_a += da * da;
        var_b += db * db;
    }

    if (var_a <= 0.0 || var_b <= 0.0) {
        *out_pcc = (var_a <= 0.0 && var_b <= 0.0) ? 1.0 : 0.0;
        return PH_SUCCESS;
    }

    const double denom = sqrt(var_a * var_b);
    double peak = -1.0;
    for (int d = 0; d < n; d++) {
        double num = 0.0;
        for (int i = 0; i < n; i++) {
            int j = (n + i - d) % n;
            num += ((double)a->data[i] - mean_a) * ((double)b->data[j] - mean_b);
        }
        double r = num / denom;
        if (r > peak)
            peak = r;
    }

    /* Rounding can carry a perfect correlation a hair past 1.0; the contract says [-1, 1]
     * and callers compare it against a threshold, so keep it there. */
    if (peak > 1.0)
        peak = 1.0;
    if (peak < -1.0)
        peak = -1.0;
    *out_pcc = peak;
    return PH_SUCCESS;
}

static const char PH_HEX_DIGITS[] = "0123456789abcdef";

PH_API ph_error_t ph_digest_to_hex(const ph_digest_t *d, char *out, size_t out_size) {
    /* size == 0 is accepted here and renders as an empty string: ph_digest_from_hex("")
     * produces such a digest, so rejecting it would break the round trip. */
    if (!ph_digest_is_valid(d) || !out)
        return PH_ERR_INVALID_ARGUMENT;

    size_t needed = (size_t)d->size * 2 + 1;
    if (out_size < needed)
        return PH_ERR_INVALID_ARGUMENT;

    for (size_t i = 0; i < d->size; i++) {
        out[i * 2] = PH_HEX_DIGITS[(d->data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = PH_HEX_DIGITS[d->data[i] & 0x0F];
    }
    out[d->size * 2] = '\0';
    return PH_SUCCESS;
}

static int ph_hex_nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

PH_API ph_error_t ph_digest_from_hex(const char *hex, ph_digest_t *out) {
    if (!hex || !out)
        return PH_ERR_INVALID_ARGUMENT;

    size_t len = strlen(hex);
    if (len % 2 != 0 || len / 2 > PH_DIGEST_MAX_BYTES)
        return PH_ERR_INVALID_ARGUMENT;

    size_t n_bytes = len / 2;
    for (size_t i = 0; i < n_bytes; i++) {
        int hi = ph_hex_nibble(hex[i * 2]);
        int lo = ph_hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return PH_ERR_INVALID_ARGUMENT;
        out->data[i] = (uint8_t)((hi << 4) | lo);
    }
    memset(out->data + n_bytes, 0, PH_DIGEST_MAX_BYTES - n_bytes);
    out->size = (uint8_t)n_bytes;
    /* Decoded from text: nothing says what the bytes mean, so nothing is claimed. */
    out->kind = (uint8_t)PH_DIGEST_KIND_UNSPECIFIED;
    memset(out->reserved, 0, sizeof(out->reserved));
    return PH_SUCCESS;
}

PH_API ph_error_t ph_hash_to_hex(uint64_t hash, char *out, size_t out_size) {
    if (!out || out_size < 17)
        return PH_ERR_INVALID_ARGUMENT;

    for (int i = 0; i < 8; i++) {
        uint8_t byte = (uint8_t)(hash >> ((7 - i) * 8));
        out[i * 2] = PH_HEX_DIGITS[(byte >> 4) & 0x0F];
        out[i * 2 + 1] = PH_HEX_DIGITS[byte & 0x0F];
    }
    out[16] = '\0';
    return PH_SUCCESS;
}
