/*
 * test_haar.c
 *
 * The inverse Haar transform and the forward/inverse cascade, through the library's own
 * ph_haar_1d_float() and ph_haar_1d_inverse_float(). The forward transform against its
 * definition (step signal, energy preservation) is tested in test_whash.c and
 * test_formula_conformance.c; this file covers what those do not: that the inverse is
 * the inverse, one level and a whole cascade deep.
 */

#include "hashes/hashes.h"
#include "libphash.h"
#include "test_macros.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static void test_haar_scale_precision(void) {
    /* PH_HAAR_SCALE must equal sqrt(2) to 9 decimal places */
    ASSERT_FLOAT_EQ(sqrt(2.0), PH_HAAR_SCALE, 1e-9);
    PASS("test_haar_scale_precision");
}

static void test_haar_inverse_of_a_known_pair(void) {
    /* The forward transform of [4, 2] is [6/sqrt(2), 2/sqrt(2)]; the inverse takes it
     * back: (LL + HH)/sqrt(2) = 4, (LL - HH)/sqrt(2) = 2. */
    float data[] = {(float)(6.0 / sqrt(2.0)), (float)(2.0 / sqrt(2.0))};
    float temp[2];
    ph_haar_1d_inverse_float(data, 2, temp);
    ASSERT_FLOAT_EQ(4.0, data[0], 1e-5);
    ASSERT_FLOAT_EQ(2.0, data[1], 1e-5);
    PASS("test_haar_inverse_of_a_known_pair");
}

/* A full cascade -- n, n/2, ..., 2 -- forward, then back up. The tolerance is in units
 * of the 0..255 signal: each level rounds in float, so the error grows with the depth
 * but stays far below one grey level. */
static void check_cascade_roundtrip(int n, uint32_t seed) {
    float original[64], data[64], temp[64];
    ph_test_rng_t rng = ph_test_rng(seed);
    for (int i = 0; i < n; i++) {
        original[i] = (float)ph_test_rng_byte(&rng);
    }
    memcpy(data, original, sizeof(float) * (size_t)n);

    for (int len = n; len >= 2; len /= 2) {
        ph_haar_1d_float(data, len, temp);
    }
    for (int len = 2; len <= n; len *= 2) {
        ph_haar_1d_inverse_float(data, len, temp);
    }

    for (int i = 0; i < n; i++) {
        ASSERT_MSG(fabsf(data[i] - original[i]) <= 1e-3f, "n=%d: sample %d: %.6f, original %.0f", n,
                   i, (double)data[i], (double)original[i]);
    }
}

static void test_haar_cascade_roundtrip(void) {
    for (int n = 2; n <= 64; n *= 2) {
        check_cascade_roundtrip(n, 0xC0FFEEu + (uint32_t)n);
    }
    PASS("test_haar_cascade_roundtrip");
}

int main(void) {
    test_haar_scale_precision();
    test_haar_inverse_of_a_known_pair();
    test_haar_cascade_roundtrip();

    printf("\nAll Haar wavelet tests passed.\n");
    return 0;
}
