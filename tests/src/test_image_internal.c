#include "internal.h"
#include "libphash.h"
#include "loader.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_color_small_fallback(void) {
    /* RGB image = 4 pixels. Must use fallback loop. */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    uint8_t rgb[12] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    uint8_t out[4];
    ph_to_grayscale(ctx, rgb, 2, 2, 3, out);
    ASSERT_UINT8_EQ(75, out[0]);

    /* 4-channel image = 4 pixels. Must use fallback loop. */
    uint8_t rgba[16] = {255, 0, 0, 0, 0, 255, 0, 0, 0, 0, 255, 0, 255, 255, 255, 0};
    ph_to_grayscale(ctx, rgba, 2, 2, 4, out);
    ASSERT_UINT8_EQ(75, out[0]);

    ph_free(ctx);
    PASS("test_color_small_fallback");
}

void test_color_1ch_passthrough(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    uint8_t src[4] = {10, 20, 30, 40};
    uint8_t dst[4] = {0};
    ph_to_grayscale(ctx, src, 2, 2, 1, dst);
    for (int i = 0; i < 4; i++)
        ASSERT_UINT8_EQ(src[i], dst[i]);

    ctx->image.width = 2;
    ctx->image.height = 2;
    ctx->image.channels = 1;
    ctx->image.raw_rgb = src;
    ctx->image.is_loaded = 1;

    uint8_t *gray = ph_get_gray(ctx);
    if (gray != src)
        exit(1);

    ctx->image.raw_rgb = NULL;
    ph_free(ctx);
    PASS("test_color_1ch_passthrough");
}

void test_gamma_nulls(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    uint8_t data[1] = {128};

    ph_apply_gamma(NULL, data, 1, 1);
    ph_apply_gamma(ctx, NULL, 1, 1);
    ph_apply_gamma(NULL, NULL, 1, 1);

    ASSERT_UINT8_EQ(128, data[0]); // No change expected

    ph_free(ctx);
    PASS("test_gamma_nulls");
}

void test_get_gray_alloc(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Setup a 3-ch image without gray_cache
    uint8_t rgb[12] = {0};
    ctx->image.width = 2;
    ctx->image.height = 2;
    ctx->image.channels = 3;
    ctx->image.raw_rgb = malloc(12);
    ctx->image.is_loaded = 1;

    uint8_t *gray = ph_get_gray(ctx);
    ASSERT_PTR_NOT_NULL(gray);
    ASSERT_PTR_NOT_NULL(ctx->image.gray_cache);
    if (gray != ctx->image.gray_cache)
        exit(1);

    ph_free(ctx);
    PASS("test_get_gray_alloc");
}

void test_color_simd_full(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // 16 pixels (8x2) to ensure SIMD loop is taken (needs at least 8)
    uint8_t rgb[16 * 3] = {0};
    uint8_t out[16];
    ph_to_grayscale(ctx, rgb, 16, 1, 3, out);

    uint8_t rgba[16 * 4] = {0};
    ph_to_grayscale(ctx, rgba, 16, 1, 4, out);

    ph_free(ctx);
    PASS("test_color_simd_full");
}

void test_loader_exhaustion(void) {
    uint8_t junk[16] = {0};
    int w, h, ch;

    // Identification loop exhaustion
    if (ph_decode_buffer(junk, 16, &w, &h, &ch, 0, 0, NULL, NULL, 0) != NULL)
        exit(1);
    if (ph_decode_buffer(NULL, 10, &w, &h, &ch, 0, 0, NULL, NULL, 0) != NULL)
        exit(1);
    if (ph_decode_buffer(junk, 0, &w, &h, &ch, 0, 0, NULL, NULL, 0) != NULL)
        exit(1);

    ph_free_image(NULL);
    PASS("test_loader_exhaustion");
}

void test_loader_corrupted_backend(void) {
    int w, h, ch;

    // 1. Trigger mock backend (starts with DE AD)
    uint8_t mock_data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t *res = ph_decode_buffer(mock_data, 4, &w, &h, &ch, 0, 0, NULL, NULL, 0);
    if (res)
        free(res);

    // 2. Junk data (loop exhaustion)
    uint8_t junk[4] = {0, 0, 0, 0};
    ph_decode_buffer(junk, 4, &w, &h, &ch, 0, 0, NULL, NULL, 0);

    PASS("test_loader_corrupted_backend");
}

int main(void) {
    test_color_small_fallback();
    test_color_simd_full();
    test_color_1ch_passthrough();
    test_gamma_nulls();
    test_get_gray_alloc();
    test_loader_exhaustion();
    test_loader_corrupted_backend();
    printf("\nAll internal image tests passed.\n");
    return 0;
}
