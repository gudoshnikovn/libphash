#include "digest.h"

#include <math.h>
#include <stddef.h> // For size_t
#include <stdint.h>
#include <string.h>

#if defined(_MSC_VER)
#    include <intrin.h>
#endif

/* One 64-bit popcount. GCC and Clang lower the builtin to the POPCNT instruction on x86-64
 * (the build's SSE4.2 baseline includes it) and to CNT on arm64; MSVC's intrinsic always
 * emits POPCNT on x64. */
static inline int popcount64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(x);
#elif defined(_MSC_VER) && defined(_M_X64)
    return (int)__popcnt64(x); /* at most 64 */
#else
    int count = 0;
    while (x) {
        x &= (x - 1);
        count++;
    }
    return count;
#endif
}

PH_API int ph_hamming_distance(uint64_t hash1, uint64_t hash2) { return popcount64(hash1 ^ hash2); }

/* Shared tail of ph_hamming_distance_digest(): plain byte-at-a-time XOR + popcount. Used
 * both for the bytes the word loop leaves, and standalone (over the whole digest) by
 * ph_hamming_distance_digest_scalar(), which exists only so
 * tests/src/test_simd_equivalence.c can compare the two against each other. */
static int hamming_scalar_tail(const ph_digest_t *a, const ph_digest_t *b, size_t start,
                               int total) {
    for (size_t i = start; i < a->size; i++) {
        uint8_t x = a->data[i] ^ b->data[i];
#if defined(__GNUC__) || defined(__clang__)
        total += __builtin_popcount(x);
#else
        while (x) {
            x &= (x - 1);
            total++;
        }
#endif
    }
    return total;
}

int ph_hamming_distance_digest_scalar(const ph_digest_t *a, const ph_digest_t *b) {
    if (!ph_digests_comparable_as(a, b, PH_DIGEST_KIND_BITS)) {
        return -1;
    }
    return hamming_scalar_tail(a, b, 0, 0);
}

/* Eight bytes at a time, then the byte tail. The words are read with memcpy(): the digest's
 * bytes are neither aligned nor typed for a uint64_t load, and the compiler turns the copy
 * into a plain load. Hand-written vector paths (AVX2, NEON) would buy nothing here: a digest
 * is at most 128 bytes, the loop is already vectorised by the compiler where that pays, and
 * measured on arm64 a NEON version was within a nanosecond either way. */
PH_API int ph_hamming_distance_digest(const ph_digest_t *a, const ph_digest_t *b) {
    if (!ph_digests_comparable_as(a, b, PH_DIGEST_KIND_BITS)) {
        return -1;
    }

    size_t len = a->size;
    int total = 0;
    size_t i = 0;
    for (; i + 8 <= len; i += 8) {
        uint64_t wa, wb;
        memcpy(&wa, &a->data[i], sizeof(wa));
        memcpy(&wb, &b->data[i], sizeof(wb));
        total += popcount64(wa ^ wb);
    }
    return hamming_scalar_tail(a, b, i, total);
}

/* Euclidean distance over a feature vector, in the features' own units.
 *
 * Two encodings reach here. PH_DIGEST_KIND_VECTOR is one unsigned byte per feature.
 * PH_DIGEST_KIND_VECTOR16 is one signed 16-bit big-endian fixed-point number per feature,
 * in units of 1/PH_VECTOR16_SCALE -- which is what ColorMoments emits, because
 * the third moment has a sign. The two must not be conflated: reading a 16-bit vector as
 * bytes treats each feature's high and low halves as separate features, so a difference of
 * one level in the high byte and one of 1/128 in the low byte would count the same.
 *
 * A digest tagged PH_DIGEST_KIND_UNSPECIFIED -- what a hand-filled FFI struct holds -- is
 * read as bytes. It is only when one
 * side says VECTOR16 that the pairs are decoded, and then the other side must agree or be
 * unspecified. */
PH_API double ph_l2_distance(const ph_digest_t *a, const ph_digest_t *b) {
    if (ph_digest_kind_is(a, PH_DIGEST_KIND_VECTOR16) ||
        ph_digest_kind_is(b, PH_DIGEST_KIND_VECTOR16)) {
        if (!ph_digests_comparable_as(a, b, PH_DIGEST_KIND_VECTOR16)) {
            return -1.0;
        }
        /* Half a feature is not a feature: an odd length is a malformed digest. */
        if (a->size % 2 != 0) {
            return -1.0;
        }

        double sum = 0;
        for (int i = 0; i + 1 < a->size; i += 2) {
            double va = ph_read_i16_be(&a->data[i]) / (double)PH_VECTOR16_SCALE;
            double vb = ph_read_i16_be(&b->data[i]) / (double)PH_VECTOR16_SCALE;
            double diff = va - vb;
            sum += diff * diff;
        }
        return sqrt(sum);
    }

    if (!ph_digests_comparable_as(a, b, PH_DIGEST_KIND_VECTOR)) {
        return -1.0;
    }

    double sum = 0;
    for (int i = 0; i < a->size; i++) {
        double diff = a->data[i] - b->data[i];
        sum += diff * diff;
    }
    return sqrt(sum);
}

PH_API double ph_similarity(uint64_t a, uint64_t b) {
    int dist = ph_hamming_distance(a, b);
    return 1.0 - (dist / 64.0);
}

PH_API double ph_similarity_digest(const ph_digest_t *a, const ph_digest_t *b) {
    if (!ph_digests_comparable_as(a, b, PH_DIGEST_KIND_BITS)) {
        return -1.0;
    }

    int dist = ph_hamming_distance_digest(a, b);
    if (dist < 0) {
        return -1.0;
    }

    double total_bits = a->size * 8.0;
    return 1.0 - (dist / total_bits);
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
 * correlation is defined against it. ph_compute_radial_hash() emits one (all zeroes) for
 * an image with no angular structure, and never otherwise: a real digest is quantised
 * onto 0..255 by its own minimum and maximum. Any score for such a pair would be
 * invented -- 1.0 would declare two unrelated faint or flat images a perfect match -- so
 * the answer is PH_ERR_NO_STRUCTURE, and the caller decides what "no data" means.
 */
PH_API ph_error_t ph_radial_similarity(const ph_digest_t *a, const ph_digest_t *b,
                                       double *out_pcc) {
    if (!out_pcc || !ph_digests_comparable_as(a, b, PH_DIGEST_KIND_COEFFICIENTS)) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    const int n = a->size;
    double sum_a = 0.0, sum_b = 0.0;
    for (int i = 0; i < n; i++) {
        sum_a += a->data[i];
        sum_b += b->data[i];
    }
    const double mean_a = sum_a / n;
    const double mean_b = sum_b / n;

    /* The denominators do not depend on the shift: a cyclic shift permutes the terms of
     * each sum of squares without changing it. pHash recomputes them inside the shift
     * loop; the result is identical and this way the loop is O(n^2) multiplications
     * rather than three times that. */
    double var_a = 0.0, var_b = 0.0;
    for (int i = 0; i < n; i++) {
        double da = a->data[i] - mean_a;
        double db = b->data[i] - mean_b;
        var_a += da * da;
        var_b += db * db;
    }

    if (var_a <= 0.0 || var_b <= 0.0) {
        return PH_ERR_NO_STRUCTURE;
    }

    const double denom = sqrt(var_a * var_b);
    double peak = -1.0;
    for (int d = 0; d < n; d++) {
        double num = 0.0;
        for (int i = 0; i < n; i++) {
            int j = (n + i - d) % n;
            num += (a->data[i] - mean_a) * (b->data[j] - mean_b);
        }
        double r = num / denom;
        if (r > peak) {
            peak = r;
        }
    }

    /* Rounding can carry a perfect correlation a hair past 1.0; the contract says [-1, 1]
     * and callers compare it against a threshold, so keep it there. */
    if (peak > 1.0) {
        peak = 1.0;
    }
    if (peak < -1.0) {
        peak = -1.0;
    }
    *out_pcc = peak;
    return PH_SUCCESS;
}

static const char PH_HEX_DIGITS[] = "0123456789abcdef";

/* The text form of a digest is "<kind>:<hex>". The kind names are the lowercase suffixes
 * of the ph_digest_kind_t enumerators, indexed by value, so a binding can derive them
 * from the header mechanically. */
static const char *const PH_DIGEST_KIND_NAMES[] = {
    "unspecified", "bits", "coefficients", "vector", "histogram", "vector16",
};
#define PH_DIGEST_KIND_COUNT (sizeof(PH_DIGEST_KIND_NAMES) / sizeof(*PH_DIGEST_KIND_NAMES))

_Static_assert(PH_DIGEST_KIND_COUNT == PH_DIGEST_KIND_VECTOR16 + 1,
               "every digest kind needs a name in its text form");
_Static_assert(sizeof("coefficients:") - 1 + PH_DIGEST_MAX_BYTES * 2 + 1 ==
                   PH_DIGEST_HEX_BUFFER_SIZE,
               "PH_DIGEST_HEX_BUFFER_SIZE must fit the longest kind name");

PH_API ph_error_t ph_digest_to_hex(const ph_digest_t *d, char *out, size_t out_size) {
    /* size == 0 is accepted here and renders as a bare prefix: ph_digest_from_hex() of
     * that produces such a digest, so rejecting it would break the round trip. */
    if (!ph_digest_is_valid(d) || !out || d->kind >= PH_DIGEST_KIND_COUNT) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    const char *name = PH_DIGEST_KIND_NAMES[d->kind];
    size_t name_len = strlen(name);
    size_t needed = name_len + 1 + d->size * 2u + 1;
    if (out_size < needed) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    memcpy(out, name, name_len);
    out[name_len] = ':';
    char *hex = out + name_len + 1;
    for (size_t i = 0; i < d->size; i++) {
        hex[i * 2] = PH_HEX_DIGITS[(d->data[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = PH_HEX_DIGITS[d->data[i] & 0x0F];
    }
    hex[d->size * 2] = '\0';
    return PH_SUCCESS;
}

static int ph_hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

PH_API ph_error_t ph_digest_from_hex(const char *text, ph_digest_t *out) {
    if (!text || !out) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    const char *colon = strchr(text, ':');
    if (!colon) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    size_t name_len = (size_t)(colon - text); /* colon is within text */
    size_t kind = 0;
    while (kind < PH_DIGEST_KIND_COUNT &&
           !(strlen(PH_DIGEST_KIND_NAMES[kind]) == name_len &&
             memcmp(PH_DIGEST_KIND_NAMES[kind], text, name_len) == 0)) {
        kind++;
    }
    if (kind == PH_DIGEST_KIND_COUNT) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    const char *hex = colon + 1;
    size_t len = strlen(hex);
    if (len % 2 != 0 || len / 2 > PH_DIGEST_MAX_BYTES) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    /* Decode into a local first: a malformed string must leave `out` untouched. */
    ph_digest_t decoded;
    size_t n_bytes = len / 2;
    for (size_t i = 0; i < n_bytes; i++) {
        int hi = ph_hex_nibble(hex[i * 2]);
        int lo = ph_hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return PH_ERR_INVALID_ARGUMENT;
        }
        decoded.data[i] = (uint8_t)((hi << 4) | lo);
    }
    memset(decoded.data + n_bytes, 0, PH_DIGEST_MAX_BYTES - n_bytes);
    decoded.size = (uint8_t)n_bytes;
    decoded.kind = (uint8_t)kind;
    memset(decoded.reserved, 0, sizeof(decoded.reserved));
    *out = decoded;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_hash_to_hex(uint64_t hash, char *out, size_t out_size) {
    if (!out || out_size < 17) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    for (int i = 0; i < 8; i++) {
        uint8_t byte = (uint8_t)(hash >> ((7 - i) * 8));
        out[i * 2] = PH_HEX_DIGITS[(byte >> 4) & 0x0F];
        out[i * 2 + 1] = PH_HEX_DIGITS[byte & 0x0F];
    }
    out[16] = '\0';
    return PH_SUCCESS;
}

PH_API ph_error_t ph_hash_from_hex(const char *hex, uint64_t *out) {
    if (!hex || !out) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    uint64_t value = 0;
    for (int i = 0; i < 16; i++) {
        int nibble = ph_hex_nibble(hex[i]); /* also stops at a NUL before the 16th digit */
        if (nibble < 0) {
            return PH_ERR_INVALID_ARGUMENT;
        }
        value = (value << 4) | (uint64_t)nibble;
    }
    if (hex[16] != '\0') {
        return PH_ERR_INVALID_ARGUMENT;
    }
    *out = value;
    return PH_SUCCESS;
}
