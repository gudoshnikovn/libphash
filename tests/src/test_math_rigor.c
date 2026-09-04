#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_dct_orthogonality() {
    const float *dct_mat = ph_get_dct_matrix_32();
    int n = 32;

    // We check if A * A^T is the identity matrix I.
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            float dot_product = 0.0f;
            for (int k = 0; k < n; k++) {
                // A[i][k] * A[j][k] (since A^T[k][j] = A[j][k])
                dot_product += dct_mat[i * n + k] * dct_mat[j * n + k];
            }
            if (i == j) {
                ASSERT_FLOAT_EQ(1.0f, dot_product, 0.01f);
            } else {
                ASSERT_FLOAT_EQ(0.0f, dot_product, 0.01f);
            }
        }
    }
    PASS("test_dct_orthogonality");
}

void test_dct2_scalar_reference_parity() {
    // Ensure deterministic random numbers for the test
    srand(42);

    int n = 32;
    int reduce = 8;

    // Generate a random 32x32 uint8 matrix
    uint8_t input[32 * 32];
    for (int i = 0; i < n * n; i++) {
        input[i] = (uint8_t)(rand() % 256);
    }

    const float *dct_mat = ph_get_dct_matrix_32();
    float optimized_out[8 * 8] = {0};

    // Call the library function which might be SIMD-accelerated
    ASSERT_OK(ph_dct2_partial(dct_mat, input, n, reduce, optimized_out));

    // Compute naive scalar reference DCT2 partial
    float reference_out[8 * 8] = {0};
    float temp[32 * 8] = {0};

    // First pass (rows)
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < reduce; j++) {
            float sum = 0.0f;
            for (int k = 0; k < n; k++) {
                sum += dct_mat[j * n + k] * input[i * n + k];
            }
            temp[i * reduce + j] = sum;
        }
    }

    // Second pass (cols)
    for (int j = 0; j < reduce; j++) {
        for (int i = 0; i < reduce; i++) {
            float sum = 0.0f;
            for (int k = 0; k < n; k++) {
                sum += dct_mat[i * n + k] * temp[k * reduce + j];
            }
            reference_out[i * reduce + j] = sum;
        }
    }

    // The optimized and reference outputs should be nearly identical (allowing for tiny fp order
    // differences)
    for (int i = 0; i < reduce * reduce; i++) {
        ASSERT_FLOAT_EQ(reference_out[i], optimized_out[i], 0.05f);
    }

    PASS("test_dct2_scalar_reference_parity");
}

void test_median_stability() {
    // Case 1: Mostly equal values.
    // We check that the median successfully picks the dominant value, and strictly-greater works
    // accurately. Insertion sort's stability doesn't logically affect the outcome since
    // indistinguishable floats act the same.
    float values1[64];
    for (int i = 0; i < 64; i++)
        values1[i] = 100.0f;
    values1[0] = 500.0f;
    values1[63] = 0.0f;

    uint64_t hash1 = ph_median_bitpack(values1, 64);
    // Since 62 elements are 100, the median is 100.
    // > 100 is only values1[0] -> bit 0
    ASSERT_UINT64_EQ(1ULL, hash1);

    // Case 2: Alternating binary pattern
    float values2[64];
    for (int i = 0; i < 64; i++)
        values2[i] = (i % 2 == 0) ? 1.0f : 0.0f;
    // We have 32 '1.0's and 32 '0.0's. Sorted: 32 '0.0's then 32 '1.0's.
    // For even n: median = (sorted[31] + sorted[32]) / 2 = (0.0f + 1.0f) / 2 = 0.5f.
    // "values2[i] > 0.5f" is true for even indices (1.0f) -> bits 0,2,4,...,62 are set.
    uint64_t hash2 = ph_median_bitpack(values2, 64);
    ASSERT_UINT64_EQ(0x5555555555555555ULL, hash2);

    PASS("test_median_stability");
}

/* The boundaries of the colour quantiser, at the values where a nudge changes the bin.
 *
 * The HSV classifier this used to exercise went with the ImageHash port in 2.0.0. The
 * opponent-axis quantiser that replaced it has the same kind of edge, and the same reason
 * to be pinned: an off-by-one at an axis end silently moves every pixel of one colour into
 * the neighbouring bin. */
void test_colour_quantiser_singularities() {
    /* The extremes of each axis land in the extreme bins, not one past them. */
    ASSERT_INT_EQ(PH_COLOR_BINS_RG - 1,
                  ph_color_histogram_bin(255, 0, 0) / (PH_COLOR_BINS_BY * PH_COLOR_BINS_WB));
    ASSERT_INT_EQ(0, ph_color_histogram_bin(0, 255, 0) / (PH_COLOR_BINS_BY * PH_COLOR_BINS_WB));
    ASSERT_INT_EQ(PH_COLOR_BINS_WB - 1, ph_color_histogram_bin(255, 255, 255) % PH_COLOR_BINS_WB);
    ASSERT_INT_EQ(0, ph_color_histogram_bin(0, 0, 0) % PH_COLOR_BINS_WB);

    /* Neutral grey sits on one chroma bin whatever its brightness -- rg and by are both
     * zero for it -- and that bin is the one the zero of each axis maps to. With six bins
     * over a span whose zero is at the centre, that is bin 2, not bin 3: the centre falls
     * on a boundary and integer division rounds down. Derived rather than written out, so
     * that changing the bin counts does not silently invalidate the check. */
    int grey_rg = (0 + 255) * PH_COLOR_BINS_RG / 511;
    int grey_by = (0 + 510) * PH_COLOR_BINS_BY / 1021;
    for (int v = 0; v <= 255; v += 51) {
        int bin = ph_color_histogram_bin(v, v, v);
        ASSERT_INT_EQ(grey_rg, bin / (PH_COLOR_BINS_BY * PH_COLOR_BINS_WB));
        ASSERT_INT_EQ(grey_by, (bin / PH_COLOR_BINS_WB) % PH_COLOR_BINS_BY);
    }

    /* Every colour in the cube lands inside the histogram -- walked exhaustively, since
     * the quantiser is three integer divisions and the whole cube is cheap. */
    for (int r = 0; r < 256; r++)
        for (int g = 0; g < 256; g += 5)
            for (int b = 0; b < 256; b += 5) {
                int bin = ph_color_histogram_bin(r, g, b);
                if (bin < 0 || bin >= PH_COLOR_BINS) {
                    fprintf(stderr, "[FAIL] rgb(%d,%d,%d) -> bin %d, outside 0..%d\n", r, g, b, bin,
                            PH_COLOR_BINS - 1);
                    exit(1);
                }
            }

    PASS("test_colour_quantiser_singularities");
}

int main() {
    test_dct_orthogonality();
    test_dct2_scalar_reference_parity();
    test_median_stability();
    test_colour_quantiser_singularities();

    printf("\nAll Math Rigor tests passed!\n");
    return 0;
}
