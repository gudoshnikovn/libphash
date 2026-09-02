/*
 * test_hash_properties.c
 *
 * Three of the nine hashes -- wHash, mHash and ColorHash -- have no primary source
 * (docs/references.md). For those, "correct" cannot mean conformance to a specification,
 * because there is none. It can only mean that the hash does the job a perceptual hash
 * exists to do, measured:
 *
 *   robustness      the same image after a benign change hashes close by
 *   discrimination  different images hash far apart
 *   separability    the two distributions above do not overlap
 *
 * Separability is the one that matters. Either of the first two is trivial on its own:
 * a constant hash is perfectly robust, and a cryptographic hash discriminates perfectly.
 * What makes a perceptual hash useful is the gap between them.
 *
 * The corpus is generated, not loaded: a deterministic set of synthetic images built
 * from a fixed seed. That choice, and its cost, are recorded in the verification
 * methodology (docs/algorithm-provenance.md). Briefly: it needs no network, adds nothing
 * to the repository, and reproduces byte-for-byte in CI -- but synthetic images are not
 * photographs, so the numbers here describe behaviour on this corpus and are evidence
 * about regressions, not about real-world recall.
 *
 * Every threshold below was set from the measurement this file prints, with the observed
 * value quoted next to it. None was chosen by eye. When one needs changing, re-run,
 * paste the new numbers, and say why in the commit -- do not just widen it.
 *
 * Complements tests/src/test_robustness.c, which checks the same two properties against
 * a real photograph with fixed per-algorithm distance caps. This file measures the
 * distributions instead of spot-checking them.
 */

#include "libphash.h"
#include "test_macros.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMG_W 128
#define IMG_H 128
#define NUM_BASE 24

typedef struct {
    uint8_t *px; /* interleaved RGB */
    int w, h;
} image_t;

/* Deterministic PRNG. Not a good one; it only has to be the same everywhere. */
static uint32_t rng_state;
static void rng_seed(uint32_t s) { rng_state = s ? s : 1u; }
static uint32_t rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static image_t image_new(int w, int h) {
    image_t im;
    im.w = w;
    im.h = h;
    im.px = (uint8_t *)malloc((size_t)w * h * 3);
    ASSERT_PTR_NOT_NULL(im.px);
    return im;
}

static void image_free(image_t *im) {
    free(im->px);
    im->px = NULL;
}

static void put(image_t *im, int x, int y, int r, int g, int b) {
    if (x < 0 || y < 0 || x >= im->w || y >= im->h)
        return;
    size_t o = ((size_t)y * im->w + x) * 3;
    im->px[o] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
    im->px[o + 1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
    im->px[o + 2] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
}

/* ---------------------------------------------------------------------------
 * The corpus
 *
 * Structurally varied on purpose: an algorithm that keys on one kind of feature
 * should not be able to score well by accident. Each image is a different family, and
 * within a family the parameters differ, so neighbouring indices are not near-duplicates.
 *
 * Every family is also coloured, and differently. An earlier version of this corpus was
 * mostly grayscale, on which ColorHash scored a separability of 0.98 -- not because the
 * algorithm is weak but because a grayscale corpus puts every pixel in its black and
 * grey buckets, so it was being measured on input it cannot see. A colour algorithm
 * needs colour in the corpus or the measurement means nothing.
 * ------------------------------------------------------------------------ */
static image_t make_base(int index) {
    image_t im = image_new(IMG_W, IMG_H);
    rng_seed(0xC0FFEEu + (uint32_t)index * 7919u);

    int family = index % 8;
    int variant = index / 8;

    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            int r = 0, g = 0, b = 0;
            switch (family) {
                case 0: { /* diagonal gradient, varying direction and hue */
                    int t = (variant == 0)   ? (x + y)
                            : (variant == 1) ? (x - y + IMG_H)
                                             : (2 * x + y);
                    int v = (t * 255) / (IMG_W + IMG_H);
                    r = v;
                    g = (v * 2) % 256;
                    b = 255 - v;
                    break;
                }
                case 1: { /* checkerboard, varying cell size and colour pair */
                    int cell = 4 << variant;
                    int on = ((x / cell) + (y / cell)) & 1;
                    r = on ? 235 : 30;
                    g = on ? 40 : 200;
                    b = on ? 90 : 60;
                    break;
                }
                case 2: { /* concentric rings, varying period, cyan/magenta */
                    int dx = x - IMG_W / 2, dy = y - IMG_H / 2;
                    double d = sqrt((double)(dx * dx + dy * dy));
                    double w = sin(d / (3.0 + variant * 2.0));
                    r = (int)(127.5 + 110.0 * w);
                    g = (int)(127.5 - 110.0 * w);
                    b = (int)(127.5 + 110.0 * sin(d / 7.0));
                    break;
                }
                case 3: { /* vertical stripes, varying width and colour */
                    int w = 3 + variant * 4;
                    int on = (x / w) & 1;
                    r = on ? 200 : 40;
                    g = on ? 60 : 180;
                    b = on ? 120 : 90;
                    break;
                }
                case 4: { /* filled disc on a flat field, varying radius and colour */
                    int dx = x - IMG_W / 3, dy = y - IMG_H / 2;
                    int rad = 20 + variant * 12;
                    int inside = dx * dx + dy * dy < rad * rad;
                    r = inside ? 220 : 25;
                    g = inside ? 30 : 150;
                    b = inside ? 60 : 230;
                    break;
                }
                case 5: { /* smooth 2-D sinusoid, varying frequency, channels out of phase */
                    double f = 0.05 + variant * 0.04;
                    double w = sin(x * f) * cos(y * f * 1.3);
                    r = (int)(127.5 + 100.0 * w);
                    g = (int)(127.5 + 100.0 * sin(x * f + 2.1) * cos(y * f * 1.3));
                    b = (int)(127.5 + 100.0 * sin(x * f + 4.2) * cos(y * f * 1.3));
                    break;
                }
                case 6: { /* quadrant blocks, varying palette */
                    int q = (x < IMG_W / 2 ? 0 : 1) + (y < IMG_H / 2 ? 0 : 2);
                    static const int pal[3][4] = {
                        {30, 90, 160, 230}, {200, 40, 120, 70}, {60, 210, 25, 140}};
                    r = pal[variant % 3][q];
                    g = pal[(variant + 1) % 3][q];
                    b = pal[(variant + 2) % 3][q];
                    break;
                }
                default: { /* structured noise over a ramp, varying amplitude and tint */
                    int amp = 20 + variant * 30;
                    int base = (y * 200) / IMG_H;
                    int n = (int)(rng_next() % (uint32_t)(2 * amp + 1)) - amp;
                    r = base + n + (variant == 0 ? 40 : 0);
                    g = base + n + (variant == 1 ? 40 : 0);
                    b = base + n + (variant == 2 ? 40 : 0);
                    break;
                }
            }
            put(&im, x, y, r, g, b);
        }
    }
    return im;
}

/* ---------------------------------------------------------------------------
 * Benign transformations
 *
 * Deliberately no rotation: only pHash claims any rotation tolerance, and Radial's
 * rotation invariance is a known defect (see the dedicated test at the end). Mixing a
 * rotation into the "benign" set would make every algorithm look bad for a reason that
 * has nothing to do with the property being measured.
 * ------------------------------------------------------------------------ */

static image_t xf_identity(const image_t *s) {
    image_t o = image_new(s->w, s->h);
    memcpy(o.px, s->px, (size_t)s->w * s->h * 3);
    return o;
}

static image_t xf_scale(const image_t *s, double f) {
    int w = (int)(s->w * f), h = (int)(s->h * f);
    if (w < 8)
        w = 8;
    if (h < 8)
        h = 8;
    image_t o = image_new(w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sx = (int)((x + 0.5) / f), sy = (int)((y + 0.5) / f);
            if (sx >= s->w)
                sx = s->w - 1;
            if (sy >= s->h)
                sy = s->h - 1;
            size_t si = ((size_t)sy * s->w + sx) * 3;
            put(&o, x, y, s->px[si], s->px[si + 1], s->px[si + 2]);
        }
    }
    return o;
}

static image_t xf_scale_down(const image_t *s) { return xf_scale(s, 0.5); }
static image_t xf_scale_up(const image_t *s) { return xf_scale(s, 1.75); }

static image_t xf_crop(const image_t *s) { /* 4% off every edge */
    int mx = s->w * 4 / 100, my = s->h * 4 / 100;
    int w = s->w - 2 * mx, h = s->h - 2 * my;
    image_t o = image_new(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t si = ((size_t)(y + my) * s->w + (x + mx)) * 3;
            put(&o, x, y, s->px[si], s->px[si + 1], s->px[si + 2]);
        }
    return o;
}

static image_t xf_brighter(const image_t *s) {
    image_t o = xf_identity(s);
    for (size_t i = 0; i < (size_t)s->w * s->h * 3; i++) {
        int v = o.px[i] + 25;
        o.px[i] = (uint8_t)(v > 255 ? 255 : v);
    }
    return o;
}

static image_t xf_gamma(const image_t *s) { /* gamma 1.4, a non-linear tone change */
    image_t o = xf_identity(s);
    uint8_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = (uint8_t)(pow(i / 255.0, 1.0 / 1.4) * 255.0 + 0.5);
    for (size_t i = 0; i < (size_t)s->w * s->h * 3; i++)
        o.px[i] = lut[o.px[i]];
    return o;
}

static image_t xf_blur(const image_t *s) { /* 3x3 box blur */
    image_t o = image_new(s->w, s->h);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++)
            for (int c = 0; c < 3; c++) {
                int sum = 0, n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= s->w || yy >= s->h)
                            continue;
                        sum += s->px[((size_t)yy * s->w + xx) * 3 + c];
                        n++;
                    }
                o.px[((size_t)y * o.w + x) * 3 + c] = (uint8_t)(sum / n);
            }
    return o;
}

static image_t xf_noise(const image_t *s) { /* +/-12 of additive noise */
    image_t o = xf_identity(s);
    rng_seed(0xBEEFu);
    for (size_t i = 0; i < (size_t)s->w * s->h * 3; i++) {
        int v = o.px[i] + (int)(rng_next() % 25u) - 12;
        o.px[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    return o;
}

typedef image_t (*transform_fn)(const image_t *);
static transform_fn TRANSFORMS[] = {xf_scale_down, xf_scale_up, xf_crop, xf_brighter,
                                    xf_gamma,      xf_blur,     xf_noise};
static const char *TRANSFORM_NAMES[] = {"scale 0.5", "scale 1.75", "crop 4%",    "brighter +25",
                                        "gamma 1.4", "blur 3x3",   "noise +/-12"};
#define NUM_TRANSFORMS ((int)(sizeof(TRANSFORMS) / sizeof(TRANSFORMS[0])))

/* ---------------------------------------------------------------------------
 * Algorithms under measurement
 *
 * Every distance is normalised to [0,1] so the algorithms are on one scale despite
 * different hash widths: a 64-bit Hamming distance over 64, a digest's over its bits.
 * ------------------------------------------------------------------------ */

typedef enum { A_AHASH, A_DHASH, A_PHASH, A_WHASH, A_MHASH, A_BMH, A_COLOR, A_COUNT } algo_t;

static const char *ALGO_NAMES[A_COUNT] = {"aHash", "dHash", "pHash",    "wHash",
                                          "mHash", "BMH",   "ColorHash"};

typedef struct {
    uint64_t bits;   /* the 64-bit algorithms */
    ph_digest_t dig; /* BMH */
} hash_set_t;

static void hash_image(const image_t *im, hash_set_t out[A_COUNT]) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, im->px, im->w, im->h, 3, 0));

    ASSERT_OK(ph_compute_ahash(ctx, &out[A_AHASH].bits));
    ASSERT_OK(ph_compute_dhash(ctx, &out[A_DHASH].bits));
    ASSERT_OK(ph_compute_phash(ctx, &out[A_PHASH].bits));
    ASSERT_OK(ph_compute_whash(ctx, &out[A_WHASH].bits));
    ASSERT_OK(ph_compute_mhash(ctx, &out[A_MHASH].bits));
    ASSERT_OK(ph_compute_bmh(ctx, &out[A_BMH].dig));
    ASSERT_OK(ph_compute_color_hash(ctx, &out[A_COLOR].bits));

    ph_free(ctx);
}

static double distance(algo_t a, const hash_set_t *x, const hash_set_t *y) {
    if (a == A_BMH) {
        int d = ph_hamming_distance_digest(&x[a].dig, &y[a].dig);
        ASSERT(d >= 0);
        return (double)d / (x[a].dig.size * 8.0);
    }
    /* ColorHash occupies 42 of its 64 bits; the rest are always zero and would only
     * dilute the distance, so it is normalised over the bits it actually uses. */
    double width = (a == A_COLOR) ? 42.0 : 64.0;
    return ph_hamming_distance(x[a].bits, y[a].bits) / width;
}

/* ---------------------------------------------------------------------------
 * Statistics
 * ------------------------------------------------------------------------ */

typedef struct {
    double sum, sum_sq, min, max;
    int n;
} stats_t;

static void stats_init(stats_t *s) {
    s->sum = s->sum_sq = 0.0;
    s->min = 1e300;
    s->max = -1e300;
    s->n = 0;
}
static void stats_add(stats_t *s, double v) {
    s->sum += v;
    s->sum_sq += v * v;
    if (v < s->min)
        s->min = v;
    if (v > s->max)
        s->max = v;
    s->n++;
}
static double stats_mean(const stats_t *s) { return s->n ? s->sum / s->n : 0.0; }
static double stats_sd(const stats_t *s) {
    if (s->n < 2)
        return 0.0;
    double m = stats_mean(s);
    double var = s->sum_sq / s->n - m * m;
    return var > 0.0 ? sqrt(var) : 0.0;
}

/* Separability, as the standardised gap between the two distributions -- the same
 * quantity as d-prime in detection theory. Above ~1 the distributions are usefully
 * apart; below ~0.5 they overlap so much that no threshold separates them. */
static double separability(const stats_t *intra, const stats_t *inter) {
    double si = stats_sd(intra), se = stats_sd(inter);
    double pooled = sqrt((si * si + se * se) / 2.0);
    if (pooled < 1e-9)
        return (stats_mean(inter) > stats_mean(intra)) ? 1e9 : 0.0;
    return (stats_mean(inter) - stats_mean(intra)) / pooled;
}

/* ---------------------------------------------------------------------------
 * Thresholds
 *
 * Measured on this corpus (24 bases x 7 transforms = 168 intra-pairs, 276 inter-pairs)
 * with the numbers printed by this test, then floored well below the observation so a
 * genuine regression trips it and ordinary noise does not. Observed values are in the
 * comment beside each entry; re-measure rather than relax.
 * ------------------------------------------------------------------------ */
typedef struct {
    double min_separability;
    double max_mean_intra;
    double min_mean_inter;
} bounds_t;

static const bounds_t BOUNDS[A_COUNT] = {
    /*             sep.  intra  inter        measured: sep / mean intra / mean inter   */
    [A_AHASH] = {2.50, 0.100, 0.400}, /* 3.54 / 0.052 / 0.488 */
    [A_DHASH] = {2.50, 0.120, 0.380}, /* 3.60 / 0.066 / 0.469 */
    [A_PHASH] = {1.80, 0.260, 0.400}, /* 2.48 / 0.177 / 0.490 */
    [A_WHASH] = {3.00, 0.080, 0.390}, /* 4.34 / 0.036 / 0.480 */
    [A_MHASH] = {1.80, 0.200, 0.380}, /* 2.54 / 0.124 / 0.470 */
    [A_BMH] = {3.50, 0.070, 0.390},   /* 5.21 / 0.031 / 0.478 */
    [A_COLOR] = {1.30, 0.070, 0.090}, /* 1.89 / 0.031 / 0.123 */
};

/* Two things the measurement says that are worth reading off it rather than assuming.
 *
 * ColorHash's inter-distance is low in absolute terms -- 0.123 where the structural
 * hashes sit near 0.48 -- and its floor is set accordingly. That is the algorithm, not a
 * fault: it is a 42-bit quantised histogram in which most bins are empty for most
 * images, so two unrelated images agree on a lot of zeroes. Its separability is still
 * 1.89 because its intra-distance is correspondingly tiny. Judge it by the gap, not by
 * the absolute distance, and do not compare its raw distances against a structural
 * hash's.
 *
 * pHash has the worst robustness of the structural hashes here -- mean intra-distance
 * 0.177 against 0.03-0.07 for the others -- and the second-lowest separability. That is
 * consistent with the DC-coefficient defect (docs/algorithm-provenance.md, defect 4):
 * including a term that is always above the median wastes a bit and drags the threshold
 * for the other 63. Consistent with, not proof of; the numbers to compare it against are
 * the ones this test prints after that defect is fixed. */

static void test_robustness_discrimination_separability(void) {
    image_t base[NUM_BASE];
    hash_set_t base_hash[NUM_BASE][A_COUNT];

    for (int i = 0; i < NUM_BASE; i++) {
        base[i] = make_base(i);
        hash_image(&base[i], base_hash[i]);
    }

    stats_t intra[A_COUNT], inter[A_COUNT];
    stats_t per_xf[A_COUNT][NUM_TRANSFORMS];
    for (int a = 0; a < A_COUNT; a++) {
        stats_init(&intra[a]);
        stats_init(&inter[a]);
        for (int t = 0; t < NUM_TRANSFORMS; t++)
            stats_init(&per_xf[a][t]);
    }

    /* Robustness: every base against every benign transformation of itself. */
    for (int i = 0; i < NUM_BASE; i++) {
        for (int t = 0; t < NUM_TRANSFORMS; t++) {
            image_t moved = TRANSFORMS[t](&base[i]);
            hash_set_t h[A_COUNT];
            hash_image(&moved, h);
            for (int a = 0; a < A_COUNT; a++) {
                double d = distance((algo_t)a, base_hash[i], h);
                stats_add(&intra[a], d);
                stats_add(&per_xf[a][t], d);
            }
            image_free(&moved);
        }
    }

    /* Discrimination: every base against every other base. */
    for (int i = 0; i < NUM_BASE; i++)
        for (int j = i + 1; j < NUM_BASE; j++)
            for (int a = 0; a < A_COUNT; a++)
                stats_add(&inter[a], distance((algo_t)a, base_hash[i], base_hash[j]));

    printf("\n  %-10s %19s %19s %8s\n", "algorithm", "same image (intra)", "different (inter)",
           "sep.");
    printf("  %-10s %19s %19s %8s\n", "", "mean    sd    max", "mean    sd    min", "");
    for (int a = 0; a < A_COUNT; a++) {
        printf("  %-10s %6.3f %6.3f %6.3f %6.3f %6.3f %6.3f %7.2f\n", ALGO_NAMES[a],
               stats_mean(&intra[a]), stats_sd(&intra[a]), intra[a].max, stats_mean(&inter[a]),
               stats_sd(&inter[a]), inter[a].min, separability(&intra[a], &inter[a]));
    }
    printf("\n  mean intra-distance by transformation\n  %-12s", "");
    for (int a = 0; a < A_COUNT; a++)
        printf("%10s", ALGO_NAMES[a]);
    printf("\n");
    for (int t = 0; t < NUM_TRANSFORMS; t++) {
        printf("  %-12s", TRANSFORM_NAMES[t]);
        for (int a = 0; a < A_COUNT; a++)
            printf("%10.3f", stats_mean(&per_xf[a][t]));
        printf("\n");
    }
    printf("\n");

    for (int a = 0; a < A_COUNT; a++) {
        double sep = separability(&intra[a], &inter[a]);
        double mi = stats_mean(&intra[a]), me = stats_mean(&inter[a]);

        if (mi >= me) {
            fprintf(stderr,
                    "[FAIL] %s: a transformed image is no closer than an unrelated one "
                    "(intra %.3f >= inter %.3f) -- the hash carries no usable signal\n",
                    ALGO_NAMES[a], mi, me);
            exit(1);
        }
        if (sep < BOUNDS[a].min_separability) {
            fprintf(stderr, "[FAIL] %s: separability %.2f below the floor %.2f\n", ALGO_NAMES[a],
                    sep, BOUNDS[a].min_separability);
            exit(1);
        }
        if (mi > BOUNDS[a].max_mean_intra) {
            fprintf(stderr, "[FAIL] %s: mean intra-distance %.3f above the ceiling %.3f\n",
                    ALGO_NAMES[a], mi, BOUNDS[a].max_mean_intra);
            exit(1);
        }
        if (me < BOUNDS[a].min_mean_inter) {
            fprintf(stderr, "[FAIL] %s: mean inter-distance %.3f below the floor %.3f\n",
                    ALGO_NAMES[a], me, BOUNDS[a].min_mean_inter);
            exit(1);
        }
    }

    for (int i = 0; i < NUM_BASE; i++)
        image_free(&base[i]);

    printf("test_robustness_discrimination_separability: PASSED\n");
}

/* ---------------------------------------------------------------------------
 * Radial: the rotation invariance that is documented but not delivered
 * ------------------------------------------------------------------------ */

static image_t rotate_90(const image_t *s) {
    image_t o = image_new(s->h, s->w);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            size_t si = ((size_t)y * s->w + x) * 3;
            put(&o, s->h - 1 - y, x, s->px[si], s->px[si + 1], s->px[si + 2]);
        }
    return o;
}

static void radial_of(const image_t *im, ph_digest_t *out) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, im->px, im->w, im->h, 3, 0));
    ASSERT_OK(ph_compute_radial_hash(ctx, out));
    ph_free(ctx);
}

static void test_radial_is_not_rotation_invariant_today(void) {
    /* KNOWN DIVERGENCE (docs/algorithm-provenance.md, defect 2): the source compares two
     * radial hashes by the peak of cross-correlation, which is what turns a rotation --
     * a cyclic shift of the radial variance vector -- into a match. libphash compares
     * digests element-wise, so the shift is not undone and the rotation is not absorbed.
     *
     * This test asserts the CURRENT, defective behaviour, so that implementing the
     * comparison from the source has to change it. It is the counter-example to the
     * claim docs/algorithms.md used to make.
     *
     * A square image with strong directional structure is used, rotated by exactly 90
     * degrees so that no resampling is involved: whatever distance shows up is the
     * algorithm's, not the interpolator's. */
    image_t im = image_new(128, 128);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            int on = ((x / 6) & 1); /* vertical stripes -- maximally direction-dependent */
            put(&im, x, y, on ? 240 : 15, on ? 240 : 15, on ? 240 : 15);
        }
    image_t rot = rotate_90(&im);

    ph_digest_t a, b;
    radial_of(&im, &a);
    radial_of(&rot, &b);

    double d = ph_l2_distance(&a, &b);
    ASSERT(d >= 0.0);

    /* Rotating stripes by 90 degrees moves every projection's variance to a different
     * angle. Under the source's comparison this is a match; element-wise it is a large
     * distance. Measured at ~1100 over 40 bytes; the floor is set well below that. */
    if (d < 200.0) {
        fprintf(stderr,
                "[FAIL] radial L2 distance under 90-degree rotation is only %.1f -- if this "
                "now matches, the cross-correlation comparison was implemented and this "
                "test should be replaced by one asserting invariance\n",
                d);
        exit(1);
    }

    printf("test_radial_is_not_rotation_invariant_today: PASSED (L2 = %.1f, divergence pinned)\n",
           d);

    image_free(&im);
    image_free(&rot);
}

int main(void) {
    test_robustness_discrimination_separability();
    test_radial_is_not_rotation_invariant_today();
    printf("ALL HASH PROPERTY TESTS PASSED\n");
    return 0;
}
