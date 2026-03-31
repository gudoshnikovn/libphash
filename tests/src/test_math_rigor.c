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
    ph_dct2_partial(dct_mat, input, n, reduce, optimized_out);

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

void test_hsv_singularities() {
    // Exactly Black Boundary (Intensity < 32)
    // Intensity = (r*299 + g*587 + b*114) / 1000
    // Try almost 32 but below: e.g. RGB = (31, 31, 31) -> Intensity = 31.0
    ph_hsv_result_t black_res = ph_hsv_classify_pixel(31.0f, 31.0f, 31.0f);
    ASSERT_INT_EQ(PH_HSV_BLACK, black_res.category);

    // Try almost 32 but slightly above: RGB = (33, 33, 33) -> Intensity 33.0, but S = 0 -> Gray
    ph_hsv_result_t gray_res = ph_hsv_classify_pixel(33.0f, 33.0f, 33.0f);
    ASSERT_INT_EQ(PH_HSV_GRAY, gray_res.category);

    // Completely White
    ph_hsv_result_t white_res = ph_hsv_classify_pixel(255.0f, 255.0f, 255.0f);
    ASSERT_INT_EQ(PH_HSV_GRAY, white_res.category);

    // Primary Colors Singularities
    ph_hsv_result_t red_res = ph_hsv_classify_pixel(255.0f, 0.0f, 0.0f);
    // h = (0-0)/255 * 42.5 = 0. Bin = 0
    ASSERT_INT_EQ(0, red_res.hue_bin);
    ASSERT_INT_EQ(PH_HSV_BRIGHT, red_res.category);

    ph_hsv_result_t green_res = ph_hsv_classify_pixel(0.0f, 255.0f, 0.0f);
    // h = (2.0 + (0-0)/255) * 42.5 = 2.0 * 42.5 = 85.0. hue_bin = 2
    ASSERT_INT_EQ(2, green_res.hue_bin);
    ASSERT_INT_EQ(PH_HSV_BRIGHT, green_res.category);

    // Note: Pure blue (0,0,255) has intensity < 32 and is classified as BLACK!
    // So we add a little R and G to pass the intensity threshold.
    ph_hsv_result_t blue_res = ph_hsv_classify_pixel(10.0f, 10.0f, 255.0f);
    // h = (4.0 + (10-10)/245) * 42.5 = 170. hue_bin = 4
    ASSERT_INT_EQ(4, blue_res.hue_bin);
    ASSERT_INT_EQ(PH_HSV_BRIGHT, blue_res.category);

    PASS("test_hsv_singularities");
}

int main() {
    test_dct_orthogonality();
    test_dct2_scalar_reference_parity();
    test_median_stability();
    test_hsv_singularities();

    printf("\nAll Math Rigor tests passed!\n");
    return 0;
}
