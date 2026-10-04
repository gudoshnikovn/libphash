#include "libphash.h"
#include "test_macros.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tests/data/decode_bomb.png is a 65-byte PNG whose IHDR declares a 100000x100000
 * (1e10 pixel) image with no real pixel data behind it — a classic decompression
 * bomb. Loading it must fail fast with PH_ERR_IMAGE_TOO_LARGE, never attempt the
 * multi-gigabyte allocation implied by the header. */
#define BOMB_PATH   TEST_DATA_DIR "/decode_bomb.png"
#define NORMAL_PATH TEST_DATA_DIR "/photo.jpeg" /* 400x400 = 160000 pixels */

void test_default_limit_rejects_bomb_from_file() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    ph_error_t err = ph_load_from_file(ctx, BOMB_PATH);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, err);
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    ph_free(ctx);
    printf("test_default_limit_rejects_bomb_from_file: PASSED\n");
}

void test_default_limit_rejects_bomb_from_memory() {
    FILE *f = fopen(BOMB_PATH, "rb");
    ASSERT_PTR_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)size);
    ASSERT_PTR_NOT_NULL(buf);
    ASSERT_INT_EQ((int)size, (int)fread(buf, 1, (size_t)size, f));
    fclose(f);

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    ph_error_t err = ph_load_from_memory(ctx, buf, (size_t)size);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, err);
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    free(buf);
    ph_free(ctx);
    printf("test_default_limit_rejects_bomb_from_memory: PASSED\n");
}

void test_default_limit_allows_normal_image() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, NORMAL_PATH));
    ASSERT_INT_EQ(1, ph_is_loaded(ctx));
    ph_free(ctx);
    printf("test_default_limit_allows_normal_image: PASSED\n");
}

void test_custom_lower_limit_rejects_normal_image() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    /* photo.jpeg is 400x400 = 160000 pixels; cap below that. */
    ph_context_set_max_pixels(ctx, 100000);

    ph_error_t err = ph_load_from_file(ctx, NORMAL_PATH);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, err);
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    ph_free(ctx);
    printf("test_custom_lower_limit_rejects_normal_image: PASSED\n");
}

void test_custom_higher_limit_allows_normal_image() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    ph_context_set_max_pixels(ctx, 200000);
    ASSERT_OK(ph_load_from_file(ctx, NORMAL_PATH));
    ASSERT_INT_EQ(1, ph_is_loaded(ctx));

    ph_free(ctx);
    printf("test_custom_higher_limit_allows_normal_image: PASSED\n");
}

/* max_pixels bounds the AREA, which on its own permits an absurd aspect
 * ratio. A 268435456 x 1 PNG hits the default 256 MP limit exactly -- w*h is not
 * greater than max_pixels -- yet implies a row buffer of ~800 MB, and passing
 * max_pixels into png_set_user_limits() would raise libpng's own per-dimension
 * default of 1000000 to 268435456, telling libpng such a width is acceptable.
 *
 * The dimensions are read straight out of the IHDR, before the buffer reaches
 * libpng, so the header below needs no valid CRC or pixel data: it must be
 * rejected long before anything looks at either. */
static void build_png_header(uint8_t *out, uint32_t w, uint32_t h) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    memcpy(out, sig, 8);
    out[8] = 0;
    out[9] = 0;
    out[10] = 0;
    out[11] = 13; /* IHDR length */
    memcpy(out + 12, "IHDR", 4);
    out[16] = (uint8_t)(w >> 24);
    out[17] = (uint8_t)(w >> 16);
    out[18] = (uint8_t)(w >> 8);
    out[19] = (uint8_t)w;
    out[20] = (uint8_t)(h >> 24);
    out[21] = (uint8_t)(h >> 16);
    out[22] = (uint8_t)(h >> 8);
    out[23] = (uint8_t)h;
    out[24] = 8; /* bit depth */
    out[25] = 2; /* colour type: truecolour */
    out[26] = 0;
    out[27] = 0;
    out[28] = 0; /* compression, filter, interlace */
}

void test_extreme_aspect_ratio_rejected() {
    uint8_t hdr[29];
    ph_context_t *ctx = NULL;

    ASSERT_OK(ph_create(&ctx));

    /* No branching on the compiled-in backend: the per-dimension cap is applied by the
     * dispatcher for PNG, so libpng and stb_image builds both answer the same
     * input with PH_ERR_IMAGE_TOO_LARGE. */

    /* Exactly the default area limit, but 268435456 pixels wide. */
    build_png_header(hdr, 268435456u, 1u);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_memory(ctx, hdr, sizeof(hdr)));
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    /* Tall variant of the same shape. */
    build_png_header(hdr, 1u, 268435456u);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_memory(ctx, hdr, sizeof(hdr)));

    /* Raising max_pixels must not raise the per-dimension limit either: the dimension
     * cap is deliberate, not derived from area. */
    ph_context_set_max_pixels(ctx, 0);
    build_png_header(hdr, 268435456u, 1u);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_memory(ctx, hdr, sizeof(hdr)));

    /* A dimension just inside the limit is not rejected for being too large; it fails
     * later, on its truncated data, which is a different and honest complaint. */
    build_png_header(hdr, 1000000u, 1u);
    ASSERT(ph_load_from_memory(ctx, hdr, sizeof(hdr)) != PH_ERR_IMAGE_TOO_LARGE);

    ph_free(ctx);
    printf("test_extreme_aspect_ratio_rejected: PASSED\n");
}

/* An encoded buffer longer than INT_MAX bytes. stb_image takes the length as an int, and
 * a straight cast would make a 2 GiB + 4 KiB PNG an "unrecognized format" and wrap
 * 4 GiB + 4 KiB to 4 KiB, decoding the prefix as if it were the whole file. Such buffers
 * must be refused. The buffers come from calloc(), so the untouched
 * tail costs address space, not memory; where even that is refused (a sanitizer's
 * allocator limit, a 32-bit build) the case is skipped. */
static void check_long_buffer(const unsigned char *png, size_t png_len, size_t total,
                              ph_error_t want) {
    unsigned char *buf = (unsigned char *)calloc(total, 1);
    if (!buf) {
        printf("  %zu-byte buffer: SKIPPED (calloc refused)\n", total);
        return;
    }
    memcpy(buf, png, png_len);
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ph_error_t err = ph_load_from_memory(ctx, buf, total);
    if (err != want) {
        fprintf(stderr, "[FAIL] %zu-byte buffer holding a %zu-byte PNG: expected %d, got %d\n",
                total, png_len, (int)want, (int)err);
        exit(1);
    }
    ph_free(ctx);
    free(buf);
}

void test_encoded_length_limit() {
    FILE *f = fopen(TEST_DATA_DIR "/photo.png", "rb");
    ASSERT_PTR_NOT_NULL(f);
    unsigned char png[4096];
    size_t png_len = fread(png, 1, sizeof(png), f);
    fclose(f);
    ASSERT(png_len > 0 && png_len < sizeof(png));

    const size_t int_max = (size_t)2147483647;
    check_long_buffer(png, png_len, int_max - 16, PH_SUCCESS); /* data after IEND is fine */
    check_long_buffer(png, png_len, int_max + 4096, PH_ERR_IMAGE_TOO_LARGE);
#if SIZE_MAX > 0xFFFFFFFFu
    check_long_buffer(png, png_len, ((size_t)1 << 32) + 4096, PH_ERR_IMAGE_TOO_LARGE);
    check_long_buffer(png, png_len, (size_t)5 << 30, PH_ERR_IMAGE_TOO_LARGE);
#endif
    printf("test_encoded_length_limit: PASSED\n");
}

int main() {
    test_encoded_length_limit();
    test_default_limit_rejects_bomb_from_file();
    test_default_limit_rejects_bomb_from_memory();
    test_default_limit_allows_normal_image();
    test_custom_lower_limit_rejects_normal_image();
    test_custom_higher_limit_allows_normal_image();
    test_extreme_aspect_ratio_rejected();
    printf("ALL DECODE LIMIT TESTS PASSED\n");
    return 0;
}
