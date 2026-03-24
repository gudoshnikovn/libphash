/*
 * test_haar.c
 *
 * Unit tests for Haar wavelet arithmetic used in wHash.
 *
 * Since haar_1d_float_dyn is static, we replicate it inline and test:
 *   - Known [a,b] pair produces correct averages/differences
 *   - Forward + inverse roundtrip recovers the original signal
 *   - Constant signal produces zero HH coefficients
 *   - PH_HAAR_SCALE value precision
 */

#include "../../include/libphash.h"
#include "../../src/internal.h"
#include "test_macros.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* =========================================================
 * Inline replica of haar_1d_float_dyn (from whash.c)
 * scale = 1/sqrt(2) ≈ 0.70710678118
 * ========================================================= */

static void haar_1d(float *data, int n, float *temp) {
    int h = n / 2;
    float inv_haar = (float)(1.0 / PH_HAAR_SCALE);
    for (int i = 0; i < h; i++) {
        temp[i] = (data[2 * i] + data[2 * i + 1]) * inv_haar;
        temp[i + h] = (data[2 * i] - data[2 * i + 1]) * inv_haar;
    }
    for (int i = 0; i < n; i++)
        data[i] = temp[i];
}

/* Inverse Haar (one level) */
static void haar_1d_inv(float *data, int n, float *temp) {
    int h = n / 2;
    float inv_haar = (float)(1.0 / PH_HAAR_SCALE);
    for (int i = 0; i < h; i++) {
        temp[2 * i] = (data[i] + data[i + h]) * inv_haar;
        temp[2 * i + 1] = (data[i] - data[i + h]) * inv_haar;
    }
    for (int i = 0; i < n; i++)
        data[i] = temp[i];
}

/* =========================================================
 * Tests
 * ========================================================= */

static void test_haar_scale_precision(void) {
    /* PH_HAAR_SCALE must equal sqrt(2) to 9 decimal places */
    ASSERT_FLOAT_EQ(sqrt(2.0), PH_HAAR_SCALE, 1e-9);
    PASS("test_haar_scale_precision");
}

static void test_haar_known_pair(void) {
    /* [4.0, 2.0] forward Haar:
     * LL = (4+2) / sqrt(2) = 6/1.41421 ≈ 4.24264
     * HH = (4-2) / sqrt(2) = 2/1.41421 ≈ 1.41421
     */
    float data[] = {4.0f, 2.0f};
    float temp[2];
    haar_1d(data, 2, temp);
    ASSERT_FLOAT_EQ(6.0 / sqrt(2.0), data[0], 1e-4); /* LL */
    ASSERT_FLOAT_EQ(2.0 / sqrt(2.0), data[1], 1e-4); /* HH */
    PASS("test_haar_known_pair");
}

static void test_haar_constant_signal_zero_hh(void) {
    /* A constant signal [c, c, c, c] must produce zero HH sub-bands.
     * LL = 2c/sqrt(2) * sqrt(N stages), HH[k] = 0 for all k. */
    float data[] = {100.0f, 100.0f, 100.0f, 100.0f};
    float temp[4];
    /* Two levels: n=4 → n=2 */
    haar_1d(data, 4, temp);
    ASSERT_FLOAT_EQ(0.0, data[2], 1e-4); /* HH1 of second pair */
    ASSERT_FLOAT_EQ(0.0, data[3], 1e-4);
    /* Apply second level to LL part */
    haar_1d(data, 2, temp);
    ASSERT_FLOAT_EQ(0.0, data[1], 1e-4); /* HH of first pair */
    PASS("test_haar_constant_signal_zero_hh");
}

static void test_haar_roundtrip_n4(void) {
    /* Forward + inverse Haar must recover original signal for n=4 */
    float original[] = {10.0f, 40.0f, 90.0f, 20.0f};
    float data[4];
    float temp[4];
    memcpy(data, original, sizeof(data));

    /* Forward: two levels */
    haar_1d(data, 4, temp);
    haar_1d(data, 2, temp);

    /* Inverse: two levels */
    haar_1d_inv(data, 2, temp);
    haar_1d_inv(data, 4, temp);

    for (int i = 0; i < 4; i++)
        ASSERT_FLOAT_EQ(original[i], data[i], 0.01f);
    PASS("test_haar_roundtrip_n4");
}

static void test_haar_roundtrip_n16(void) {
    /* Roundtrip for a 16-element signal with 4-level cascade */
    float original[16];
    for (int i = 0; i < 16; i++)
        original[i] = (float)(i * 17 % 256);

    float data[16];
    float temp[16];
    memcpy(data, original, sizeof(data));

    /* Forward cascade: 16 → 8 → 4 → 2 */
    haar_1d(data, 16, temp);
    haar_1d(data, 8, temp);
    haar_1d(data, 4, temp);
    haar_1d(data, 2, temp);

    /* Inverse cascade: 2 → 4 → 8 → 16 */
    haar_1d_inv(data, 2, temp);
    haar_1d_inv(data, 4, temp);
    haar_1d_inv(data, 8, temp);
    haar_1d_inv(data, 16, temp);

    for (int i = 0; i < 16; i++)
        ASSERT_FLOAT_EQ(original[i], data[i], 0.1f);
    PASS("test_haar_roundtrip_n16");
}

static void test_haar_energy_preserved(void) {
    /* Parsevals theorem: sum(data²) must equal sum(haar(data)²)
     * because Haar is an orthonormal transform. */
    float data[] = {30.0f, 80.0f, 150.0f, 200.0f};
    float temp[4];

    double energy_before = 0.0;
    for (int i = 0; i < 4; i++)
        energy_before += (double)data[i] * data[i];

    haar_1d(data, 4, temp);

    double energy_after = 0.0;
    for (int i = 0; i < 4; i++)
        energy_after += (double)data[i] * data[i];

    ASSERT_FLOAT_EQ(energy_before, energy_after, 0.1);
    PASS("test_haar_energy_preserved");
}

/* =========================================================
 * main
 * ========================================================= */

int main(void) {
    test_haar_scale_precision();
    test_haar_known_pair();
    test_haar_constant_signal_zero_hh();
    test_haar_roundtrip_n4();
    test_haar_roundtrip_n16();
    test_haar_energy_preserved();

    printf("\nAll Haar wavelet tests passed.\n");
    return 0;
}
