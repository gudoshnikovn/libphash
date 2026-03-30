#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_core_lifecycle_and_errors(void) {
    // 1. ph_version
    const char *ver = ph_version();
    ASSERT_PTR_NOT_NULL(ver);

    // 2. ph_get_error_string
    ASSERT_STR_EQ("Success", ph_get_error_string(PH_SUCCESS));
    ASSERT_STR_EQ("Memory allocation failed", ph_get_error_string(PH_ERR_ALLOCATION_FAILED));
    ASSERT_STR_EQ("Image decoding failed", ph_get_error_string(PH_ERR_DECODE_FAILED));
    ASSERT_STR_EQ("Invalid argument", ph_get_error_string(PH_ERR_INVALID_ARGUMENT));
    ASSERT_STR_EQ("Not implemented", ph_get_error_string(PH_ERR_NOT_IMPLEMENTED));
    ASSERT_STR_EQ("Empty image (no image loaded)", ph_get_error_string(PH_ERR_EMPTY_IMAGE));
    ASSERT_STR_EQ("Unknown error", ph_get_error_string((ph_error_t)999));

    // 3. ph_create NULLj
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_create(NULL));

    // 4. ph_is_loaded edge cases
    ASSERT_INT_EQ(0, ph_is_loaded(NULL));
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    // 5. ph_context_get_dimensions NULL
    ph_context_get_dimensions(NULL, NULL, NULL, NULL);

    ph_free(ctx);
    PASS("test_core_lifecycle_and_errors");
}

void test_core_setters_happy_and_edge(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Happy Paths
    ph_context_set_gamma(ctx, 1.0f);              // Hits precompute loop
    ph_context_set_gamma(ctx, 2.2f);              // Hits precompute loop again
    ph_context_set_gray_weights(ctx, 30, 60, 10); // Hits normalization
    ph_context_set_phash_params(ctx, 64, 16);
    ph_context_set_radial_params(ctx, 60, 256);
    ph_context_set_block_params(ctx, 8);
    ph_context_set_load_grayscale(ctx, 1);
    ph_context_set_whash_mode(ctx, PH_WHASH_FULL);

    // Edge Cases (NULL or invalid)
    ph_context_set_gamma(NULL, 2.2f);
    ph_context_set_gamma(ctx, 0.0f);
    ph_context_set_gray_weights(NULL, 1, 1, 1);
    ph_context_set_gray_weights(ctx, 0, 0, 0); // Fallback path
    ph_context_set_phash_params(NULL, 32, 8);
    ph_context_set_phash_params(ctx, 0, 8);
    ph_context_set_radial_params(NULL, 40, 128);
    ph_context_set_radial_params(ctx, 0, 128);
    ph_context_set_block_params(NULL, 16);
    ph_context_set_block_params(ctx, 0);
    ph_context_set_load_grayscale(NULL, 1);
    ph_context_set_whash_mode(NULL, PH_WHASH_FULL);

    int w, h, c;
    ph_context_get_dimensions(ctx, &w, &h, &c);
    ASSERT_INT_EQ(0, w);

    ph_free(ctx);
    PASS("test_core_setters_happy_and_edge");
}

void test_core_loading_mock_success(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Memory loading SUCCESS with mock data
    uint8_t mock_data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_OK(ph_load_from_memory(ctx, mock_data, 4));
    ASSERT_INT_EQ(1, ph_is_loaded(ctx));
    ASSERT_INT_EQ(1, ctx->image.width);

    // Memory loading FAILures
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_load_from_memory(ctx, NULL, 3));
    uint8_t junk[4] = {0, 0, 0, 0};
    ASSERT_INT_EQ(PH_ERR_DECODE_FAILED, ph_load_from_memory(ctx, junk, 4));

    // File loading edge cases
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_load_from_file(ctx, NULL));
    ASSERT_INT_EQ(PH_ERR_DECODE_FAILED, ph_load_from_file(ctx, "non_existent_file.png"));

    ph_free(ctx);
    PASS("test_core_loading_mock_success");
}

void test_scratchpad_stress(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // 1. Growth through repeated calls
    uint8_t *p1 = ph_get_scratchpad(ctx, 512);
    ASSERT_PTR_NOT_NULL(p1);
    ASSERT_INT_EQ(512, (int)ctx->arena.offset);

    uint8_t *p2 = ph_get_scratchpad(ctx, 2048); // Triggers realloc/growth
    ASSERT_PTR_NOT_NULL(p2);

    // 2. Auto-trim logic
    ctx->arena.offset = 0; // Simulate end of complex operation
    // capacity is now > 2560. Requesting 100 bytes should trigger trim
    ph_get_scratchpad(ctx, 100);

    // 3. NULL/Zero paths
    ASSERT_PTR_NULL(ph_get_scratchpad(NULL, 100));
    ASSERT_PTR_NULL(ph_get_scratchpad(ctx, 0));

    ph_free(ctx);
    PASS("test_scratchpad_stress");
}

void test_hashes_extra_coverage(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    uint64_t hash;
    ph_digest_t digest;

    // --- pHash Extra ---
    // Hits non-32 dct_size (use_cache = false)
    ph_context_set_phash_params(ctx, 16, 8);
    // Mock image
    ctx->image.width = 32;
    ctx->image.height = 32;
    ctx->image.channels = 1;
    ctx->image.raw_rgb = malloc(32 * 32);
    memset(ctx->image.raw_rgb, 0x80, 32 * 32);
    ctx->image.is_loaded = 1;
    ASSERT_OK(ph_compute_phash(ctx, &hash));

    // Hits ph_dct2_partial fallback for large sizes
    ph_dct2_partial(NULL, NULL, 33, 8, NULL);
    // Hits ph_median_bitpack guards (insertion sort & boundary)
    ASSERT_UINT64_EQ(0, ph_median_bitpack(NULL, 0));
    float vals[2] = {1.0f, 0.5f};
    ph_median_bitpack(vals, 2); // Hits insertion sort while

    // Hits phash boundary (reduction > 8)
    ph_context_set_phash_params(ctx, 32, 16);
    ASSERT_OK(ph_compute_phash(ctx, &hash));

    // --- Radial Extra ---
    // Hits max_variance <= 0.001 path (uniform image)
    ASSERT_OK(ph_compute_radial_hash(ctx, &digest));
    // Hits ph_get_pixel_bilinear OOB
    ASSERT_FLOAT_EQ(-1.0f, ph_get_pixel_bilinear(ctx->image.raw_rgb, 32, 32, -1.0f, 0), 0.001);
    // Hits ph_projection_variance count = 0 (far outside OOB)
    double var = ph_projection_variance(ctx->image.raw_rgb, 32, 32, 1000, 1000, 10, 1, 0, 10);
    ASSERT_FLOAT_EQ(0.0, (float)var, 0.001);

    // --- Resize Extra ---
    uint8_t src8x8[64] = {0};
    uint8_t out[16] = {0};
    // Boundary checks
    ph_resize_bilinear(src8x8, 8, 8, out, 0, 0);
    ph_resize_box(src8x8, 8, 8, out, 0, 4);
    ph_resize_box(src8x8, 8, 8, out, 4, 0);
    ph_resize_mipmap(ctx, src8x8, 8, 8, out, 0, 4);

    // Mipmap hit
    uint8_t src16x16[256] = {0};
    ph_resize_mipmap(ctx, src16x16, 16, 16, out, 2, 2);

    ph_free(ctx);
    PASS("test_hashes_extra_coverage");
}

int main(void) {
    test_core_lifecycle_and_errors();
    test_core_setters_happy_and_edge();
    test_core_loading_mock_success();
    test_scratchpad_stress();
    test_hashes_extra_coverage();

    // Stub checks
    ph_can_use_webp();

    printf("\nAll final push tests passed.\n");
    return 0;
}
