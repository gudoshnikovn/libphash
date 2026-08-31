#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void test_dct2_partial_unit() {
    const float *dct_mat = ph_get_dct_matrix_32();
    uint8_t input[32 * 32];
    float out[8 * 8];

    // Case 1: Constant input (all 128)
    memset(input, 128, sizeof(input));
    ASSERT_OK(ph_dct2_partial(dct_mat, input, 32, 8, out));

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

    ASSERT_OK(ph_dct2_partial(dct_mat, input, 32, 8, out));

    // Transpose input (first row becomes bright)
    uint8_t input_t[32 * 32];
    memset(input_t, 0, sizeof(input_t));
    for (int j = 0; j < 32; j++)
        input_t[j] = 255;

    float out_t[8 * 8];
    ASSERT_OK(ph_dct2_partial(dct_mat, input_t, 32, 8, out_t));

    // The output of 2D DCT should also be transposed
    // (though for a symmetric kernel it might just be swapped in rows/cols)
    ASSERT_FLOAT_EQ(out[0], out_t[0], 0.1); // DC should be same

    // Case 3 (R02): out-of-range sizes must be reported, and `out` must be left
    // untouched instead of being silently skipped.
    for (int i = 0; i < 64; i++)
        out[i] = -12345.0f;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_dct2_partial(dct_mat, input, 33, 8, out));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_dct2_partial(dct_mat, input, 32, 9, out));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_dct2_partial(dct_mat, input, 4, 8, out));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_dct2_partial(dct_mat, input, 0, 8, out));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_dct2_partial(NULL, input, 32, 8, out));
    for (int i = 0; i < 64; i++)
        ASSERT_FLOAT_EQ(-12345.0f, out[i], 0.0);

    PASS("test_dct2_partial_unit");
}

void test_median_bitpack_unit() {
    float values[4] = {1.0, 3.0, 2.0, 4.0};
    // sorted: 1.0, 2.0, 3.0, 4.0. n=4 (even). median = (sorted[1]+sorted[2])/2 = (2.0+3.0)/2 = 2.5
    // values > 2.5: 3.0 (index 1) and 4.0 (index 3). hash = (1<<1)|(1<<3) = 0xa
    uint64_t hash = ph_median_bitpack(values, 4);
    ASSERT_UINT64_EQ(0x0a, hash);

    float values64[64];
    for (int i = 0; i < 64; i++)
        values64[i] = (float)i;
    // n=64 (even). median = (sorted[31]+sorted[32])/2 = (31.0+32.0)/2 = 31.5
    // values > 31.5 are indices 32..63 (32 values).
    hash = ph_median_bitpack(values64, 64);
    uint64_t expected = 0;
    for (int i = 32; i < 64; i++)
        expected |= (1ULL << i);
    ASSERT_UINT64_EQ(expected, hash);

    PASS("test_median_bitpack_unit");
}

void test_phash_e2e() {
    ph_context_t *ctx = NULL;
    uint64_t hash1, hash2;

    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_OK(ph_compute_phash(ctx, &hash1));

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_copy.jpeg"));
    ASSERT_OK(ph_compute_phash(ctx, &hash2));
    ASSERT_UINT64_EQ(hash1, hash2);

    ph_free(ctx);
    PASS("test_phash_e2e");
}

/* R02: the public setter must reject out-of-range parameters and leave the
 * previously configured (valid) values in place. */
void test_phash_params_setter_bounds() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    /* Defaults */
    ASSERT_INT_EQ(PH_DCT_SIZE, ctx->config.phash_dct_size);
    ASSERT_INT_EQ(PH_DCT_REDUCTION_SIZE, ctx->config.phash_reduction_size);

    /* dct_size above the supported maximum -> rejected, config untouched */
    ph_context_set_phash_params(ctx, 33, 8);
    ASSERT_INT_EQ(PH_DCT_SIZE, ctx->config.phash_dct_size);
    ASSERT_INT_EQ(PH_DCT_REDUCTION_SIZE, ctx->config.phash_reduction_size);

    /* reduction_size above the supported maximum -> rejected */
    ph_context_set_phash_params(ctx, 32, 9);
    ASSERT_INT_EQ(PH_DCT_SIZE, ctx->config.phash_dct_size);
    ASSERT_INT_EQ(PH_DCT_REDUCTION_SIZE, ctx->config.phash_reduction_size);

    /* Boundary values are accepted */
    ph_context_set_phash_params(ctx, 32, 8);
    ASSERT_INT_EQ(32, ctx->config.phash_dct_size);
    ASSERT_INT_EQ(8, ctx->config.phash_reduction_size);

    /* A smaller valid pair is accepted too */
    ph_context_set_phash_params(ctx, 16, 4);
    ASSERT_INT_EQ(16, ctx->config.phash_dct_size);
    ASSERT_INT_EQ(4, ctx->config.phash_reduction_size);

    ph_free(ctx);
    PASS("test_phash_params_setter_bounds");
}

/* R02: defensive check inside ph_compute_phash(). The config is poisoned
 * directly (bypassing the setter) to emulate any other way an out-of-range
 * value could reach the hash path. Previously ph_dct2_partial() bailed out
 * silently, leaving the arena-backed dct_out buffer uninitialized, and
 * ph_compute_phash() returned PH_SUCCESS with a hash made of whatever the
 * previous algorithm left in the arena. */
void test_phash_out_of_range_config_rejected() {
    ph_context_t *ctx = NULL;
    uint64_t hash = 0xdeadbeefcafebabeULL;
    uint64_t whash = 0;

    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    /* Dirty the arena with another algorithm first. */
    ASSERT_OK(ph_compute_whash(ctx, &whash));

    ctx->config.phash_dct_size = 33;
    ctx->config.phash_reduction_size = 8;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_phash(ctx, &hash));
    ASSERT_UINT64_EQ(0xdeadbeefcafebabeULL, hash); /* digest untouched */

    ctx->config.phash_dct_size = 32;
    ctx->config.phash_reduction_size = 9;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_phash(ctx, &hash));
    ASSERT_UINT64_EQ(0xdeadbeefcafebabeULL, hash);

    /* reduction_size > dct_size is invalid as well */
    ctx->config.phash_dct_size = 4;
    ctx->config.phash_reduction_size = 8;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_phash(ctx, &hash));
    ASSERT_UINT64_EQ(0xdeadbeefcafebabeULL, hash);

    ph_free(ctx);
    PASS("test_phash_out_of_range_config_rejected");
}

/* R02: pHash at the boundary parameters must be independent of which
 * algorithms ran before it (i.e. of the arena contents). */
void test_phash_dirty_arena_determinism() {
    ph_context_t *ctx = NULL;
    uint64_t clean = 0, after_whash = 0, after_many = 0, scratch = 0;

    /* Clean context: pHash first thing after load. */
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ph_context_set_phash_params(ctx, 32, 8);
    ASSERT_OK(ph_compute_phash(ctx, &clean));
    ph_free(ctx);

    /* wHash first, then pHash. */
    ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ph_context_set_phash_params(ctx, 32, 8);
    ASSERT_OK(ph_compute_whash(ctx, &scratch));
    ASSERT_OK(ph_compute_phash(ctx, &after_whash));
    ASSERT_UINT64_EQ(clean, after_whash);
    ph_free(ctx);

    /* Several algorithms first, then pHash twice. */
    ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ph_context_set_phash_params(ctx, 32, 8);
    ASSERT_OK(ph_compute_whash(ctx, &scratch));
    ASSERT_OK(ph_compute_mhash(ctx, &scratch));
    ASSERT_OK(ph_compute_ahash(ctx, &scratch));
    ASSERT_OK(ph_compute_phash(ctx, &after_many));
    ASSERT_UINT64_EQ(clean, after_many);
    ASSERT_OK(ph_compute_phash(ctx, &after_many));
    ASSERT_UINT64_EQ(clean, after_many);
    ph_free(ctx);

    PASS("test_phash_dirty_arena_determinism");
}

int main() {
    test_dct2_partial_unit();
    test_median_bitpack_unit();
    test_phash_e2e();
    test_phash_params_setter_bounds();
    test_phash_out_of_range_config_rejected();
    test_phash_dirty_arena_determinism();
    return 0;
}
