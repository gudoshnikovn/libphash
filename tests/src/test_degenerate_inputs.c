/* R45 -- what every algorithm does with an image that has no structure to describe.
 *
 * The nine algorithms are written for photographs, and a photograph is never 1x1, never
 * a single solid colour and never one pixel wide. Those inputs still reach the library:
 * a thumbnail, a spacer image, a scanner producing a blank page, a caller feeding a video
 * frame that has not started yet. What they must not do is return a value that *looks*
 * like a hash while being a readout of uninitialised memory or of floating-point residue
 * -- that is exactly how H5 (pHash reading an unwritten buffer) reached a release.
 *
 * So this file states the answer for each degenerate class rather than leaving it to be
 * discovered later:
 *
 *   - every algorithm succeeds, or refuses with a documented error, on every geometry
 *     down to 1x1, and gives the same answer twice;
 *   - a uniform image has one documented digest per algorithm, and each of those digests
 *     follows from the algorithm's own threshold rather than from whatever the resampler
 *     happened to leave behind;
 *   - a hash of a grey image does not depend on how many channels that grey arrived in;
 *   - the balance and contrast properties the algorithms are built on hold at maximum
 *     contrast, where they are easiest to check by hand.
 *
 * Two tests here are characterisation tests: they assert behaviour that is wrong, because
 * fixing it changes hash values and belongs in its own task. Each says so at the top and
 * names what its assertion should become once the defect is fixed.
 */
#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- fixtures ------------------------------------------------------------------- */

typedef struct {
    int w, h;
    const char *name;
} geometry_t;

/* 1x1 is the smallest image there is; 1xN and Nx1 have a dimension in which the
 * resamplers and the 3x3 filters cannot form a neighbourhood; 3x3 and 7x7 are below the
 * 8x8 grid every uint64 hash reduces to; 9x8 and 5x13 are non-square and not powers of
 * two, which is where the wHash level cascade has to pick a scale. */
static const geometry_t GEOMS[] = {
    {1, 1, "1x1"}, {1, 8, "1x8"}, {8, 1, "8x1"},   {2, 2, "2x2"},     {3, 3, "3x3"},
    {7, 7, "7x7"}, {9, 8, "9x8"}, {5, 13, "5x13"}, {16, 16, "16x16"},
};
static const int N_GEOMS = (int)(sizeof(GEOMS) / sizeof(GEOMS[0]));

enum { MAX_SIDE = 16, MAX_PIXELS = MAX_SIDE * MAX_SIDE };

typedef enum {
    FILL_BLACK,   /* the minimum of the range */
    FILL_WHITE,   /* the maximum of the range */
    FILL_MID,     /* a solid colour that is neither */
    FILL_CHECKER, /* maximum contrast: only 0 and 255, alternating */
    FILL_SPLIT,   /* two tones, equal areas, one edge */
    FILL_COUNT
} fill_t;

static const char *FILL_NAMES[] = {"black", "white", "mid", "checker", "split"};

static void fill_image(uint8_t *px, int w, int h, int channels, fill_t fill) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int v;
            switch (fill) {
                case FILL_BLACK:
                    v = 0;
                    break;
                case FILL_WHITE:
                    v = 255;
                    break;
                case FILL_MID:
                    v = 137;
                    break;
                case FILL_CHECKER:
                    v = ((x + y) & 1) ? 255 : 0;
                    break;
                default:
                    v = (x < w / 2) ? 0 : 255;
                    break;
            }
            for (int c = 0; c < channels; c++)
                px[((size_t)y * w + x) * channels + c] = (uint8_t)v;
        }
}

/* Every result one image produces, so that two runs can be compared as a whole. */
typedef struct {
    uint64_t ahash, dhash, phash, whash_fast, whash_full;
    ph_digest_t bmh, mhash, radial, color, moments;
} results_t;

static void compute_all(const uint8_t *px, int w, int h, int channels, results_t *out) {
    ph_context_t *ctx = NULL;
    memset(out, 0, sizeof(*out));
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, px, w, h, channels, 0));

    ASSERT_OK(ph_compute_ahash(ctx, &out->ahash));
    ASSERT_OK(ph_compute_dhash(ctx, &out->dhash));
    ASSERT_OK(ph_compute_phash(ctx, &out->phash));
    ASSERT_OK(ph_context_set_whash_mode(ctx, PH_WHASH_FAST));
    ASSERT_OK(ph_compute_whash(ctx, &out->whash_fast));
    ASSERT_OK(ph_context_set_whash_mode(ctx, PH_WHASH_FULL));
    ASSERT_OK(ph_compute_whash(ctx, &out->whash_full));
    ASSERT_OK(ph_compute_bmh(ctx, &out->bmh));
    ASSERT_OK(ph_compute_mhash(ctx, &out->mhash));
    ASSERT_OK(ph_compute_radial_hash(ctx, &out->radial));

    if (channels >= 3) {
        ASSERT_OK(ph_compute_color_hash(ctx, &out->color));
        ASSERT_OK(ph_compute_color_moments_hash(ctx, &out->moments));
    } else {
        /* A single channel carries no colour, and the refusal has to leave the caller's
         * digest exactly as it was -- a partially written one would be worse than none. */
        ph_digest_t untouched;
        memset(&untouched, 0xC7, sizeof(untouched));
        ph_digest_t probe = untouched;
        ASSERT_INT_EQ(PH_ERR_REQUIRES_COLOR, ph_compute_color_hash(ctx, &probe));
        ASSERT(memcmp(&probe, &untouched, sizeof(probe)) == 0);
        probe = untouched;
        ASSERT_INT_EQ(PH_ERR_REQUIRES_COLOR, ph_compute_color_moments_hash(ctx, &probe));
        ASSERT(memcmp(&probe, &untouched, sizeof(probe)) == 0);
    }
    ph_free(ctx);
}

/* A digest may never claim more bytes than it wrote, nor leave anything in the tail of
 * `data` or in `reserved`: those bytes travel to the caller and into stored hashes. */
static void check_digest_shape(const ph_digest_t *d, int expect_size, ph_digest_kind_t kind,
                               const char *what) {
    if (d->size != expect_size || d->kind != (uint8_t)kind) {
        fprintf(stderr, "[FAIL] %s: size %u kind %u, expected %d/%d\n", what, d->size, d->kind,
                expect_size, (int)kind);
        exit(1);
    }
    for (int i = d->size; i < PH_DIGEST_MAX_BYTES; i++)
        if (d->data[i] != 0) {
            fprintf(stderr, "[FAIL] %s: byte %d past the digest is 0x%02x, not zero\n", what, i,
                    d->data[i]);
            exit(1);
        }
    for (size_t i = 0; i < sizeof(d->reserved); i++)
        ASSERT_INT_EQ(0, d->reserved[i]);
}

static int popcount64(uint64_t v) {
    int n = 0;
    while (v) {
        v &= v - 1;
        n++;
    }
    return n;
}

static int popcount_digest(const ph_digest_t *d) {
    int n = 0;
    for (int i = 0; i < d->size; i++)
        for (int b = 0; b < 8; b++)
            if (d->data[i] & (1u << b))
                n++;
    return n;
}

/* --- tests ---------------------------------------------------------------------- */

/* Nothing crashes, nothing fails, and nothing drifts: every algorithm, run twice over
 * every degenerate geometry, fill and channel layout, has to give the same answer both
 * times, with a digest of the width and kind it declares and no stale bytes behind it.
 *
 * Run under ASan/UBSan (`make debug`) this also covers the "reads memory nobody wrote"
 * half of the question, which is the half H5 was: a buffer left unwritten by an
 * early-returning resize would show up here as two runs disagreeing even without a
 * sanitizer, since the stack garbage differs between calls. */
void test_every_algorithm_is_defined_on_degenerate_geometry(void) {
    uint8_t px[MAX_PIXELS * 4];

    for (int g = 0; g < N_GEOMS; g++) {
        for (int f = 0; f < FILL_COUNT; f++) {
            for (int channels = 1; channels <= 4; channels++) {
                if (channels == 2)
                    continue; /* not a layout the library accepts */
                fill_image(px, GEOMS[g].w, GEOMS[g].h, channels, (fill_t)f);

                results_t first, second;
                compute_all(px, GEOMS[g].w, GEOMS[g].h, channels, &first);
                compute_all(px, GEOMS[g].w, GEOMS[g].h, channels, &second);

                if (memcmp(&first, &second, sizeof(first)) != 0) {
                    fprintf(stderr, "[FAIL] %s %s c%d: two runs disagree\n", GEOMS[g].name,
                            FILL_NAMES[f], channels);
                    exit(1);
                }

                check_digest_shape(&first.bmh, (PH_BLOCK_SIZE * PH_BLOCK_SIZE) / 8,
                                   PH_DIGEST_KIND_BITS, "BMH");
                check_digest_shape(&first.mhash, PH_MH_BYTES, PH_DIGEST_KIND_BITS, "mHash");
                check_digest_shape(&first.radial, PH_RADIAL_COEFFS, PH_DIGEST_KIND_COEFFICIENTS,
                                   "Radial");
                if (channels >= 3) {
                    check_digest_shape(&first.color, PH_COLOR_BINS, PH_DIGEST_KIND_HISTOGRAM,
                                       "ColorHash");
                    check_digest_shape(&first.moments, PH_COLOR_CHANNELS * PH_COLOR_MOMENTS * 2,
                                       PH_DIGEST_KIND_VECTOR16, "ColorMoments");
                }
            }
        }
    }

    PASS("test_every_algorithm_is_defined_on_degenerate_geometry");
}

/* The documented answer for an image with a single colour in it.
 *
 * Every one of these follows from the algorithm's own threshold, and each is pinned here
 * so that a change of resampler or of threshold cannot alter it unnoticed:
 *
 *   aHash    every sample equals the mean and the test is `>`, so no bit is set;
 *   dHash    every horizontal difference is zero and the test is `<`, so no bit is set;
 *   wHash    every LL coefficient equals the median, same `>`, so no bit is set;
 *   mHash    the Laplacian-of-Gaussian response is flat, so every block equals its
 *            window's mean and no bit is set;
 *   Radial   the projection variances are all zero, which is the flat case the algorithm
 *            answers with an all-zero digest;
 *   BMH      the threshold is `>=` the median and every block *is* the median, so every
 *            bit is set. This is the one algorithm whose flat-image answer is all ones,
 *            and it is a direct consequence of the >= in the source's equation 3.9 --
 *            the same >= that gives BMH its balanced bit distribution everywhere else;
 *   ColorHash    one bin holds every pixel and bins are scaled against the largest, so
 *            that bin is 255 and the other 107 are 0;
 *   ColorMoments the mean is the fill value; the second and third central moments are 0.
 *
 * pHash is deliberately absent: on a uniform image it produces rounding noise instead.
 * See test_phash_of_a_uniform_image_is_rounding_noise() below. */
void test_uniform_images_have_documented_digests(void) {
    static const int levels[] = {0, 1, 137, 254, 255};
    uint8_t px[MAX_PIXELS * 3];

    for (int g = 0; g < N_GEOMS; g++) {
        for (unsigned l = 0; l < sizeof(levels) / sizeof(levels[0]); l++) {
            const int v = levels[l];
            memset(px, v, (size_t)GEOMS[g].w * GEOMS[g].h * 3);

            results_t r;
            compute_all(px, GEOMS[g].w, GEOMS[g].h, 3, &r);

            ASSERT_UINT64_EQ(0ULL, r.ahash);
            ASSERT_UINT64_EQ(0ULL, r.dhash);
            ASSERT_UINT64_EQ(0ULL, r.whash_fast);
            ASSERT_UINT64_EQ(0ULL, r.whash_full);
            for (int i = 0; i < r.mhash.size; i++)
                ASSERT_UINT8_EQ(0x00, r.mhash.data[i]);
            for (int i = 0; i < r.radial.size; i++)
                ASSERT_UINT8_EQ(0x00, r.radial.data[i]);
            for (int i = 0; i < r.bmh.size; i++)
                ASSERT_UINT8_EQ(0xFF, r.bmh.data[i]);

            /* Exactly one bin, and it is the bin the quantisation says this grey is in. */
            int filled = 0, where = -1;
            for (int i = 0; i < r.color.size; i++)
                if (r.color.data[i] != 0) {
                    filled++;
                    where = i;
                    ASSERT_UINT8_EQ(255, r.color.data[i]);
                }
            ASSERT_INT_EQ(1, filled);
            ASSERT_INT_EQ(ph_color_histogram_bin(v, v, v), where);

            /* Since R62 each feature is a signed 16-bit big-endian fixed-point number,
             * two bytes wide, in units of 1/PH_VECTOR16_SCALE. */
            for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
                int feature = c * PH_COLOR_MOMENTS;
                double mean = (double)ph_read_i16_be(&r.moments.data[(feature + 0) * 2]) /
                              (double)PH_VECTOR16_SCALE;
                double std_dev = (double)ph_read_i16_be(&r.moments.data[(feature + 1) * 2]) /
                                 (double)PH_VECTOR16_SCALE;
                double skew = (double)ph_read_i16_be(&r.moments.data[(feature + 2) * 2]) /
                              (double)PH_VECTOR16_SCALE;
                ASSERT_FLOAT_EQ((double)v, mean, 0.01);
                ASSERT_FLOAT_EQ(0.0, std_dev, 0.01);
                ASSERT_FLOAT_EQ(0.0, skew, 0.01);
            }
        }
    }

    PASS("test_uniform_images_have_documented_digests");
}

/* CHARACTERISATION TEST FOR A DEFECT (R45). pHash on a uniform image is a readout of
 * floating-point rounding error, not a hash.
 *
 * Mathematically every AC coefficient of the DCT of a constant image is exactly zero, so
 * the 63 values the median is taken over are all zero, the median is zero, and `> median`
 * sets nothing -- which is what happens for a black image, where the products are exactly
 * zero in float too. For any other level the row sums of the DCT matrix are not exactly
 * zero in float, so each AC coefficient comes out at some 1e-5 times the fill value, the
 * median lands in the middle of that noise, and half the bits are decided by which way a
 * rounding error went.
 *
 * The consequence is that two flat greys a human cannot tell apart hash to nothing like
 * each other: measured on 2.0.0, levels 136, 137 and 138 give three unrelated hashes, and
 * 254 of the 255 steps from 0 to 255 change the hash. Only 0 is stable, and only because
 * zero times anything is zero.
 *
 * test_dct2_of_constant_image() in test_formula_conformance.c already records the cause
 * one level down, at the coefficients. What is added here is the consequence at the API,
 * which is the part a caller sees, plus the black image: it is the one input for which
 * the "the DC bit is always 1" invariant asserted in that same file does not hold, since
 * with DC at exactly 0 and a median of exactly 0 the `>` fails for DC too. The hash of a
 * fully black image is therefore 0, all 64 bits.
 *
 * This is not caught by comparing against ImageHash or pHash: they have the same problem,
 * for the same reason. It is confined to uniform images -- on any real picture the AC
 * coefficients are orders of magnitude above the noise -- so it does not touch the golden
 * hashes, and no fix is attempted here because every candidate (thresholding with `>=`,
 * snapping near-zero coefficients, special-casing a zero-variance input) changes pHash
 * values for some inputs and needs its own decision.
 *
 * WHEN THAT DECISION IS MADE: this test should become an assertion that all flat images
 * hash alike -- most likely to zero, matching every other algorithm here. */
void test_phash_of_a_uniform_image_is_rounding_noise(void) {
    enum { SIDE = 32 };
    uint8_t px[SIDE * SIDE * 3];
    uint64_t hashes[256];

    for (int v = 0; v < 256; v++) {
        memset(px, v, sizeof(px));
        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_load_from_pixels(ctx, px, SIDE, SIDE, 3, 0));
        ASSERT_OK(ph_compute_phash(ctx, &hashes[v]));
        ph_free(ctx);
    }

    /* Black is the only level whose coefficients are exactly zero. */
    ASSERT_UINT64_EQ(0ULL, hashes[0]);

    /* Everything else is noise, and neighbouring levels are as far apart as unrelated
     * images. If a future change makes these agree, the defect has been fixed and this
     * test has to be rewritten -- see the note above; it must not simply be deleted. */
    int changes = 0;
    for (int v = 1; v < 256; v++)
        if (hashes[v] != hashes[v - 1])
            changes++;
    if (changes < 200) {
        fprintf(stderr,
                "[NOTE] pHash over flat greys now changes only %d times in 255 steps -- if it "
                "is stable, replace this characterisation test with the invariant\n",
                changes);
        exit(1);
    }
    ASSERT(ph_hamming_distance(hashes[136], hashes[137]) > 10);
    ASSERT(ph_hamming_distance(hashes[137], hashes[138]) > 10);

    PASS("test_phash_of_a_uniform_image_is_rounding_noise");
}

/* CHARACTERISATION TEST FOR A DEFECT (R45). Three parameter values the setters accept
 * collapse their algorithm to a constant, and report PH_SUCCESS while doing it.
 *
 *   ph_context_set_phash_params(ctx, n, 1)  -- the hash is 1x1 = one coefficient, the DC
 *       term, and pHash thresholds against the median of the AC coefficients only. With
 *       one coefficient there are no AC terms, ph_median_bitpack_from() sees
 *       median_from >= n and returns 0. Every image hashes to 0.
 *   ph_context_set_block_params(ctx, 1)     -- one block, whose value is its own median,
 *       and the threshold is `>=`. Every image hashes to 0x01.
 *   ph_context_set_radial_params(ctx, p, 1) -- one sample per projection, and the variance
 *       of one sample is zero. Every projection is flat, so every image gets the all-zero
 *       digest that means "no radial structure".
 *
 * All three are the H5/M12 anti-pattern the review is about: a silently wrong answer that
 * a caller cannot distinguish from a real one. They are recorded rather than fixed here
 * because the fix is a contract change -- either the setters reject these values, or the
 * compute functions return PH_ERR_INVALID_ARGUMENT -- and that is a decision, not a patch.
 *
 * WHEN IT IS MADE: each block below becomes an assertion that the setter (or the compute
 * call) returns PH_ERR_INVALID_ARGUMENT. */
void test_parameter_values_that_collapse_the_hash_to_a_constant(void) {
    enum { SIDE = 32 };
    uint8_t a[SIDE * SIDE], b[SIDE * SIDE];
    for (int y = 0; y < SIDE; y++)
        for (int x = 0; x < SIDE; x++) {
            a[y * SIDE + x] = (uint8_t)((x * 37 + y * 91) % 256);
            b[y * SIDE + x] = (uint8_t)((x * x + y * 13) % 256);
        }

    /* pHash with a single coefficient. */
    for (int i = 0; i < 2; i++) {
        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_context_set_phash_params(ctx, PH_DCT_SIZE, 1));
        ASSERT_OK(ph_load_from_pixels(ctx, i ? b : a, SIDE, SIDE, 1, 0));
        uint64_t h = 0xdeadbeefULL;
        ASSERT_OK(ph_compute_phash(ctx, &h));
        ASSERT_UINT64_EQ(0ULL, h);
        ph_free(ctx);
    }

    /* BMH with a single block. */
    for (int i = 0; i < 2; i++) {
        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_context_set_block_params(ctx, 1));
        ASSERT_OK(ph_load_from_pixels(ctx, i ? b : a, SIDE, SIDE, 1, 0));
        ph_digest_t d;
        memset(&d, 0xAA, sizeof(d));
        ASSERT_OK(ph_compute_bmh(ctx, &d));
        ASSERT_INT_EQ(1, d.size);
        ASSERT_UINT8_EQ(0x01, d.data[0]);
        ph_free(ctx);
    }

    /* Radial with a single sample per projection. */
    for (int i = 0; i < 2; i++) {
        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_context_set_radial_params(ctx, PH_RADIAL_PROJECTIONS, 1));
        ASSERT_OK(ph_load_from_pixels(ctx, i ? b : a, SIDE, SIDE, 1, 0));
        ph_digest_t d;
        memset(&d, 0xAA, sizeof(d));
        ASSERT_OK(ph_compute_radial_hash(ctx, &d));
        ASSERT_INT_EQ(PH_RADIAL_COEFFS, d.size);
        for (int k = 0; k < d.size; k++)
            ASSERT_UINT8_EQ(0x00, d.data[k]);
        ph_free(ctx);
    }

    PASS("test_parameter_values_that_collapse_the_hash_to_a_constant");
}

/* The same grey picture delivered three ways must hash identically.
 *
 * The default weights are 38/75/15 and the conversion is a >> 7, so a pixel (v, v, v)
 * converts to exactly v -- the weights are normalised to sum to exactly 128, with the
 * blue weight absorbing the rounding, which is what makes this exact at both ends of the
 * range rather than approximately right in the middle. And alpha is not a colour: a
 * fourth channel full of noise must not move a single bit.
 *
 * This catches a whole class of channel-indexing mistakes that a single-layout test
 * cannot -- reading the wrong stride, forgetting the alpha skip, taking the grayscale
 * fast path for the wrong channel count. */
void test_channel_layout_does_not_change_a_grey_hash(void) {
    enum { W = 37, H = 23 };
    uint8_t gray[W * H], rgb[W * H * 3], rgba[W * H * 4];

    for (int i = 0; i < W * H; i++) {
        uint8_t v = (uint8_t)((i * 97) % 256);
        gray[i] = v;
        rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = v;
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = v;
        rgba[i * 4 + 3] = (uint8_t)((i * 31) % 256); /* alpha varies and is irrelevant */
    }

    results_t g1, g3, g4;
    compute_all(gray, W, H, 1, &g1);
    compute_all(rgb, W, H, 3, &g3);
    compute_all(rgba, W, H, 4, &g4);

    /* The colour digests are left out of the comparison: the 1-channel run has none. */
    g1.color = g3.color;
    g1.moments = g3.moments;
    ASSERT(memcmp(&g1, &g3, sizeof(g1)) == 0);

    ASSERT_UINT64_EQ(g3.ahash, g4.ahash);
    ASSERT_UINT64_EQ(g3.dhash, g4.dhash);
    ASSERT_UINT64_EQ(g3.phash, g4.phash);
    ASSERT_UINT64_EQ(g3.whash_fast, g4.whash_fast);
    ASSERT_UINT64_EQ(g3.whash_full, g4.whash_full);
    ASSERT(memcmp(&g3.bmh, &g4.bmh, sizeof(g3.bmh)) == 0);
    ASSERT(memcmp(&g3.mhash, &g4.mhash, sizeof(g3.mhash)) == 0);
    ASSERT(memcmp(&g3.radial, &g4.radial, sizeof(g3.radial)) == 0);
    ASSERT(memcmp(&g3.color, &g4.color, sizeof(g3.color)) == 0);
    ASSERT(memcmp(&g3.moments, &g4.moments, sizeof(g3.moments)) == 0);

    PASS("test_channel_layout_does_not_change_a_grey_hash");
}

/* Maximum contrast, where the thresholds can be worked out on paper.
 *
 * A picture split down the middle into a black half and a white half reduces, under any
 * of the resamplers, to an 8x8 (or 16x16) grid that is still half black and half white.
 * Then:
 *
 *   - aHash's mean lands between the two tones, so exactly the bright half of the grid is
 *     set: 32 of 64 bits;
 *   - BMH's median is the upper of the two central values, i.e. the bright tone, and the
 *     threshold is `>=`, so again exactly half the blocks are set: 128 of 256 bits. That
 *     balance is the property the source's method rests on, and this is the cleanest case
 *     in which to assert it;
 *   - dHash reduces to 9 columns, and 9 is odd, so the middle column straddles the edge
 *     and comes out at an intermediate tone. Each row therefore rises twice -- black to
 *     the middle tone, middle tone to white -- and dHash sets 2 of its 8 bits per row,
 *     16 in all. (One per row would be the answer for an even reduction width; the 9x8
 *     grid is the source's, so 16 is.)
 *
 * Getting any of these wrong by one is the classic off-by-one in a threshold or in a bit
 * index, and on a photograph it would be invisible. */
void test_maximum_contrast_thresholds(void) {
    enum { SIDE = 64 };
    static uint8_t px[SIDE * SIDE];
    for (int y = 0; y < SIDE; y++)
        for (int x = 0; x < SIDE; x++)
            px[y * SIDE + x] = (uint8_t)(x < SIDE / 2 ? 0 : 255);

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, px, SIDE, SIDE, 1, 0));

    uint64_t ahash = 0, dhash = 0;
    ASSERT_OK(ph_compute_ahash(ctx, &ahash));
    ASSERT_OK(ph_compute_dhash(ctx, &dhash));
    ASSERT_INT_EQ(32, popcount64(ahash));
    ASSERT_INT_EQ(16, popcount64(dhash));

    ph_digest_t bmh;
    ASSERT_OK(ph_compute_bmh(ctx, &bmh));
    ASSERT_INT_EQ(PH_BLOCK_SIZE * PH_BLOCK_SIZE / 2, popcount_digest(&bmh));

    ph_free(ctx);
    PASS("test_maximum_contrast_thresholds");
}

/* An image whose smaller side is 1 has no horizontal neighbours to differ from, and dHash
 * is nothing but horizontal differences: its answer is 0, the same as for a blank image.
 * Radial has the matching problem -- every projection line leaves the image immediately,
 * so there is no variance to measure and the digest is the all-zero one.
 *
 * Both are collisions with the blank image, and both are inherent rather than
 * fixable: there is genuinely nothing in a one-pixel-wide picture for either descriptor
 * to describe. Stated here so it reads as a known limit rather than as a surprise. */
void test_one_pixel_wide_images_collide_with_a_blank_image(void) {
    enum { N = 32 };
    uint8_t col[N], row[N];
    for (int i = 0; i < N; i++)
        col[i] = row[i] = (uint8_t)((i * 71) % 256);

    results_t vertical, horizontal;
    compute_all(col, 1, N, 1, &vertical);   /* 1 x N */
    compute_all(row, N, 1, 1, &horizontal); /* N x 1 */

    ASSERT_UINT64_EQ(0ULL, vertical.dhash);
    for (int i = 0; i < vertical.radial.size; i++)
        ASSERT_UINT8_EQ(0x00, vertical.radial.data[i]);
    for (int i = 0; i < horizontal.radial.size; i++)
        ASSERT_UINT8_EQ(0x00, horizontal.radial.data[i]);

    /* A row of varying pixels is not blank, though, and the algorithms that can see along
     * it must say so -- otherwise this test would be passing for the wrong reason. */
    ASSERT(horizontal.dhash != 0ULL);
    ASSERT(horizontal.ahash != 0ULL);
    ASSERT(vertical.ahash != 0ULL);

    PASS("test_one_pixel_wide_images_collide_with_a_blank_image");
}

/* An image smaller than the grid an algorithm reduces to is upsampled, not rejected. The
 * box resampler makes that exact: replicating every pixel 2x2 and hashing the result must
 * give the same answer as hashing the original, for every algorithm whose reduction is a
 * box resize (pHash, wHash in both modes, BMH).
 *
 * aHash, dHash and mHash are excluded because they reduce through ph_resize_lanczos(),
 * whose filter is not a box and therefore not invariant to replication; Radial is excluded
 * because its blur runs at full resolution, so the two images genuinely differ before it
 * ever samples them. */
void test_a_replicated_image_hashes_like_the_original(void) {
    enum { W = 6, H = 5 };
    uint8_t small[W * H], big[W * 2 * H * 2];
    for (int i = 0; i < W * H; i++)
        small[i] = (uint8_t)((i * 53) % 256);
    for (int y = 0; y < H * 2; y++)
        for (int x = 0; x < W * 2; x++)
            big[y * (W * 2) + x] = small[(y / 2) * W + (x / 2)];

    results_t s, b;
    compute_all(small, W, H, 1, &s);
    compute_all(big, W * 2, H * 2, 1, &b);

    ASSERT_UINT64_EQ(s.phash, b.phash);
    ASSERT_UINT64_EQ(s.whash_fast, b.whash_fast);
    ASSERT_UINT64_EQ(s.whash_full, b.whash_full);
    ASSERT(memcmp(&s.bmh, &b.bmh, sizeof(s.bmh)) == 0);

    PASS("test_a_replicated_image_hashes_like_the_original");
}

/* Saturated colours are where a channel mix-up shows: every one of them is an extreme of
 * one axis of the colour histogram and a middle of the others, so swapping two channels
 * anywhere in the pipeline moves the bin. The six corners of the colour cube that are not
 * grey must land in six different bins, and the resulting histograms must not intersect
 * at all.
 *
 * The grayscale hashes see a flat image in every one of these cases -- a constant colour
 * is a constant grey -- so they give the uniform-image answers, which is asserted too:
 * it is the check that the colour is actually reaching the colour algorithms and not
 * leaking into the structural ones. */
void test_saturated_colours_are_binned_apart(void) {
    static const struct {
        const char *name;
        int r, g, b;
    } colours[] = {
        {"red", 255, 0, 0},      {"green", 0, 255, 0},  {"blue", 0, 0, 255},
        {"yellow", 255, 255, 0}, {"cyan", 0, 255, 255}, {"magenta", 255, 0, 255},
    };
    const int n = (int)(sizeof(colours) / sizeof(colours[0]));
    enum { SIDE = 8 };
    uint8_t px[SIDE * SIDE * 3];
    ph_digest_t hist[6];
    int bins[6];

    for (int i = 0; i < n; i++) {
        for (int k = 0; k < SIDE * SIDE; k++) {
            px[k * 3] = (uint8_t)colours[i].r;
            px[k * 3 + 1] = (uint8_t)colours[i].g;
            px[k * 3 + 2] = (uint8_t)colours[i].b;
        }
        results_t r;
        compute_all(px, SIDE, SIDE, 3, &r);
        hist[i] = r.color;

        bins[i] = -1;
        for (int k = 0; k < r.color.size; k++)
            if (r.color.data[k] != 0) {
                ASSERT_INT_EQ(-1, bins[i]); /* a flat colour occupies exactly one bin */
                bins[i] = k;
            }
        ASSERT_INT_EQ(ph_color_histogram_bin(colours[i].r, colours[i].g, colours[i].b), bins[i]);

        /* Flat in grey, whatever the colour. */
        ASSERT_UINT64_EQ(0ULL, r.ahash);
        ASSERT_UINT64_EQ(0ULL, r.dhash);
        ASSERT_UINT64_EQ(0ULL, r.whash_fast);
        for (int k = 0; k < r.radial.size; k++)
            ASSERT_UINT8_EQ(0x00, r.radial.data[k]);
    }

    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            if (bins[i] == bins[j]) {
                fprintf(stderr, "[FAIL] %s and %s share colour bin %d\n", colours[i].name,
                        colours[j].name, bins[i]);
                exit(1);
            }
            double v = -1.0;
            ASSERT_OK(ph_histogram_intersection(&hist[i], &hist[j], &v));
            ASSERT_FLOAT_EQ(0.0, v, 1e-12);
        }

    PASS("test_saturated_colours_are_binned_apart");
}

int main(void) {
    test_every_algorithm_is_defined_on_degenerate_geometry();
    test_uniform_images_have_documented_digests();
    test_phash_of_a_uniform_image_is_rounding_noise();
    test_parameter_values_that_collapse_the_hash_to_a_constant();
    test_channel_layout_does_not_change_a_grey_hash();
    test_maximum_contrast_thresholds();
    test_one_pixel_wide_images_collide_with_a_blank_image();
    test_a_replicated_image_hashes_like_the_original();
    test_saturated_colours_are_binned_apart();
    return 0;
}
