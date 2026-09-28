#ifndef PH_DIGEST_H
#define PH_DIGEST_H

/* Validity and compatibility checks on a caller-supplied ph_digest_t, shared by the
 * comparison functions. */

#include "libphash.h"
#include <stdint.h>

/* Structural validity of a caller-supplied ph_digest_t.
 *
 * ph_digest_t is a flat public struct that FFI bindings fill in
 * by hand, and `size` is a uint8_t that can hold up to 255 while `data` is only
 * PH_DIGEST_MAX_BYTES long. Every public function that reads a digest must check
 * this first, or a size of 200 reads past the end of the array. */
static inline int ph_digest_is_valid(const ph_digest_t *d) {
    return d != NULL && d->size <= PH_DIGEST_MAX_BYTES;
}

/* As above, plus "carries information". A zero-length digest has no bits to
 * compare, so distance/similarity functions reject it rather than reporting a
 * distance of 0 -- which would read as "identical". */
static inline int ph_digest_is_comparable(const ph_digest_t *d) {
    return ph_digest_is_valid(d) && d->size > 0;
}

/* Whether a digest states this exact kind. Distinct from ph_digest_kind_allows(): that
 * one lets PH_DIGEST_KIND_UNSPECIFIED through, which is right for permitting a metric and
 * wrong for choosing between two encodings. */
static inline int ph_digest_kind_is(const ph_digest_t *d, ph_digest_kind_t kind) {
    return d != NULL && d->kind == (uint8_t)kind;
}

/* Reads one big-endian signed 16-bit feature out of a PH_DIGEST_KIND_VECTOR16 digest.
 * Assembled in unsigned arithmetic and converted at the end, because shifting a value
 * into the sign bit of an int is undefined. */
static inline int16_t ph_read_i16_be(const uint8_t *p) {
    uint16_t bits = (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
    return (int16_t)bits;
}

/* Whether a digest may be compared with a metric meant for `kind`.
 *
 * PH_DIGEST_KIND_UNSPECIFIED passes everything: it is what a hand-filled struct holds,
 * since it is zero, and an FFI binding that never learned about the tag has to keep
 * working. Anything else must match, so that Hamming distance over quantised DCT
 * coefficients -- a plausible number that means nothing -- is refused rather than
 * returned. The tag never selects a metric; it only rules one out. */
static inline int ph_digest_kind_allows(const ph_digest_t *d, ph_digest_kind_t kind) {
    return d->kind == (uint8_t)PH_DIGEST_KIND_UNSPECIFIED || d->kind == (uint8_t)kind;
}

/* Both digests valid, non-empty, of equal size, and compatible with `kind`. */
static inline int ph_digests_comparable_as(const ph_digest_t *a, const ph_digest_t *b,
                                           ph_digest_kind_t kind) {
    return ph_digest_is_comparable(a) && ph_digest_is_comparable(b) && a->size == b->size &&
           ph_digest_kind_allows(a, kind) && ph_digest_kind_allows(b, kind);
}

/* Same contract as the public ph_hamming_distance_digest(), but always the scalar path,
 * even on a build with a SIMD-capable target. Exists only so
 * tests/src/test_simd_equivalence.c can compare the two against each other; production
 * code should call the public function. */
int ph_hamming_distance_digest_scalar(const ph_digest_t *a, const ph_digest_t *b);

#endif /* PH_DIGEST_H */
