#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <stdlib.h>
#include <string.h>

/* Verifies that hashing raw pixels equals hashing the file they were decoded from. */
void test_load_from_pixels_matches_file() {
    ph_context_t *file_ctx = NULL;
    ASSERT_OK(ph_create(&file_ctx));
    ASSERT_OK(ph_load_from_file(file_ctx, TEST_DATA_DIR "/photo.png"));

    int w, h, ch;
    ph_context_get_dimensions(file_ctx, &w, &h, &ch);

    /* Grab a copy of the decoded pixels straight out of the context. */
    size_t n = (size_t)w * (size_t)h * (size_t)ch;
    uint8_t *pixels = malloc(n);
    ASSERT_PTR_NOT_NULL(pixels);
    memcpy(pixels, file_ctx->image.raw_rgb, n);

    uint64_t file_hash;
    ASSERT_OK(ph_compute_phash(file_ctx, &file_hash));

    ph_context_t *pixel_ctx = NULL;
    ASSERT_OK(ph_create(&pixel_ctx));
    ASSERT_OK(ph_load_from_pixels(pixel_ctx, pixels, w, h, ch, 0));

    int pw, ph_, pch;
    ph_context_get_dimensions(pixel_ctx, &pw, &ph_, &pch);
    ASSERT_INT_EQ(w, pw);
    ASSERT_INT_EQ(h, ph_);
    ASSERT_INT_EQ(ch, pch);

    uint64_t pixel_hash;
    ASSERT_OK(ph_compute_phash(pixel_ctx, &pixel_hash));

    ASSERT_INT_EQ(0, ph_hamming_distance(file_hash, pixel_hash));

    free(pixels);
    ph_free(file_ctx);
    ph_free(pixel_ctx);
    printf("test_load_from_pixels_matches_file: PASSED\n");
}

/* Verifies that a non-zero (padded) stride is handled correctly. */
void test_load_from_pixels_padded_stride() {
    int w = 4, h = 3, ch = 3;
    int stride = w * ch + 16; /* extra padding bytes per row */

    uint8_t *padded = calloc((size_t)stride * h, 1);
    ASSERT_PTR_NOT_NULL(padded);
    uint8_t *tight = malloc((size_t)w * h * ch);
    ASSERT_PTR_NOT_NULL(tight);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w * ch; x++) {
            uint8_t val = (uint8_t)((y * 37 + x * 13) & 0xFF);
            padded[y * stride + x] = val;
            tight[y * w * ch + x] = val;
        }
    }

    ph_context_t *padded_ctx = NULL;
    ASSERT_OK(ph_create(&padded_ctx));
    ASSERT_OK(ph_load_from_pixels(padded_ctx, padded, w, h, ch, stride));

    ph_context_t *tight_ctx = NULL;
    ASSERT_OK(ph_create(&tight_ctx));
    ASSERT_OK(ph_load_from_pixels(tight_ctx, tight, w, h, ch, 0));

    ASSERT_INT_EQ(0,
                  memcmp(padded_ctx->image.raw_rgb, tight_ctx->image.raw_rgb, (size_t)w * h * ch));

    free(padded);
    free(tight);
    ph_free(padded_ctx);
    ph_free(tight_ctx);
    printf("test_load_from_pixels_padded_stride: PASSED\n");
}

/* Verifies that reloading a context invalidates a previously cached grayscale buffer. */
void test_load_from_pixels_invalidates_gray_cache() {
    int w = 8, h = 8, ch = 3;
    size_t n = (size_t)w * h * ch;
    uint8_t *buf_a = malloc(n);
    uint8_t *buf_b = malloc(n);
    ASSERT_PTR_NOT_NULL(buf_a);
    ASSERT_PTR_NOT_NULL(buf_b);
    /* Distinct checkerboard patterns so the grayscale conversion differs pixel-by-pixel. */
    for (size_t i = 0; i < n; i++) {
        buf_a[i] = (uint8_t)((i % 2 == 0) ? 10 : 250);
        buf_b[i] = (uint8_t)((i % 2 == 0) ? 250 : 10);
    }

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, buf_a, w, h, ch, 0));

    uint8_t *gray_a = ph_get_gray(ctx); /* forces gray_cache to populate */
    ASSERT_PTR_NOT_NULL(gray_a);
    uint8_t saved_gray_a[64];
    memcpy(saved_gray_a, gray_a, (size_t)w * h);

    ASSERT_OK(ph_load_from_pixels(ctx, buf_b, w, h, ch, 0));
    ASSERT(ctx->image.gray_cache == NULL); /* must be invalidated by the reload */

    uint8_t *gray_b = ph_get_gray(ctx);
    ASSERT_PTR_NOT_NULL(gray_b);
    ASSERT(memcmp(saved_gray_a, gray_b, (size_t)w * h) != 0);

    free(buf_a);
    free(buf_b);
    ph_free(ctx);
    printf("test_load_from_pixels_invalidates_gray_cache: PASSED\n");
}

void test_load_from_pixels_invalid_arguments() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    uint8_t buf[3 * 4 * 4];

    ASSERT(ph_load_from_pixels(NULL, buf, 4, 4, 3, 0) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_load_from_pixels(ctx, NULL, 4, 4, 3, 0) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_load_from_pixels(ctx, buf, 0, 4, 3, 0) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_load_from_pixels(ctx, buf, 4, 0, 3, 0) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_load_from_pixels(ctx, buf, -1, 4, 3, 0) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_load_from_pixels(ctx, buf, 4, 4, 2, 0) ==
           PH_ERR_INVALID_ARGUMENT); /* unsupported channels */
    ASSERT(ph_load_from_pixels(ctx, buf, 4, 4, 3, 5) ==
           PH_ERR_INVALID_ARGUMENT); /* stride < width*channels */
    ASSERT(ph_load_from_pixels(ctx, buf, 4, 4, 3, -1) == PH_ERR_INVALID_ARGUMENT);

    ph_free(ctx);
    printf("test_load_from_pixels_invalid_arguments: PASSED\n");
}

void test_load_from_pixels_single_channel() {
    int w = 5, h = 5, ch = 1;
    uint8_t *buf = malloc((size_t)w * h);
    ASSERT_PTR_NOT_NULL(buf);
    for (int i = 0; i < w * h; i++)
        buf[i] = (uint8_t)(i * 7);

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, buf, w, h, ch, 0));

    int pw, ph_, pch;
    ph_context_get_dimensions(ctx, &pw, &ph_, &pch);
    ASSERT_INT_EQ(w, pw);
    ASSERT_INT_EQ(h, ph_);
    ASSERT_INT_EQ(1, pch);
    ASSERT_INT_EQ(1, ph_is_loaded(ctx));

    free(buf);
    ph_free(ctx);
    printf("test_load_from_pixels_single_channel: PASSED\n");
}

int main() {
    test_load_from_pixels_matches_file();
    test_load_from_pixels_padded_stride();
    test_load_from_pixels_invalidates_gray_cache();
    test_load_from_pixels_invalid_arguments();
    test_load_from_pixels_single_channel();
    return 0;
}
