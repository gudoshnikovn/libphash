#include "libphash.h"
#include "test_macros.h"
#include "../src/internal.h"
#include <stdio.h>
#include <string.h>

void test_dct2_partial_unit() {
    const float *dct_mat = ph_get_dct_matrix_32();
    uint8_t input[32 * 32];
    float out[8 * 8];

    // Case 1: Constant input (all 128)
    memset(input, 128, sizeof(input));
    ph_dct2_partial(dct_mat, input, 32, 8, out);

    // DC component for N=32, value=128 should be (1/32) * (32*32*128) = 4096.0
    ASSERT_FLOAT_EQ(4096.0, out[0], 0.1);

    // AC components should be near zero
    for (int i = 1; i < 64; i++) {
        ASSERT_FLOAT_EQ(0.0, out[i], 0.1);
    }

    // Case 2: Symmetry check (transposed input)
    // For a simplistic symmetry check, we'll use a non-uniform input
    memset(input, 0, sizeof(input));
    for (int i = 0; i < 32; i++)
        input[i * 32] = 255; // First column is bright

    ph_dct2_partial(dct_mat, input, 32, 8, out);

    // Transpose input (first row becomes bright)
    uint8_t input_t[32 * 32];
    memset(input_t, 0, sizeof(input_t));
    for (int j = 0; j < 32; j++)
        input_t[j] = 255;

    float out_t[8 * 8];
    ph_dct2_partial(dct_mat, input_t, 32, 8, out_t);

    // The output of 2D DCT should also be transposed
    // (though for a symmetric kernel it might just be swapped in rows/cols)
    ASSERT_FLOAT_EQ(out[0], out_t[0], 0.1); // DC should be same

    PASS("test_dct2_partial_unit");
}

void test_median_bitpack_unit() {
    float values[4] = {1.0, 3.0, 2.0, 4.0};
    // sorted: 1.0, 2.0, 3.0, 4.0. n=4. n/2=2. median=3.0.
    // values > 3.0: 4.0 only (index 3). hash = 1<<3 = 8
    uint64_t hash = ph_median_bitpack(values, 4);
    ASSERT_UINT64_EQ(0x08, hash);

    float values64[64];
    for (int i = 0; i < 64; i++)
        values64[i] = (float)i;
    // median is indexed at 64/2 = 32. sorted[32] = 32.0.
    // values > 32.0 are 33..63 (31 values).
    hash = ph_median_bitpack(values64, 64);
    uint64_t expected = 0;
    for (int i = 33; i < 64; i++)
        expected |= (1ULL << i);
    ASSERT_UINT64_EQ(expected, hash);

    PASS("test_median_bitpack_unit");
}

void test_phash_e2e() {
    ph_context_t *ctx = NULL;
    uint64_t hash1, hash2;

    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, "tests/photo.jpeg"));
    ASSERT_OK(ph_compute_phash(ctx, &hash1));

    ASSERT_OK(ph_load_from_file(ctx, "tests/photo_copy.jpeg"));
    ASSERT_OK(ph_compute_phash(ctx, &hash2));
    ASSERT_UINT64_EQ(hash1, hash2);

    ph_free(ctx);
    PASS("test_phash_e2e");
}

int main() {
    test_dct2_partial_unit();
    test_median_bitpack_unit();
    test_phash_e2e();
    return 0;
}
