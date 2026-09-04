#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The operator, against its definition.
 *
 * Marr and Hildreth's Laplacian of Gaussian, as pHash parameterises it: a square kernel
 * of side 2*sigma+1 with sigma = 4*alpha^level, whose element at (x, y) is
 * (2 - A) * exp(-A/2), A being the squared distance from the centre scaled by
 * alpha^-level. Checked element by element rather than by a spot value. */
void test_mh_kernel_matches_the_definition() {
    float kernel[65 * 65];
    int side = ph_mh_kernel(PH_MH_ALPHA, PH_MH_LEVEL, kernel, 65);

    /* alpha = 2, level = 1 -> sigma = 8 -> 17x17. */
    ASSERT_INT_EQ(17, side);

    int sigma = side / 2;
    double scale = pow((double)PH_MH_ALPHA, -(double)PH_MH_LEVEL);
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            double xp = scale * (x - sigma);
            double yp = scale * (y - sigma);
            double a = xp * xp + yp * yp;
            double want = (2.0 - a) * exp(-a / 2.0);
            ASSERT_FLOAT_EQ(want, kernel[y * side + x], 1e-5);
        }
    }

    /* The shape that makes it a Mexican hat rather than a blur: positive at the centre,
     * negative in a surrounding ring. */
    ASSERT(kernel[sigma * side + sigma] > 1.9f);
    ASSERT(kernel[sigma * side + (sigma + 3)] < 0.0f);

    /* A bad request is refused rather than truncated. */
    ASSERT_INT_EQ(0, ph_mh_kernel(PH_MH_ALPHA, PH_MH_LEVEL, kernel, 8));
    ASSERT_INT_EQ(0, ph_mh_kernel(0.0f, PH_MH_LEVEL, kernel, 65));

    PASS("test_mh_kernel_matches_the_definition");
}

/* Histogram equalisation, on a distribution whose answer can be worked out by hand. */
void test_equalize_histogram_unit() {
    /* Four values, one quarter of the pixels each. After equalisation the lowest maps to
     * 0 and the highest to 255, with the two in between evenly spread. */
    uint8_t data[8] = {10, 10, 40, 40, 90, 90, 200, 200};
    ph_equalize_histogram(data, 8, 256);
    ASSERT_INT_EQ(0, data[0]);
    ASSERT_INT_EQ(255, data[7]);
    ASSERT(data[2] > data[0] && data[4] > data[2] && data[6] > data[4]);

    /* A flat image has nothing to stretch and must not become noise or divide by zero. */
    uint8_t flat[8];
    memset(flat, 77, sizeof(flat));
    ph_equalize_histogram(flat, 8, 256);
    for (int i = 0; i < 8; i++)
        ASSERT_INT_EQ(flat[0], flat[i]);

    PASS("test_equalize_histogram_unit");
}

void test_gaussian_blur_sigma_unit() {
    /* A flat field stays flat: the kernel is renormalised to sum to one. */
    const int w = 16, h = 16;
    uint8_t src[16 * 16], dst[16 * 16];
    float scratch[16 * 16];
    memset(src, 123, sizeof(src));
    ph_gaussian_blur_sigma(src, w, h, 1.0f, scratch, dst);
    for (int i = 0; i < w * h; i++)
        ASSERT_INT_EQ(123, dst[i]);

    /* An impulse spreads, and its peak drops. */
    memset(src, 0, sizeof(src));
    src[8 * w + 8] = 255;
    ph_gaussian_blur_sigma(src, w, h, 1.0f, scratch, dst);
    ASSERT(dst[8 * w + 8] < 255);
    ASSERT(dst[8 * w + 8] > dst[8 * w + 9]);
    ASSERT(dst[8 * w + 9] > 0);
    /* Symmetric about the impulse. */
    ASSERT_INT_EQ(dst[8 * w + 7], dst[8 * w + 9]);
    ASSERT_INT_EQ(dst[7 * w + 8], dst[9 * w + 8]);

    PASS("test_gaussian_blur_sigma_unit");
}

/* The fast block sums against the definition they stand for.
 *
 * ph_mh_block_sums() folds the 16x16 block sum into the kernel and evaluates it through an
 * integral image. This computes the same quantity the slow way -- correlate every pixel,
 * then add the block up -- in double precision, and requires the two to agree.
 *
 * The tolerance is relative, because the values run to millions. Note that the direct
 * evaluation is the one that has to be done carefully: in float it loses several digits,
 * since the LoG kernel sums to nearly zero and the products cancel. That is not a
 * hypothetical -- computing the hash that way measured a separability of 1.81 against 2.49
 * for this one. */
static void test_mh_block_sums_match_the_direct_definition() {
    const int n = 128; /* smaller than the hash's 512: this is about the arithmetic */
    uint8_t *img = (uint8_t *)malloc((size_t)n * n);
    ASSERT_PTR_NOT_NULL(img);
    uint32_t state = 0x1234567u;
    for (int i = 0; i < n * n; i++) {
        state = state * 1664525u + 1013904223u;
        img[i] = (uint8_t)(state >> 24);
    }

    float kernel[65 * 65];
    int side = ph_mh_kernel(PH_MH_ALPHA, PH_MH_LEVEL, kernel, PH_MH_MAX_KERNEL_SIDE);
    ASSERT_INT_EQ(17, side);
    int half = side / 2;

    /* The grid is always PH_MH_GRID square, so the block size follows from the image --
     * the same rule ph_compute_mhash() uses. */
    int block = n / PH_MH_GRID;

    uint8_t *scratch = (uint8_t *)malloc(ph_mh_block_sums_scratch(n, half));
    ASSERT_PTR_NOT_NULL(scratch);
    float fast[PH_MH_GRID * PH_MH_GRID];
    ph_mh_block_sums(img, n, block, kernel, side, scratch, fast);

    for (int by = 0; by < PH_MH_GRID; by++) {
        for (int bx = 0; bx < PH_MH_GRID; bx++) {
            double want = 0.0;
            for (int y = by * block; y < (by + 1) * block; y++) {
                for (int x = bx * block; x < (bx + 1) * block; x++) {
                    double acc = 0.0;
                    for (int ky = 0; ky < side; ky++) {
                        int sy = y + ky - half;
                        if (sy < 0)
                            sy = 0;
                        if (sy >= n)
                            sy = n - 1;
                        for (int kx = 0; kx < side; kx++) {
                            int sx = x + kx - half;
                            if (sx < 0)
                                sx = 0;
                            if (sx >= n)
                                sx = n - 1;
                            acc += (double)kernel[ky * side + kx] * (double)img[sy * n + sx];
                        }
                    }
                    want += acc;
                }
            }
            double got = (double)fast[by * PH_MH_GRID + bx];
            double scale = fabs(want) > 1.0 ? fabs(want) : 1.0;
            if (fabs(got - want) / scale > 1e-5) {
                fprintf(stderr, "[FAIL] block (%d,%d): folded %.6f, direct definition %.6f\n", bx,
                        by, got, want);
                exit(1);
            }
        }
    }

    free(scratch);
    free(img);
    PASS("test_mh_block_sums_match_the_direct_definition");
}

void test_mhash_e2e() {
    ph_context_t *ctx = NULL;
    ph_digest_t orig, copy, mod;

    ASSERT_OK(ph_create(&ctx));

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_OK(ph_compute_mhash(ctx, &orig));
    ASSERT_INT_EQ(PH_MH_BYTES, orig.size);
    ASSERT_INT_EQ(72, orig.size);
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_BITS, orig.kind);

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_copy.jpeg"));
    ASSERT_OK(ph_compute_mhash(ctx, &copy));
    ASSERT_INT_EQ(0, ph_hamming_distance_digest(&orig, &copy));

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_color_changed.jpeg"));
    ASSERT_OK(ph_compute_mhash(ctx, &mod));

    /* An edge descriptor should barely notice a colour shift. Measured at 2.0.0: 14 of
     * 576 bits, i.e. 4.3%. The old 64-bit hash allowed 12 of 64, i.e. 19%, so the bound
     * here is tighter in relative terms and is set from the measurement. */
    int dist = ph_hamming_distance_digest(&orig, &mod);
    ASSERT(dist >= 0);
    if (dist > 58) { /* 10% of 576 */
        fprintf(stderr, "mHash too sensitive to a colour change: %d of %d bits\n", dist,
                orig.size * 8);
        exit(1);
    }

    /* Not a constant hash: an unrelated image is far away. */
    ph_digest_t other;
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_complex.png"));
    ASSERT_OK(ph_compute_mhash(ctx, &other));
    int far = ph_hamming_distance_digest(&orig, &other);
    if (far <= dist * 4) {
        fprintf(stderr, "mHash does not separate: colour shift %d, unrelated image %d\n", dist,
                far);
        exit(1);
    }

    printf("  mHash: colour shift %d bits, unrelated image %d bits, of %d\n", dist, far,
           orig.size * 8);

    ph_free(ctx);
    PASS("test_mhash_e2e");
}

/* A flat image has no edges, so the response is flat, every window equals its own mean,
 * and nothing clears the threshold. Worth pinning: it is the one input where the
 * normalisation step would divide by zero. */
void test_mhash_on_a_flat_image() {
    uint8_t *px = (uint8_t *)malloc(64 * 64 * 3);
    ASSERT_PTR_NOT_NULL(px);
    memset(px, 200, (size_t)64 * 64 * 3);

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, px, 64, 64, 3, 0));

    ph_digest_t d;
    ASSERT_OK(ph_compute_mhash(ctx, &d));
    ASSERT_INT_EQ(PH_MH_BYTES, d.size);
    for (int i = 0; i < d.size; i++)
        ASSERT_INT_EQ(0, d.data[i]);

    ph_free(ctx);
    free(px);
    PASS("test_mhash_on_a_flat_image");
}

void test_mhash_params_setter() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    /* The defaults are the reference implementation's. */
    ASSERT_FLOAT_EQ(2.0, ctx->config.mhash_alpha, 1e-9);
    ASSERT_FLOAT_EQ(1.0, ctx->config.mhash_level, 1e-9);
    ASSERT_INT_EQ(512, ctx->config.mhash_size);

    ASSERT_OK(ph_context_set_mhash_params(ctx, 2.0f, 2.0f, 256));
    ASSERT_INT_EQ(256, ctx->config.mhash_size);

    /* Rejected values leave the configuration exactly as it was. */
    const struct {
        float a, l;
        int n;
    } bad[] = {
        {1.0f, 1.0f, 256},  /* alpha must be > 1 */
        {2.0f, -1.0f, 256}, /* level must be >= 0 */
        {2.0f, 1.0f, 61},   /* below two pixels per block of the 31x31 grid */
        {2.0f, 1.0f, 4097}, /* above the cap */
        {2.0f, 4.0f, 256},  /* kernel side 2*64+1 = 129, past the buffer */
        {0.0f / 1.0f, 1.0f, 256},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                      ph_context_set_mhash_params(ctx, bad[i].a, bad[i].l, bad[i].n));
        ASSERT_FLOAT_EQ(2.0, ctx->config.mhash_alpha, 1e-9);
        ASSERT_FLOAT_EQ(2.0, ctx->config.mhash_level, 1e-9);
        ASSERT_INT_EQ(256, ctx->config.mhash_size);
    }
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_mhash_params(NULL, 2.0f, 1.0f, 512));

    /* The digest stays 576 bits whatever the preset -- the grid is what fixes it -- but
     * the hash does react to the setting, or the knob would be doing nothing. */
    uint8_t *px = (uint8_t *)malloc(200 * 200 * 3);
    ASSERT_PTR_NOT_NULL(px);
    for (int i = 0; i < 200 * 200 * 3; i++)
        px[i] = (uint8_t)((i * 7 + (i / 137) * 31) & 0xFF);
    ASSERT_OK(ph_load_from_pixels(ctx, px, 200, 200, 3, 0));

    ph_digest_t small, large;
    ASSERT_OK(ph_context_set_mhash_params(ctx, 2.0f, 1.0f, 128));
    ASSERT_OK(ph_compute_mhash(ctx, &small));
    ASSERT_OK(ph_context_set_mhash_params(ctx, 2.0f, 1.0f, 512));
    ASSERT_OK(ph_compute_mhash(ctx, &large));
    ASSERT_INT_EQ(PH_MH_BYTES, small.size);
    ASSERT_INT_EQ(PH_MH_BYTES, large.size);
    if (memcmp(small.data, large.data, small.size) == 0) {
        fprintf(stderr, "mHash ignored the normalisation size\n");
        exit(1);
    }

    free(px);
    ph_free(ctx);
    PASS("test_mhash_params_setter");
}

int main() {
    test_mh_kernel_matches_the_definition();
    test_equalize_histogram_unit();
    test_gaussian_blur_sigma_unit();
    test_mh_block_sums_match_the_direct_definition();
    test_mhash_e2e();
    test_mhash_on_a_flat_image();
    test_mhash_params_setter();
    return 0;
}
