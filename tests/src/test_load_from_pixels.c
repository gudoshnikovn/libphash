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

/* The acceptance criterion for stride: a padded buffer must hash exactly like the dense
 * one it was built from -- every algorithm, not just the raw pixel copy. The padding is
 * filled with a value that would be impossible to miss if a row copy ever read it,
 * and the same comparison is run for 1, 3 and 4 channels because the row length (and so
 * the amount of padding skipped) differs for each.
 *
 * `stride == 0` and `stride == width * channels` are the same request spelled two ways,
 * so both are compared against the same dense reference in one go. */
static void check_stride_matches_dense(int ch, int stride_pad, uint8_t pad_byte) {
    /* Big enough that every algorithm's downscale has real content to work with. */
    const int w = 61, h = 47; /* deliberately not a multiple of any hash grid */
    const size_t dense_row = (size_t)w * (size_t)ch;
    const int stride = (int)dense_row + stride_pad;

    uint8_t *dense = malloc(dense_row * (size_t)h);
    uint8_t *padded = malloc((size_t)stride * (size_t)h);
    ASSERT_PTR_NOT_NULL(dense);
    ASSERT_PTR_NOT_NULL(padded);
    memset(padded, pad_byte, (size_t)stride * (size_t)h);

    for (int y = 0; y < h; y++) {
        for (size_t x = 0; x < dense_row; x++) {
            /* A pattern that varies in both directions, so a row read at the wrong
             * offset shifts the image instead of reproducing it. */
            uint8_t v = (uint8_t)((y * 31 + (int)x * 17 + (int)(x % 7) * 41) & 0xFF);
            dense[(size_t)y * dense_row + x] = v;
            padded[(size_t)y * (size_t)stride + x] = v;
        }
    }

    ph_context_t *dense_ctx = NULL;
    ph_context_t *padded_ctx = NULL;
    ph_context_t *explicit_ctx = NULL;
    ASSERT_OK(ph_create(&dense_ctx));
    ASSERT_OK(ph_create(&padded_ctx));
    ASSERT_OK(ph_create(&explicit_ctx));

    ASSERT_OK(ph_load_from_pixels(dense_ctx, dense, w, h, ch, 0));
    ASSERT_OK(ph_load_from_pixels(padded_ctx, padded, w, h, ch, stride));
    /* stride spelled out explicitly for a tightly packed buffer: the documented
     * equivalent of passing 0, and the boundary of the `stride < width*channels`
     * rejection. */
    ASSERT_OK(ph_load_from_pixels(explicit_ctx, dense, w, h, ch, (int)dense_row));

    ASSERT_INT_EQ(
        0, memcmp(dense_ctx->image.raw_rgb, padded_ctx->image.raw_rgb, dense_row * (size_t)h));
    ASSERT_INT_EQ(
        0, memcmp(dense_ctx->image.raw_rgb, explicit_ctx->image.raw_rgb, dense_row * (size_t)h));

    ph_context_t *ctxs[3] = {dense_ctx, padded_ctx, explicit_ctx};
    uint64_t a[3], d[3], p[3], wh[3];
    ph_digest_t bmh[3];
    for (int i = 0; i < 3; i++) {
        ASSERT_OK(ph_compute_ahash(ctxs[i], &a[i]));
        ASSERT_OK(ph_compute_dhash(ctxs[i], &d[i]));
        ASSERT_OK(ph_compute_phash(ctxs[i], &p[i]));
        ASSERT_OK(ph_compute_whash(ctxs[i], &wh[i]));
        ASSERT_OK(ph_compute_bmh(ctxs[i], &bmh[i]));
    }
    for (int i = 1; i < 3; i++) {
        ASSERT_UINT64_EQ(a[0], a[i]);
        ASSERT_UINT64_EQ(d[0], d[i]);
        ASSERT_UINT64_EQ(p[0], p[i]);
        ASSERT_UINT64_EQ(wh[0], wh[i]);
        ASSERT_INT_EQ(bmh[0].size, bmh[i].size);
        ASSERT_INT_EQ(0, ph_hamming_distance_digest(&bmh[0], &bmh[i]));
    }

    /* The colour algorithms only exist for a colour buffer; their digest widths are
     * deliberately read from the digest rather than hardcoded, since ColorMoments'
     * width is still moving. */
    if (ch >= 3) {
        ph_digest_t cm[3], chist[3];
        for (int i = 0; i < 3; i++) {
            ASSERT_OK(ph_compute_color_moments_hash(ctxs[i], &cm[i]));
            ASSERT_OK(ph_compute_color_hash(ctxs[i], &chist[i]));
        }
        for (int i = 1; i < 3; i++) {
            ASSERT_INT_EQ(cm[0].size, cm[i].size);
            ASSERT_INT_EQ(0, memcmp(cm[0].data, cm[i].data, cm[0].size));
            ASSERT_INT_EQ(chist[0].size, chist[i].size);
            ASSERT_INT_EQ(0, memcmp(chist[0].data, chist[i].data, chist[0].size));
        }
    }

    free(dense);
    free(padded);
    ph_free(dense_ctx);
    ph_free(padded_ctx);
    ph_free(explicit_ctx);
}

void test_load_from_pixels_stride_equals_dense() {
    /* Padding bytes chosen at both extremes: 0xFF would brighten any row that leaked it,
     * 0x00 would darken it. */
    check_stride_matches_dense(1, 1, 0xFF);
    check_stride_matches_dense(1, 64, 0x00);
    check_stride_matches_dense(3, 5, 0xFF);
    check_stride_matches_dense(3, 128, 0x00);
    check_stride_matches_dense(4, 3, 0xFF);
    check_stride_matches_dense(4, 97, 0x00);
    printf("test_load_from_pixels_stride_equals_dense: PASSED\n");
}

/* A one-row image is the case where stride is never actually stepped over, and a
 * one-column one is where the padding outweighs the data. Both used to be the kind of
 * thing a row-copy loop gets wrong at the last iteration (reading a full stride past the
 * end of the buffer) -- run under ASan, this is what would catch it. */
void test_load_from_pixels_stride_degenerate_shapes() {
    struct {
        int w, h, ch;
    } shapes[] = {{1, 1, 3}, {1, 8, 3}, {8, 1, 3}, {1, 1, 1}, {2, 3, 4}};

    for (size_t s = 0; s < sizeof(shapes) / sizeof(shapes[0]); s++) {
        int w = shapes[s].w, h = shapes[s].h, ch = shapes[s].ch;
        size_t row = (size_t)w * (size_t)ch;
        int stride = (int)row + 11;

        /* Exactly (h-1)*stride + row bytes: the last row's padding is not allocated, so
         * reading it is an out-of-bounds access rather than a silently harmless one. */
        size_t exact = (size_t)(h - 1) * (size_t)stride + row;
        uint8_t *buf = malloc(exact);
        ASSERT_PTR_NOT_NULL(buf);
        for (size_t i = 0; i < exact; i++)
            buf[i] = (uint8_t)(i * 13 + 7);

        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_load_from_pixels(ctx, buf, w, h, ch, stride));

        int gw, gh, gch;
        ph_context_get_dimensions(ctx, &gw, &gh, &gch);
        ASSERT_INT_EQ(w, gw);
        ASSERT_INT_EQ(h, gh);
        ASSERT_INT_EQ(ch, gch);

        /* Every row must have come from its own stride offset. */
        for (int y = 0; y < h; y++) {
            ASSERT_INT_EQ(0, memcmp(ctx->image.raw_rgb + (size_t)y * row,
                                    buf + (size_t)y * (size_t)stride, row));
        }

        uint64_t hash = 0;
        ASSERT_OK(ph_compute_ahash(ctx, &hash));

        free(buf);
        ph_free(ctx);
    }
    printf("test_load_from_pixels_stride_degenerate_shapes: PASSED\n");
}

/* Loading raw pixels bypasses the decoder, so ph_context_set_load_grayscale() has no
 * decoder to ask: the buffer's own `channels` is what the context ends up with. A caller
 * that enabled the flag and then handed over an RGB buffer must not be told it has a
 * 1-channel image -- and the colour algorithms must still work on it. */
void test_load_from_pixels_ignores_load_grayscale() {
    int w = 16, h = 16, ch = 3;
    size_t n = (size_t)w * h * ch;
    uint8_t *buf = malloc(n);
    ASSERT_PTR_NOT_NULL(buf);
    for (size_t i = 0; i < n; i++)
        buf[i] = (uint8_t)((i * 29 + i / 3) & 0xFF);

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_context_set_load_grayscale(ctx, 1));
    ASSERT_OK(ph_load_from_pixels(ctx, buf, w, h, ch, 0));

    int gw, gh, gch;
    ph_context_get_dimensions(ctx, &gw, &gh, &gch);
    ASSERT_INT_EQ(3, gch);

    ph_digest_t d;
    ASSERT_OK(ph_compute_color_moments_hash(ctx, &d));
    ASSERT(d.size > 0);

    /* The mirror case: a 1-channel buffer has no colour statistics whatever the flag
     * says, and must be refused rather than answered with a made-up number. */
    ph_context_t *gray_ctx = NULL;
    ASSERT_OK(ph_create(&gray_ctx));
    ASSERT_OK(ph_context_set_load_grayscale(gray_ctx, 0));
    ASSERT_OK(ph_load_from_pixels(gray_ctx, buf, w, h, 1, 0));
    ASSERT_INT_EQ(PH_ERR_REQUIRES_COLOR, ph_compute_color_moments_hash(gray_ctx, &d));
    ASSERT_INT_EQ(PH_ERR_REQUIRES_COLOR, ph_compute_color_hash(gray_ctx, &d));

    free(buf);
    ph_free(ctx);
    ph_free(gray_ctx);
    printf("test_load_from_pixels_ignores_load_grayscale: PASSED\n");
}

int main() {
    test_load_from_pixels_matches_file();
    test_load_from_pixels_padded_stride();
    test_load_from_pixels_invalidates_gray_cache();
    test_load_from_pixels_invalid_arguments();
    test_load_from_pixels_single_channel();
    test_load_from_pixels_stride_equals_dense();
    test_load_from_pixels_stride_degenerate_shapes();
    test_load_from_pixels_ignores_load_grayscale();
    return 0;
}
