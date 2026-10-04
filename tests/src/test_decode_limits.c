#include "libphash.h"
#include "loader.h"
#include "safety.h"
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

/* The caller's max_pixels at its exact boundary, for every format. Each format reaches
 * the limit through its own decoder -- the dispatcher judges only PNG dimensions -- so
 * this is what runs the check inside libjpeg-turbo's, libpng's and libwebp's backends
 * in a native build, and stb_image's otherwise. */
static void check_max_pixels_boundary(const char *path) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ph_error_t err = ph_load_from_file(ctx, path);
    if (err == PH_ERR_DECODER_UNAVAILABLE) {
        printf("  %s: SKIPPED (no decoder in this build)\n", path);
        ph_free(ctx);
        return;
    }
    ASSERT_MSG(err == PH_SUCCESS, "%s: load returned %d", path, err);
    int w = 0, h = 0;
    ph_context_get_dimensions(ctx, &w, &h, NULL);
    const uint64_t pixels = (uint64_t)w * (uint64_t)h;

    ASSERT_OK(ph_context_set_max_pixels(ctx, pixels));
    err = ph_load_from_file(ctx, path);
    ASSERT_MSG(err == PH_SUCCESS, "%s: max_pixels = w*h = %llu refused with %d", path,
               (unsigned long long)pixels, err);

    ASSERT_OK(ph_context_set_max_pixels(ctx, pixels - 1));
    err = ph_load_from_file(ctx, path);
    ASSERT_MSG(err == PH_ERR_IMAGE_TOO_LARGE, "%s: max_pixels = w*h - 1 returned %d", path, err);
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));
    ph_free(ctx);
}

void test_max_pixels_boundary_every_format() {
    check_max_pixels_boundary(TEST_DATA_DIR "/photo.jpeg");
    check_max_pixels_boundary(TEST_DATA_DIR "/photo.png");
    check_max_pixels_boundary(TEST_DATA_DIR "/photo.webp");
    printf("test_max_pixels_boundary_every_format: PASSED\n");
}

/* The checks below are inside the native backends and sit behind a dispatcher check
 * that answers the same input first, so they are reached by calling the backend
 * directly. They keep the backend safe on its own: it is the one that hands the length
 * and the dimensions to the third-party decoder. */

#ifdef PH_USE_LIBJPEG_TURBO
/* jpeg_mem_src() takes the length as unsigned long, 32 bits on Windows x64. The length
 * is refused before a byte is read, so a short buffer is enough. */
static void test_jpeg_backend_refuses_long_input(void) {
    static const unsigned char soi[4] = {0xFF, 0xD8, 0xFF, 0xD9};
    int w, h, ch;
    ph_error_t err = PH_SUCCESS;
    char msg[128];
    unsigned char *px = ph_decode_jpeg_mem(soi, PH_MAX_ENCODED_SIZE + (size_t)1, &w, &h, &ch, 0, 0,
                                           PH_DECODE_SCALE_FULL, &err, msg, sizeof(msg));
    ASSERT_PTR_NULL(px);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, err);
    printf("test_jpeg_backend_refuses_long_input: PASSED\n");
}
#endif

#ifdef PH_USE_LIBPNG
static uint32_t crc32_of(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return c ^ 0xFFFFFFFFu;
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Signature, IHDR with a valid CRC, and the header of an empty IDAT: enough for libpng to
 * finish png_read_info() and report the dimensions, nothing to decode. */
static size_t build_png_stub(uint8_t out[45], uint32_t w, uint32_t h) {
    build_png_header(out, w, h);
    put_be32(out + 29, crc32_of(out + 12, 17));
    put_be32(out + 33, 0);
    memcpy(out + 37, "IDAT", 4);
    put_be32(out + 41, crc32_of(out + 37, 4));
    return 45;
}

static ph_error_t decode_png_stub(uint32_t w, uint32_t h, uint64_t max_pixels) {
    uint8_t png[45];
    size_t n = build_png_stub(png, w, h);
    int ow, oh, ch;
    ph_error_t err = PH_SUCCESS;
    char msg[128];
    unsigned char *px = ph_decode_png_mem(png, n, &ow, &oh, &ch, 0, max_pixels,
                                          PH_DECODE_SCALE_FULL, &err, msg, sizeof(msg));
    ASSERT_PTR_NULL(px);
    return err;
}

static void test_png_backend_limits(void) {
    /* The backend's own per-dimension cap, the same one the dispatcher applies first. */
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, decode_png_stub(268435456u, 1u, 0));

    /* 46340 x 46340 RGB is just under PH_MAX_SUPPORTED_PIXELS (INT_MAX), so with no limit
     * of the caller's own it passes the pixel check, and its 46340 * 3 * 46340 bytes
     * (6.4 GB) are refused by the row-buffer size check -- reachable only where size_t
     * is 32 bits. Where it is not, the stub with max_pixels one short of w*h shows the
     * header is read as far as the check right before that one. */
    const uint32_t side = 46340u;
    ASSERT((uint64_t)side * side <= PH_MAX_SUPPORTED_PIXELS);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, decode_png_stub(side, side, (uint64_t)side * side - 1));
#    if SIZE_MAX <= 0xFFFFFFFFu
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, decode_png_stub(side, side, 0));
    printf("test_png_backend_limits: PASSED (32-bit row-buffer check included)\n");
#    else
    printf("test_png_backend_limits: PASSED\n");
#    endif
}
#endif

int main() {
    test_encoded_length_limit();
    test_default_limit_rejects_bomb_from_file();
    test_default_limit_rejects_bomb_from_memory();
    test_default_limit_allows_normal_image();
    test_custom_lower_limit_rejects_normal_image();
    test_custom_higher_limit_allows_normal_image();
    test_extreme_aspect_ratio_rejected();
    test_max_pixels_boundary_every_format();
#ifdef PH_USE_LIBJPEG_TURBO
    test_jpeg_backend_refuses_long_input();
#endif
#ifdef PH_USE_LIBPNG
    test_png_backend_limits();
#endif
    printf("ALL DECODE LIMIT TESTS PASSED\n");
    return 0;
}
