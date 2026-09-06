#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The quantisation, against the axis definitions rather than against itself. */
void test_color_histogram_bin_unit() {
    ASSERT_INT_EQ(108, PH_COLOR_BINS);

    /* Every bin index is inside the digest, for every colour in the cube. Stepping by 17
     * covers 16 values per channel, which is enough to catch an off-by-one at any edge
     * without walking all 16.7 million. */
    for (int r = 0; r < 256; r += 17)
        for (int g = 0; g < 256; g += 17)
            for (int b = 0; b < 256; b += 17) {
                int bin = ph_color_histogram_bin(r, g, b);
                ASSERT(bin >= 0 && bin < PH_COLOR_BINS);
            }
    /* And at the corners exactly, which the step above skips. */
    const int corners[8][3] = {{0, 0, 0},     {255, 0, 0},   {0, 255, 0},   {0, 0, 255},
                               {255, 255, 0}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
    for (int i = 0; i < 8; i++) {
        int bin = ph_color_histogram_bin(corners[i][0], corners[i][1], corners[i][2]);
        ASSERT(bin >= 0 && bin < PH_COLOR_BINS);
    }

    /* The axes are what they claim. Pure red and pure green sit at opposite ends of the
     * red-green axis; pure blue and pure yellow at opposite ends of the blue-yellow one. */
    int red = ph_color_histogram_bin(255, 0, 0) / (PH_COLOR_BINS_BY * PH_COLOR_BINS_WB);
    int green = ph_color_histogram_bin(0, 255, 0) / (PH_COLOR_BINS_BY * PH_COLOR_BINS_WB);
    ASSERT_INT_EQ(PH_COLOR_BINS_RG - 1, red);
    ASSERT_INT_EQ(0, green);

    int blue = (ph_color_histogram_bin(0, 0, 255) / PH_COLOR_BINS_WB) % PH_COLOR_BINS_BY;
    int yellow = (ph_color_histogram_bin(255, 255, 0) / PH_COLOR_BINS_WB) % PH_COLOR_BINS_BY;
    ASSERT_INT_EQ(PH_COLOR_BINS_BY - 1, blue);
    ASSERT_INT_EQ(0, yellow);

    /* Every grey is on the same chroma bin -- rg and by are both zero -- and differs only
     * along the light-dark axis. */
    int dark = ph_color_histogram_bin(20, 20, 20);
    int mid = ph_color_histogram_bin(128, 128, 128);
    int light = ph_color_histogram_bin(240, 240, 240);
    ASSERT_INT_EQ(dark / PH_COLOR_BINS_WB, mid / PH_COLOR_BINS_WB);
    ASSERT_INT_EQ(mid / PH_COLOR_BINS_WB, light / PH_COLOR_BINS_WB);
    ASSERT(dark % PH_COLOR_BINS_WB < mid % PH_COLOR_BINS_WB);
    ASSERT(mid % PH_COLOR_BINS_WB < light % PH_COLOR_BINS_WB);

    PASS("test_color_histogram_bin_unit");
}

/* The comparison, against the formula worked out by hand. */
void test_histogram_intersection_unit() {
    ph_digest_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.size = b.size = 4;
    a.kind = b.kind = (uint8_t)PH_DIGEST_KIND_HISTOGRAM;

    /* a = (100, 100, 0, 0) -> (0.5, 0.5, 0, 0); b = (50, 0, 50, 100) -> (0.25, 0, 0.25, 0.5).
     * Intersection = min(.5,.25) + min(.5,0) + min(0,.25) + min(0,.5) = 0.25. */
    a.data[0] = 100;
    a.data[1] = 100;
    b.data[0] = 50;
    b.data[2] = 50;
    b.data[3] = 100;
    double v = -9.0;
    ASSERT_OK(ph_histogram_intersection(&a, &b, &v));
    ASSERT_FLOAT_EQ(0.25, v, 1e-9);

    /* Symmetric, unlike the formula as Swain and Ballard state it. */
    double back = -9.0;
    ASSERT_OK(ph_histogram_intersection(&b, &a, &back));
    ASSERT_FLOAT_EQ(v, back, 1e-9);

    /* Identical distributions score 1.0, and so do two scalings of the same shape --
     * the normalisation is by each side's own total. */
    ASSERT_OK(ph_histogram_intersection(&a, &a, &v));
    ASSERT_FLOAT_EQ(1.0, v, 1e-9);
    ph_digest_t scaled = a;
    scaled.data[0] = 40;
    scaled.data[1] = 40;
    ASSERT_OK(ph_histogram_intersection(&a, &scaled, &v));
    ASSERT_FLOAT_EQ(1.0, v, 1e-9);

    /* Disjoint distributions score 0.0. */
    ph_digest_t c;
    memset(&c, 0, sizeof(c));
    c.size = 4;
    c.kind = (uint8_t)PH_DIGEST_KIND_HISTOGRAM;
    c.data[2] = 200;
    c.data[3] = 55;
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    d.size = 4;
    d.kind = (uint8_t)PH_DIGEST_KIND_HISTOGRAM;
    d.data[0] = 10;
    d.data[1] = 20;
    ASSERT_OK(ph_histogram_intersection(&c, &d, &v));
    ASSERT_FLOAT_EQ(0.0, v, 1e-9);

    /* Two empty histograms are as alike as two images can be; one empty one is not. */
    ph_digest_t e;
    memset(&e, 0, sizeof(e));
    e.size = 4;
    e.kind = (uint8_t)PH_DIGEST_KIND_HISTOGRAM;
    ASSERT_OK(ph_histogram_intersection(&e, &e, &v));
    ASSERT_FLOAT_EQ(1.0, v, 1e-9);
    ASSERT_OK(ph_histogram_intersection(&e, &a, &v));
    ASSERT_FLOAT_EQ(0.0, v, 1e-9);

    /* Refusals leave the output alone, and the wrong kind is a refusal. */
    v = -9.0;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_histogram_intersection(NULL, &a, &v));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_histogram_intersection(&a, &b, NULL));
    ph_digest_t bits = a;
    bits.kind = (uint8_t)PH_DIGEST_KIND_BITS;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_histogram_intersection(&bits, &a, &v));
    ph_digest_t shorter = a;
    shorter.size = 3;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_histogram_intersection(&a, &shorter, &v));
    ASSERT_FLOAT_EQ(-9.0, v, 1e-9);

    PASS("test_histogram_intersection_unit");
}

static void flat_digest(int r, int g, int b, ph_digest_t *out) {
    uint8_t px[8 * 8 * 3];
    for (int i = 0; i < 8 * 8; i++) {
        px[i * 3] = (uint8_t)r;
        px[i * 3 + 1] = (uint8_t)g;
        px[i * 3 + 2] = (uint8_t)b;
    }
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, px, 8, 8, 3, 0));
    ASSERT_OK(ph_compute_color_hash(ctx, out));
    ph_free(ctx);
}

/* What the quantisation can and cannot tell apart, stated rather than discovered later.
 *
 * The resolution was picked by measurement, and two candidates that scored *higher* on the
 * property corpus -- 6x6x1 at 4.28 and 9x9x1 at 3.94, against 3.95 for the one chosen --
 * were rejected because they drop the light-dark axis, which makes a black image and a
 * white image hash identically. A corpus of colourful pictures cannot see that. This test
 * can, and it exists so that a future retuning cannot make the same trade quietly. */
void test_color_hash_separates_flat_colours() {
    struct {
        const char *name;
        int r, g, b;
    } colours[] = {
        {"black", 8, 8, 8},       {"mid grey", 128, 128, 128},  {"white", 248, 248, 248},
        {"dark red", 90, 20, 20}, {"light red", 240, 170, 170}, {"blue", 30, 40, 220},
    };
    const int n = (int)(sizeof(colours) / sizeof(colours[0]));
    ph_digest_t d[6];
    for (int i = 0; i < n; i++)
        flat_digest(colours[i].r, colours[i].g, colours[i].b, &d[i]);

    /* Black against white is the pair that the rejected quantisations merged. */
    double v = 0.0;
    ASSERT_OK(ph_histogram_intersection(&d[0], &d[2], &v));
    if (v > 0.01) {
        fprintf(stderr,
                "[FAIL] black and white intersect at %.3f -- the light-dark axis has been "
                "weakened or dropped\n",
                v);
        exit(1);
    }

    /* And every distinguishable pair here stays distinguishable. */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            ASSERT_OK(ph_histogram_intersection(&d[i], &d[j], &v));
            if (v > 0.01) {
                fprintf(stderr, "[FAIL] %s and %s intersect at %.3f\n", colours[i].name,
                        colours[j].name, v);
                exit(1);
            }
        }

    /* The known limit, asserted as a limit: three intensity bins cannot split the bottom
     * third, so a very dark grey and a slightly less dark grey do collide. Documented in
     * the header of src/hashes/color_histogram.c. */
    ph_digest_t very_dark, dark_grey;
    flat_digest(8, 8, 8, &very_dark);
    flat_digest(64, 64, 64, &dark_grey);
    ASSERT_OK(ph_histogram_intersection(&very_dark, &dark_grey, &v));
    ASSERT_FLOAT_EQ(1.0, v, 1e-9);

    PASS("test_color_hash_separates_flat_colours");
}

void test_color_hash_e2e() {
    ph_context_t *ctx = NULL;
    ph_digest_t same, copy, other;

    ASSERT_OK(ph_create(&ctx));

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_OK(ph_compute_color_hash(ctx, &same));
    ASSERT_INT_EQ(PH_COLOR_BINS, same.size);
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_HISTOGRAM, same.kind);

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_copy.jpeg"));
    ASSERT_OK(ph_compute_color_hash(ctx, &copy));
    double v = 0.0;
    ASSERT_OK(ph_histogram_intersection(&same, &copy, &v));
    ASSERT_FLOAT_EQ(1.0, v, 1e-9);

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_complex.png"));
    ASSERT_OK(ph_compute_color_hash(ctx, &other));
    double unrelated = 0.0;
    ASSERT_OK(ph_histogram_intersection(&same, &other, &unrelated));
    if (unrelated > 0.6) {
        fprintf(stderr, "[FAIL] an unrelated image intersects at %.3f\n", unrelated);
        exit(1);
    }
    printf("  ColorHash: identical copy %.4f, unrelated image %.4f\n", v, unrelated);

    /* Rotating the picture cannot change a histogram at all: it counts pixels and knows
     * nothing about where they are. This is the property the algorithm is for, and the
     * one that makes it useless on its own. */
    ph_digest_t rotated;
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_rotated_90.jpeg"));
    ASSERT_OK(ph_compute_color_hash(ctx, &rotated));
    ASSERT_OK(ph_histogram_intersection(&same, &rotated, &v));
    if (v < 0.95) {
        fprintf(stderr, "[FAIL] a 90-degree rotation moved the histogram to %.3f\n", v);
        exit(1);
    }

    ph_free(ctx);
    PASS("test_color_hash_e2e");
}

/* R08: a grayscale-loaded image carries no colour information at all -- ColorHash used
 * to silently classify every pixel from a single replicated channel and still report
 * PH_SUCCESS. It must refuse instead, without writing to the output. */
void test_color_hash_requires_color() {
    ph_context_t *ctx = NULL;
    ph_digest_t digest;
    memset(&digest, 0xAB, sizeof(digest));

    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_context_set_load_grayscale(ctx, 1));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    ASSERT_INT_EQ(PH_ERR_REQUIRES_COLOR, ph_compute_color_hash(ctx, &digest));
    for (size_t i = 0; i < sizeof(digest); i++)
        ASSERT_INT_EQ(0xAB, ((const uint8_t *)&digest)[i]);

    /* Grayscale-only algorithms are unaffected. */
    uint64_t ahash = 0;
    ASSERT_OK(ph_compute_ahash(ctx, &ahash));

    ph_free(ctx);
    PASS("test_color_hash_requires_color");
}

/* A single-channel buffer handed straight to ph_load_from_pixels() is refused for the
 * same reason -- the check is on the loaded image, not on the load_grayscale setting. */
void test_color_hash_refuses_one_channel_pixels() {
    ph_context_t *ctx = NULL;
    uint8_t gray[16 * 16];
    ph_digest_t digest;
    memset(&digest, 0, sizeof(digest));

    for (int i = 0; i < 16 * 16; i++)
        gray[i] = (uint8_t)i;

    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, gray, 16, 16, 1, 0));
    ASSERT_INT_EQ(PH_ERR_REQUIRES_COLOR, ph_compute_color_hash(ctx, &digest));

    ph_free(ctx);
    PASS("test_color_hash_refuses_one_channel_pixels");
}

/* R45. The counts, not just the bins they land in.
 *
 * The digest is scaled against the largest bin rather than against the pixel count, and
 * rounded to nearest in integer arithmetic. Both choices are invisible on a photograph,
 * where every bin holds a fraction of a percent, and both are easy to get wrong by one --
 * a truncating divide, or a scale by the total. An image built from two colours in a
 * known ratio pins them: the majority colour is 255 by definition, and the other is that
 * ratio of it, rounded.
 *
 * The single-pixel case is the degenerate end of the same rule and is worth stating
 * because it is the one a caller is most likely to hit by accident: one pixel is its own
 * largest bin, so a 1x1 image produces exactly one bin at 255, whatever its colour. */
void test_color_histogram_counts_are_scaled_against_the_largest_bin() {
    enum { N = 16 };
    uint8_t px[N * 3];

    struct {
        int majority; /* how many of the N pixels are red; the rest are blue */
        int expected; /* what the blue bin must therefore hold */
    } cases[] = {
        {12, 85},  /* 4/12 -> 85.0, exact */
        {8, 255},  /* equal halves: both bins are the largest */
        {15, 17},  /* 1/15 -> 17.0, exact */
        {10, 153}, /* 6/10 -> 153.0, exact */
        {11, 116}, /* 5/11 -> 115.9, rounds up */
    };

    const int red_bin = ph_color_histogram_bin(255, 0, 0);
    const int blue_bin = ph_color_histogram_bin(0, 0, 255);
    ASSERT(red_bin != blue_bin);

    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        for (int i = 0; i < N; i++) {
            int red = i < cases[c].majority;
            px[i * 3] = (uint8_t)(red ? 255 : 0);
            px[i * 3 + 1] = 0;
            px[i * 3 + 2] = (uint8_t)(red ? 0 : 255);
        }
        ph_context_t *ctx = NULL;
        ph_digest_t d;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_load_from_pixels(ctx, px, N, 1, 3, 0));
        ASSERT_OK(ph_compute_color_hash(ctx, &d));
        ph_free(ctx);

        ASSERT_UINT8_EQ(255, d.data[red_bin]);
        ASSERT_UINT8_EQ(cases[c].expected, d.data[blue_bin]);
        for (int i = 0; i < d.size; i++)
            if (i != red_bin && i != blue_bin)
                ASSERT_UINT8_EQ(0, d.data[i]);
    }

    /* One pixel: one bin, full scale. */
    uint8_t single[3] = {30, 200, 90};
    ph_context_t *ctx = NULL;
    ph_digest_t d;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, single, 1, 1, 3, 0));
    ASSERT_OK(ph_compute_color_hash(ctx, &d));
    ph_free(ctx);
    ASSERT_UINT8_EQ(255, d.data[ph_color_histogram_bin(30, 200, 90)]);
    int filled = 0;
    for (int i = 0; i < d.size; i++)
        if (d.data[i])
            filled++;
    ASSERT_INT_EQ(1, filled);

    PASS("test_color_histogram_counts_are_scaled_against_the_largest_bin");
}

int main() {
    test_color_histogram_bin_unit();
    test_color_histogram_counts_are_scaled_against_the_largest_bin();
    test_histogram_intersection_unit();
    test_color_hash_separates_flat_colours();
    test_color_hash_e2e();
    test_color_hash_requires_color();
    test_color_hash_refuses_one_channel_pixels();
    return 0;
}
