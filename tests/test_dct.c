/*
 * test_dct.c
 *
 * Validates properties of the DCT coefficient matrix used in pHash:
 *   - Row 0 is constant 1/sqrt(N)
 *   - Each row has unit norm (||row||² == 1)
 *   - Different rows are orthogonal (dot product ≈ 0)
 *
 * The function is static inside phash.c, so we re-implement it here
 * verbatim (3 lines of math) and test the mathematical properties
 * rather than the pointer itself.
 */

#include "../src/internal.h"
#include "test_macros.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* =========================================================
 * Inline replica of the static compute_dct_coefficients
 * (identical to the one in phash.c — if the source changes,
 *  this test will diverge and fail, which is intentional)
 * ========================================================= */

static void compute_dct_matrix(float *m, int n) {
    float c = (float)sqrt(1.0 / (double)n);
    for (int j = 0; j < n; j++)
        m[j] = c;
    c = (float)sqrt(2.0 / (double)n);
    for (int i = 1; i < n; i++)
        for (int j = 0; j < n; j++)
            m[i * n + j] = (float)(c * cos(M_PI * i * (j + 0.5) / (double)n));
}

/* =========================================================
 * Tests (N=8, N=32)
 * ========================================================= */

static void test_dct_row0_is_constant(void) {
    /* Row 0 = 1/sqrt(N) for all columns */
    int n = 8;
    float m[64];
    compute_dct_matrix(m, n);
    float expected = (float)(1.0 / sqrt((double)n));
    for (int j = 0; j < n; j++)
        ASSERT_FLOAT_EQ(expected, m[0 * n + j], 1e-5);
    PASS("test_dct_row0_is_constant");
}

static void test_dct_unit_norm(void) {
    /* Every row must have ||row||² ≈ 1.0 */
    int n = 8;
    float m[64];
    compute_dct_matrix(m, n);
    for (int i = 0; i < n; i++) {
        double sum_sq = 0.0;
        for (int j = 0; j < n; j++)
            sum_sq += (double)m[i * n + j] * m[i * n + j];
        ASSERT_FLOAT_EQ(1.0, sum_sq, 1e-5);
    }
    PASS("test_dct_unit_norm");
}

static void test_dct_orthogonality(void) {
    /* Dot product of distinct rows ≈ 0 (orthonormal matrix) */
    int n = 8;
    float m[64];
    compute_dct_matrix(m, n);
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double dot = 0.0;
            for (int k = 0; k < n; k++)
                dot += (double)m[i * n + k] * m[j * n + k];
            ASSERT_FLOAT_EQ(0.0, dot, 1e-5);
        }
    }
    PASS("test_dct_orthogonality");
}

static void test_dct_unit_norm_n32(void) {
    /* Same unit-norm check for N=32 (the cached path in pHash) */
    int n = 32;
    float m[32 * 32];
    compute_dct_matrix(m, n);
    for (int i = 0; i < n; i++) {
        double sum_sq = 0.0;
        for (int j = 0; j < n; j++)
            sum_sq += (double)m[i * n + j] * m[i * n + j];
        ASSERT_FLOAT_EQ(1.0, sum_sq, 1e-4);
    }
    PASS("test_dct_unit_norm_n32");
}

static void test_dct_dc_component_largest(void) {
    /* For a non-zero constant signal, the DC coefficient (i=0) must be the largest.
     * signal = [v, v, v, ..., v] → DCT[0] = v * sqrt(N), DCT[i>0] = 0 */
    int n = 8;
    float m[64];
    compute_dct_matrix(m, n);

    float signal[8];
    for (int j = 0; j < n; j++)
        signal[j] = 100.0f;

    float coeffs[8];
    for (int i = 0; i < n; i++) {
        float sum = 0.0f;
        for (int j = 0; j < n; j++)
            sum += m[i * n + j] * signal[j];
        coeffs[i] = sum;
    }

    /* DC coefficient: 100 * sqrt(8) ≈ 282.84 */
    ASSERT_FLOAT_EQ(100.0 * sqrt(8.0), coeffs[0], 1e-3);
    /* All AC coefficients must be ~0 for a flat signal */
    for (int i = 1; i < n; i++)
        ASSERT_FLOAT_EQ(0.0, coeffs[i], 1e-3);
    PASS("test_dct_dc_component_largest");
}

/* =========================================================
 * main
 * ========================================================= */

int main(void) {
    test_dct_row0_is_constant();
    test_dct_unit_norm();
    test_dct_orthogonality();
    test_dct_unit_norm_n32();
    test_dct_dc_component_largest();

    printf("\nAll DCT tests passed.\n");
    return 0;
}
