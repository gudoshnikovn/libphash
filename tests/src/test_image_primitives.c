/*
 * test_image_primitives.c
 *
 * Unit tests for low-level image processing primitives:
 *   - ph_to_grayscale
 *   - ph_apply_gamma (via LUT baked into ctx)
 *   - ph_resize_box
 *   - ph_resize_bilinear
 *   - ph_resize_mipmap
 *   - ph_apply_gaussian_blur
 *   - ph_apply_laplacian_3x3
 *
 * All tests use hand-crafted pixel arrays — no image files needed.
 */

#include "../../include/libphash.h"
#include "../../src/internal.h"
#include "test_macros.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* =========================================================
 * Helpers
 * ========================================================= */

/* Build a minimal context with default grayscale weights and gamma.
 * The context is stack-allocated — caller must NOT call ph_free(). */
static ph_context_t make_ctx(void) {
    ph_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.config.gamma = PH_DEFAULT_GAMMA;
    ctx.config.gray_r = PH_GRAY_R;
    ctx.config.gray_g = PH_GRAY_G;
    ctx.config.gray_b = PH_GRAY_B;
    /* Build gamma LUT */
    for (int i = 0; i < 256; i++) {
        double norm = i / 255.0;
        double corrected = pow(norm, 1.0 / PH_DEFAULT_GAMMA);
        ctx.config.gamma_lut[i] = (uint8_t)(corrected * 255.0 + 0.5);
    }
    return ctx;
}

/* =========================================================
 * ph_to_grayscale tests
 * ========================================================= */

static void test_gray_pure_red(void) {
    /* Expected: (255*38 + 0*75 + 0*15) >> 7 = 9690 >> 7 = 75 */
    ph_context_t ctx = make_ctx();
    uint8_t rgb[] = {255, 0, 0};
    uint8_t out[1];
    ph_to_grayscale(&ctx, rgb, 1, 1, 3, out);
    ASSERT_UINT8_EQ(75, out[0]);
    PASS("test_gray_pure_red");
}

static void test_gray_pure_green(void) {
    /* Expected: (0*38 + 255*75 + 0*15) >> 7 = 19125 >> 7 = 149 */
    ph_context_t ctx = make_ctx();
    uint8_t rgb[] = {0, 255, 0};
    uint8_t out[1];
    ph_to_grayscale(&ctx, rgb, 1, 1, 3, out);
    ASSERT_UINT8_EQ(149, out[0]);
    PASS("test_gray_pure_green");
}

static void test_gray_pure_blue(void) {
    /* Expected: (0*38 + 0*75 + 255*15) >> 7 = 3825 >> 7 = 29 */
    ph_context_t ctx = make_ctx();
    uint8_t rgb[] = {0, 0, 255};
    uint8_t out[1];
    ph_to_grayscale(&ctx, rgb, 1, 1, 3, out);
    ASSERT_UINT8_EQ(29, out[0]);
    PASS("test_gray_pure_blue");
}

static void test_gray_white(void) {
    /* Expected: (255*(38+75+15)) >> 7 = (255*128) >> 7 = 255 */
    ph_context_t ctx = make_ctx();
    uint8_t rgb[] = {255, 255, 255};
    uint8_t out[1];
    ph_to_grayscale(&ctx, rgb, 1, 1, 3, out);
    ASSERT_UINT8_EQ(255, out[0]);
    PASS("test_gray_white");
}

static void test_gray_black(void) {
    ph_context_t ctx = make_ctx();
    uint8_t rgb[] = {0, 0, 0};
    uint8_t out[1];
    ph_to_grayscale(&ctx, rgb, 1, 1, 3, out);
    ASSERT_UINT8_EQ(0, out[0]);
    PASS("test_gray_black");
}

static void test_gray_passthrough_1ch(void) {
    /* Single-channel input must be copied verbatim */
    ph_context_t ctx = make_ctx();
    uint8_t src[] = {42, 100, 200, 7};
    uint8_t out[4];
    ph_to_grayscale(&ctx, src, 4, 1, 1, out);
    for (int i = 0; i < 4; i++)
        ASSERT_UINT8_EQ(src[i], out[i]);
    PASS("test_gray_passthrough_1ch");
}

static void test_gray_rgba_ignores_alpha(void) {
    /* RGBA: formula uses only R,G,B channels (indices 0,1,2). Alpha is stride-skipped. */
    ph_context_t ctx = make_ctx();
    uint8_t rgba[] = {255, 0, 0, 128}; /* pure red with alpha=128 */
    uint8_t out[1];
    ph_to_grayscale(&ctx, rgba, 1, 1, 4, out);
    ASSERT_UINT8_EQ(75, out[0]); /* same as pure red in RGB */
    PASS("test_gray_rgba_ignores_alpha");
}

static void test_gray_multi_pixel(void) {
    /* Two pixels: red and green, verify both correct simultaneously */
    ph_context_t ctx = make_ctx();
    uint8_t rgb[] = {255, 0,   0,  /* red   → 75  */
                     0,   255, 0}; /* green → 149 */
    uint8_t out[2];
    ph_to_grayscale(&ctx, rgb, 2, 1, 3, out);
    ASSERT_UINT8_EQ(75, out[0]);
    ASSERT_UINT8_EQ(149, out[1]);
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
    /* gamma=1.0 → pow(x, 1/1.0) = x → LUT is identity */
    ph_context_t ctx = make_ctx();
    ctx.config.gamma = 1.0f;
    for (int i = 0; i < 256; i++) {
        ctx.config.gamma_lut[i] = (uint8_t)i;
    }
    uint8_t data[] = {0, 64, 128, 192, 255};
    uint8_t copy[5];
    memcpy(copy, data, 5);
    ph_apply_gamma(&ctx, data, 5, 1);
    for (int i = 0; i < 5; i++)
        ASSERT_UINT8_EQ(copy[i], data[i]);
    PASS("test_gamma_identity_lut");
}

static void test_gamma_2_2_midpoint(void) {
    /* gamma=2.2, input=128 → expected = round(pow(128/255, 1/2.2) * 255) ≈ 186 */
    ph_context_t ctx = make_ctx();
    uint8_t expected = ctx.config.gamma_lut[128];
    double computed = pow(128.0 / 255.0, 1.0 / 2.2) * 255.0;
    ASSERT_FLOAT_EQ(computed, (double)expected, 1.0);
    PASS("test_gamma_2_2_midpoint");
}

static void test_gamma_uniform_image(void) {
    /* All pixels the same value → after gamma all still the same value */
    ph_context_t ctx = make_ctx();
    uint8_t data[16];
    memset(data, 100, 16);
    ph_apply_gamma(&ctx, data, 4, 4);
    uint8_t expected = ctx.config.gamma_lut[100];
    for (int i = 0; i < 16; i++)
        ASSERT_UINT8_EQ(expected, data[i]);
    PASS("test_gamma_uniform_image");
}

static void test_gamma_zero_stays_zero(void) {
    ph_context_t ctx = make_ctx();
    uint8_t data[] = {0};
    ph_apply_gamma(&ctx, data, 1, 1);
    ASSERT_UINT8_EQ(0, data[0]);
    PASS("test_gamma_zero_stays_zero");
}

static void test_gamma_255_stays_255(void) {
    ph_context_t ctx = make_ctx();
    uint8_t data[] = {255};
    ph_apply_gamma(&ctx, data, 1, 1);
    ASSERT_UINT8_EQ(255, data[0]);
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

/* =========================================================
 * ph_resize_bilinear tests
 * ========================================================= */

static void test_bilinear_identity(void) {
    uint8_t src[] = {10, 20, 30, 40};
    uint8_t dst[4];
    ph_resize_bilinear(src, 2, 2, dst, 2, 2);
    for (int i = 0; i < 4; i++)
        ASSERT_UINT8_EQ(src[i], dst[i]);
    PASS("test_bilinear_identity");
}

static void test_bilinear_1x1_upscale(void) {
    /* 1×1 px → 3×3: all output must equal the single input pixel */
    uint8_t src[] = {128};
    uint8_t dst[9];
    ph_resize_bilinear(src, 1, 1, dst, 3, 3);
    for (int i = 0; i < 9; i++)
        ASSERT_UINT8_EQ(128, dst[i]);
    PASS("test_bilinear_1x1_upscale");
}

static void test_bilinear_interpolation_midpoint(void) {
    /* 2×1 [0, 255] → 3×1: endpoints preserved, middle ≈ 127 or 128 */
    uint8_t src[] = {0, 255};
    uint8_t dst[3];
    ph_resize_bilinear(src, 2, 1, dst, 3, 1);
    ASSERT_UINT8_EQ(0, dst[0]);
    /* Middle pixel: interpolated. Allow ±1 rounding */
    if (dst[1] < 126 || dst[1] > 129) {
        fprintf(stderr, "[FAIL] test_bilinear_interpolation_midpoint: mid=%u (expected ~127)\n",
                dst[1]);
        exit(1);
    }
    ASSERT_UINT8_EQ(255, dst[2]);
    PASS("test_bilinear_interpolation_midpoint");
}

static void test_bilinear_uniform(void) {
    /* Uniform input → uniform output regardless of size */
    uint8_t src[16];
    memset(src, 77, 16);
    uint8_t dst[25];
    ph_resize_bilinear(src, 4, 4, dst, 5, 5);
    for (int i = 0; i < 25; i++)
        ASSERT_UINT8_EQ(77, dst[i]);
    PASS("test_bilinear_uniform");
}

/* =========================================================
 * ph_resize_mipmap tests
 * ========================================================= */

static void test_mipmap_uniform(void) {
    /* Uniform image should come out uniform */
    ph_context_t ctx = make_ctx();
    ctx.arena.buffer = NULL;
    ctx.arena.capacity = 0;
    ctx.arena.offset = 0;

    uint8_t src[64];
    memset(src, 150, 64);
    uint8_t dst[4];
    ph_resize_mipmap(&ctx, src, 8, 8, dst, 2, 2);
    for (int i = 0; i < 4; i++)
        ASSERT_UINT8_EQ(150, dst[i]);
    PASS("test_mipmap_uniform");
}

static void test_mipmap_small_falls_back_to_bilinear(void) {
    /* src (3×3) <= dst*2 (4×4) → falls back to bilinear → uniform preserves value */
    ph_context_t ctx = make_ctx();
    ctx.arena.buffer = NULL;
    ctx.arena.capacity = 0;
    ctx.arena.offset = 0;

    uint8_t src[9];
    memset(src, 88, 9);
    uint8_t dst[4];
    ph_resize_mipmap(&ctx, src, 3, 3, dst, 2, 2);
    for (int i = 0; i < 4; i++)
        ASSERT_UINT8_EQ(88, dst[i]);
    PASS("test_mipmap_small_falls_back_to_bilinear");
}

/* =========================================================
 * ph_apply_gaussian_blur tests
 * ========================================================= */

static void test_gaussian_uniform(void) {
    /* Blurring a constant image must keep every pixel the same value */
    ph_context_t ctx = make_ctx();
    ctx.arena.buffer = NULL;
    ctx.arena.capacity = 0;
    ctx.arena.offset = 0;

    uint8_t src[25], dst[25];
    memset(src, 120, 25);
    ph_apply_gaussian_blur(&ctx, src, 5, 5, dst);
    for (int i = 0; i < 25; i++)
        ASSERT_UINT8_EQ(120, dst[i]);
    PASS("test_gaussian_uniform");
}

static void test_gaussian_impulse_spreads(void) {
    /* Single bright pixel in the center of a 5×5 black image.
     * After blur the center must be dimmer, and its 4 direct neighbors
     * must be brighter than the corners. */
    ph_context_t ctx = make_ctx();
    ctx.arena.buffer = NULL;
    ctx.arena.capacity = 0;
    ctx.arena.offset = 0;

    uint8_t src[25] = {0};
    src[2 * 5 + 2] = 255; /* center pixel */
    uint8_t dst[25] = {0};
    ph_apply_gaussian_blur(&ctx, src, 5, 5, dst);

    /* Center must have been dimmed (from 255) */
    if (dst[2 * 5 + 2] >= 255) {
        fprintf(stderr, "[FAIL] test_gaussian_impulse_spreads: center not reduced (got %u)\n",
                dst[2 * 5 + 2]);
        exit(1);
    }
    /* At least one direct neighbor must be non-zero */
    int neighbor_sum = dst[1 * 5 + 2] + dst[3 * 5 + 2] + dst[2 * 5 + 1] + dst[2 * 5 + 3];
    if (neighbor_sum == 0) {
        fprintf(stderr,
                "[FAIL] test_gaussian_impulse_spreads: energy did not spread to neighbors\n");
        exit(1);
    }
    PASS("test_gaussian_impulse_spreads");
}

static void test_gaussian_horizontal_kernel(void) {
    /* 1×3 image [0, 255, 0]: after horizontal pass centre → (0+510+0)/4 = 127.
     * Too small for full 2D blur (h<3), so blur copies src→dst. Just confirm no crash. */
    ph_context_t ctx = make_ctx();
    ctx.arena.buffer = NULL;
    ctx.arena.capacity = 0;
    ctx.arena.offset = 0;

    uint8_t src[] = {0, 255, 0};
    uint8_t dst[3];
    ph_apply_gaussian_blur(&ctx, src, 3, 1, dst);
    /* w=3 but h=1 < 3 → copies verbatim */
    ASSERT_UINT8_EQ(0, dst[0]);
    ASSERT_UINT8_EQ(255, dst[1]);
    ASSERT_UINT8_EQ(0, dst[2]);
    PASS("test_gaussian_horizontal_kernel");
}

static void test_gaussian_alias_safe(void) {
    /* Calling blur with src == dst must not corrupt the image
     * (the implementation uses a temp scratchpad). */
    ph_context_t ctx = make_ctx();
    ctx.arena.buffer = NULL;
    ctx.arena.capacity = 0;
    ctx.arena.offset = 0;

    uint8_t img[25];
    memset(img, 80, 25);
    /* Pass img as both src and dst */
    ph_apply_gaussian_blur(&ctx, img, 5, 5, img);
    /* Uniform image → should still be 80 */
    for (int i = 0; i < 25; i++)
        ASSERT_UINT8_EQ(80, img[i]);
    PASS("test_gaussian_alias_safe");
}

/* =========================================================
 * ph_apply_laplacian_3x3 tests
 * ========================================================= */

static void test_laplacian_uniform(void) {
    /* Uniform image: interior = 5*v - 4*v = v; borders = v (copied).
     * All pixels should stay at the same value. */
    uint8_t src[25], dst[25];
    memset(src, 50, 25);
    ph_apply_laplacian_3x3(src, 5, 5, dst);
    for (int i = 0; i < 25; i++)
        ASSERT_UINT8_EQ(50, dst[i]);
    PASS("test_laplacian_uniform");
}

static void test_laplacian_center_amplified(void) {
    /* 5×5 uniform=10 except center=200.
     * center Laplacian = 5*200 - 4*10 = 960, clamped to 255. */
    uint8_t src[25], dst[25];
    memset(src, 10, 25);
    src[2 * 5 + 2] = 200;
    ph_apply_laplacian_3x3(src, 5, 5, dst);
    ASSERT_UINT8_EQ(255, dst[2 * 5 + 2]);
    PASS("test_laplacian_center_amplified");
}

static void test_laplacian_negative_clamped(void) {
    /* Surround brighter than center: center = 5*0 - 4*200 = -800, clamp to 0. */
    uint8_t src[25], dst[25];
    memset(src, 200, 25);
    src[2 * 5 + 2] = 0;
    ph_apply_laplacian_3x3(src, 5, 5, dst);
    ASSERT_UINT8_EQ(0, dst[2 * 5 + 2]);
    PASS("test_laplacian_negative_clamped");
}

static void test_laplacian_borders_copied(void) {
    /* Border pixels must equal source border pixels verbatim */
    uint8_t src[25], dst[25];
    for (int i = 0; i < 25; i++)
        src[i] = (uint8_t)i;
    ph_apply_laplacian_3x3(src, 5, 5, dst);
    /* Top and bottom rows, left and right columns */
    for (int x = 0; x < 5; x++) {
        ASSERT_UINT8_EQ(src[0 * 5 + x], dst[0 * 5 + x]); /* top row */
        ASSERT_UINT8_EQ(src[4 * 5 + x], dst[4 * 5 + x]); /* bottom row */
    }
    for (int y = 0; y < 5; y++) {
        ASSERT_UINT8_EQ(src[y * 5 + 0], dst[y * 5 + 0]); /* left col */
        ASSERT_UINT8_EQ(src[y * 5 + 4], dst[y * 5 + 4]); /* right col */
    }
    PASS("test_laplacian_borders_copied");
}

/* =========================================================
 * main
 * ========================================================= */

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

    /* Bilinear resize */
    test_bilinear_identity();
    test_bilinear_1x1_upscale();
    test_bilinear_interpolation_midpoint();
    test_bilinear_uniform();

    /* Mipmap resize */
    test_mipmap_uniform();
    test_mipmap_small_falls_back_to_bilinear();

    /* Gaussian blur */
    test_gaussian_uniform();
    test_gaussian_impulse_spreads();
    test_gaussian_horizontal_kernel();
    test_gaussian_alias_safe();

    /* Laplacian */
    test_laplacian_uniform();
    test_laplacian_center_amplified();
    test_laplacian_negative_clamped();
    test_laplacian_borders_copied();

    printf("\nAll image primitive tests passed.\n");
    return 0;
}
