/*
 * test_image_primitives.c
 *
 * Unit tests for low-level image processing primitives:
 *   - ph_to_grayscale
 *   - ph_apply_gamma (per-image normalisation, see src/image/color.c)
 *   - ph_resize_box
 *
 * All tests use hand-crafted pixel arrays — no image files needed.
 */

#include "image/image.h"
#include "libphash.h"
#include "test_macros.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Helpers */

/* =========================================================
 * ph_to_grayscale tests
 * ========================================================= */

static void test_gray_pure_red(void) {
    /* Expected: (255*38 + 0*75 + 0*15) >> 7 = 9690 >> 7 = 75 */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    uint8_t rgb[] = {255, 0, 0};
    uint8_t out[1];
    ph_to_grayscale(ctx, rgb, 1, 1, 3, out);
    ASSERT_UINT8_EQ(75, out[0]);
    ph_free(ctx);
    PASS("test_gray_pure_red");
}

static void test_gray_pure_green(void) {
    /* Expected: (0*38 + 255*75 + 0*15) >> 7 = 19125 >> 7 = 149 */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    uint8_t rgb[] = {0, 255, 0};
    uint8_t out[1];
    ph_to_grayscale(ctx, rgb, 1, 1, 3, out);
    ASSERT_UINT8_EQ(149, out[0]);
    ph_free(ctx);
    PASS("test_gray_pure_green");
}

static void test_gray_pure_blue(void) {
    /* Expected: (0*38 + 0*75 + 255*15) >> 7 = 3825 >> 7 = 29 */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    uint8_t rgb[] = {0, 0, 255};
    uint8_t out[1];
    ph_to_grayscale(ctx, rgb, 1, 1, 3, out);
    ASSERT_UINT8_EQ(29, out[0]);
    ph_free(ctx);
    PASS("test_gray_pure_blue");
}

static void test_gray_white(void) {
    /* Expected: (255*(38+75+15)) >> 7 = (255*128) >> 7 = 255 */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    uint8_t rgb[] = {255, 255, 255};
    uint8_t out[1];
    ph_to_grayscale(ctx, rgb, 1, 1, 3, out);
    ASSERT_UINT8_EQ(255, out[0]);
    ph_free(ctx);
    PASS("test_gray_white");
}

static void test_gray_black(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    uint8_t rgb[] = {0, 0, 0};
    uint8_t out[1];
    ph_to_grayscale(ctx, rgb, 1, 1, 3, out);
    ASSERT_UINT8_EQ(0, out[0]);
    ph_free(ctx);
    PASS("test_gray_black");
}

static void test_gray_passthrough_1ch(void) {
    /* Single-channel input must be copied verbatim */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    uint8_t src[] = {42, 100, 200, 7};
    uint8_t out[4];
    ph_to_grayscale(ctx, src, 4, 1, 1, out);
    for (int i = 0; i < 4; i++)
        ASSERT_UINT8_EQ(src[i], out[i]);
    ph_free(ctx);
    PASS("test_gray_passthrough_1ch");
}

static void test_gray_rgba_ignores_alpha(void) {
    /* RGBA: formula uses only R,G,B channels (indices 0,1,2). Alpha is stride-skipped. */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    uint8_t rgba[] = {255, 0, 0, 128}; /* pure red with alpha=128 */
    uint8_t out[1];
    ph_to_grayscale(ctx, rgba, 1, 1, 4, out);
    ASSERT_UINT8_EQ(75, out[0]); /* same as pure red in RGB */
    ph_free(ctx);
    PASS("test_gray_rgba_ignores_alpha");
}

static void test_gray_multi_pixel(void) {
    /* Two pixels: red and green, verify both correct simultaneously */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    uint8_t rgb[] = {255, 0,   0,  /* red   → 75  */
                     0,   255, 0}; /* green → 149 */
    uint8_t out[2];
    ph_to_grayscale(ctx, rgb, 2, 1, 3, out);
    ASSERT_UINT8_EQ(75, out[0]);
    ASSERT_UINT8_EQ(149, out[1]);
    ph_free(ctx);
    PASS("test_gray_multi_pixel");
}

static void test_gray_null_ctx_uses_defaults(void) {
    /* ph_to_grayscale(NULL, ...) must fall back to PH_GRAY_R/G/B defaults */
    uint8_t rgb[] = {255, 0, 0};
    uint8_t out[1];
    ph_to_grayscale(NULL, rgb, 1, 1, 3, out);
    ASSERT_UINT8_EQ(75, out[0]);
    PASS("test_gray_null_ctx_uses_defaults");
}

/* =========================================================
 * ph_apply_gamma tests
 * ========================================================= */

static void test_gamma_identity_lut(void) {
    /* gamma=1.0 -> pow(v/max, 1.0)*max = v for any max > 0: an exact identity, and the
     * default, so this holds even without an explicit set_gamma() call. */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    uint8_t data[] = {0, 64, 128, 192, 255};
    uint8_t copy[5];
    memcpy(copy, data, 5);
    ph_apply_gamma(ctx, data, 5, 1);
    for (int i = 0; i < 5; i++)
        ASSERT_UINT8_EQ(copy[i], data[i]);
    ph_free(ctx);
    PASS("test_gamma_identity_lut");
}

static void test_gamma_2_2_midpoint(void) {
    /* gamma raises pixels to `gamma` directly (not `1.0/gamma`) and normalises
     * by the buffer's own maximum rather than assuming a fixed 0..255 span. With 255
     * present in the buffer, max=255 and the formula reduces to
     * expected = round(pow(128/255, 2.2) * 255) */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_context_set_gamma(ctx, 2.2f));
    uint8_t data[] = {128, 255}; /* 255 present so max=255, matching the formula below */
    ph_apply_gamma(ctx, data, 2, 1);
    double computed = pow(128.0 / 255.0, 2.2) * 255.0;
    ASSERT_FLOAT_EQ(computed, (double)data[0], 1.0);
    ASSERT_UINT8_EQ(255, data[1]); /* the max itself is always fixed under the formula */
    ph_free(ctx);
    PASS("test_gamma_2_2_midpoint");
}

static void test_gamma_uniform_image(void) {
    /* A flat buffer's only value is its own maximum, so normalised == 1 and
     * pow(1, gamma) == 1 for any gamma: a uniform image is invariant to gamma entirely,
     * not merely mapped through some non-trivial fixed point. This is the direct
     * consequence of normalising by the buffer's own maximum rather than by a
     * fixed 0..255 span, where a non-255 uniform value would have moved. */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_context_set_gamma(ctx, 2.2f));
    uint8_t data[16];
    memset(data, 100, 16);
    ph_apply_gamma(ctx, data, 4, 4);
    for (int i = 0; i < 16; i++)
        ASSERT_UINT8_EQ(100, data[i]);
    ph_free(ctx);
    PASS("test_gamma_uniform_image");
}

static void test_gamma_zero_stays_zero(void) {
    /* A single black pixel has max=0: ph_apply_gamma() treats this as nothing to
     * normalise by and leaves the buffer untouched, rather than dividing by zero. */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_context_set_gamma(ctx, 2.2f));
    uint8_t data[] = {0};
    ph_apply_gamma(ctx, data, 1, 1);
    ASSERT_UINT8_EQ(0, data[0]);
    ph_free(ctx);
    PASS("test_gamma_zero_stays_zero");
}

static void test_gamma_255_stays_255(void) {
    /* A single pixel at 255 is its own maximum: normalised == 1, pow(1, gamma) == 1,
     * rescaled by 255 again -- fixed under any gamma, not merely under the identity. */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_context_set_gamma(ctx, 2.2f));
    uint8_t data[] = {255};
    ph_apply_gamma(ctx, data, 1, 1);
    ASSERT_UINT8_EQ(255, data[0]);
    ph_free(ctx);
    PASS("test_gamma_255_stays_255");
}

/* =========================================================
 * ph_resize_box tests
 * ========================================================= */

static void test_box_uniform(void) {
    /* 4×4 uniform image → any output size stays uniform */
    uint8_t src[16];
    memset(src, 200, 16);
    uint8_t dst[4];
    ph_resize_box(src, 4, 4, dst, 2, 2);
    for (int i = 0; i < 4; i++)
        ASSERT_UINT8_EQ(200, dst[i]);
    PASS("test_box_uniform");
}

static void test_box_2x2_to_1x1_average(void) {
    /* 2×2 = [10, 30, 50, 70] → 1×1 = avg = 40 */
    uint8_t src[] = {10, 30, 50, 70};
    uint8_t dst[1];
    ph_resize_box(src, 2, 2, dst, 1, 1);
    ASSERT_UINT8_EQ(40, dst[0]);
    PASS("test_box_2x2_to_1x1_average");
}

static void test_box_4x1_to_2x1(void) {
    /* Row: [10, 30, 50, 70] → 2 outputs: avg(10,30)=20, avg(50,70)=60 */
    uint8_t src[] = {10, 30, 50, 70};
    uint8_t dst[2];
    ph_resize_box(src, 4, 1, dst, 2, 1);
    ASSERT_UINT8_EQ(20, dst[0]);
    ASSERT_UINT8_EQ(60, dst[1]);
    PASS("test_box_4x1_to_2x1");
}

static void test_box_identity(void) {
    /* Same size in = same size out → pixel-exact copy */
    uint8_t src[] = {10, 20, 30, 40};
    uint8_t dst[4];
    ph_resize_box(src, 2, 2, dst, 2, 2);
    for (int i = 0; i < 4; i++)
        ASSERT_UINT8_EQ(src[i], dst[i]);
    PASS("test_box_identity");
}

static void test_box_black_white_halves(void) {
    /* 4×1 = [0, 0, 255, 255] → 2×1 = [0, 255] */
    uint8_t src[] = {0, 0, 255, 255};
    uint8_t dst[2];
    ph_resize_box(src, 4, 1, dst, 2, 1);
    ASSERT_UINT8_EQ(0, dst[0]);
    ASSERT_UINT8_EQ(255, dst[1]);
    PASS("test_box_black_white_halves");
}

void test_resize_zero_negative_dims(void) {
    uint8_t src[4] = {1, 2, 3, 4};
    uint8_t dst[4] = {0};

    // Box
    ph_resize_box(src, 2, 2, dst, 0, 2);
    ph_resize_box(src, 2, 2, dst, 2, 0);
    ph_resize_box(src, 0, 2, dst, 2, 2);
    ph_resize_box(src, 2, 0, dst, 2, 2);

    PASS("test_resize_zero_negative_dims");
}

void test_box_resize_count_zero(void) {
    // 1x1 upsampled to 10x10 must not crash or leave the output unwritten.
    uint8_t src[1] = {255};
    uint8_t dst[100]; // Correct size for 10x10
    ph_resize_box(src, 1, 1, dst, 10, 10);
    PASS("test_box_resize_count_zero");
}

int main(void) {
    /* Grayscale */
    test_gray_pure_red();
    test_gray_pure_green();
    test_gray_pure_blue();
    test_gray_white();
    test_gray_black();
    test_gray_passthrough_1ch();
    test_gray_rgba_ignores_alpha();
    test_gray_multi_pixel();
    test_gray_null_ctx_uses_defaults();

    /* Gamma */
    test_gamma_identity_lut();
    test_gamma_2_2_midpoint();
    test_gamma_uniform_image();
    test_gamma_zero_stays_zero();
    test_gamma_255_stays_255();

    /* Box resize */
    test_box_uniform();
    test_box_2x2_to_1x1_average();
    test_box_4x1_to_2x1();
    test_box_identity();
    test_box_black_white_halves();
    test_box_resize_count_zero();

    /* General Resize Edges */
    test_resize_zero_negative_dims();

    printf("\nAll image primitive tests passed.\n");
    return 0;
}
