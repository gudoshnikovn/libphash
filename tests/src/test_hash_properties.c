/*
 * test_hash_properties.c
 *
 * Two of the nine hashes -- wHash and ColorHash -- have no primary source
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

#include "internal.h"
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
 * Deliberately no rotation: only pHash and Radial tolerate one at all, and Radial's is
 * partial until the comparison from its source lands (see the dedicated test at the
 * end). Mixing a rotation into the "benign" set would make every algorithm look bad for
 * a reason that has nothing to do with the property being measured.
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

typedef enum {
    A_AHASH,
    A_DHASH,
    A_PHASH,
    A_WHASH,
    A_MHASH,
    A_BMH,
    A_COLOR,
    A_RADIAL,
    A_COUNT
} algo_t;

static const char *ALGO_NAMES[A_COUNT] = {"aHash", "dHash",     "pHash",  "wHash",  "mHash",
                                          "BMH",   "ColorHash", "Radial", "Rad/PCC"};

typedef struct {
    uint64_t bits;   /* the 64-bit algorithms */
    ph_digest_t dig; /* BMH, mHash, Radial, ColorHash */
} hash_set_t;

static void hash_image(const image_t *im, hash_set_t out[A_COUNT]) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, im->px, im->w, im->h, 3, 0));

    ASSERT_OK(ph_compute_ahash(ctx, &out[A_AHASH].bits));
    ASSERT_OK(ph_compute_dhash(ctx, &out[A_DHASH].bits));
    ASSERT_OK(ph_compute_phash(ctx, &out[A_PHASH].bits));
    ASSERT_OK(ph_compute_whash(ctx, &out[A_WHASH].bits));
    ASSERT_OK(ph_compute_mhash(ctx, &out[A_MHASH].dig));
    ASSERT_OK(ph_compute_bmh(ctx, &out[A_BMH].dig));
    ASSERT_OK(ph_compute_color_hash(ctx, &out[A_COLOR].dig));
    ASSERT_OK(ph_compute_radial_hash(ctx, &out[A_RADIAL].dig));

    ph_free(ctx);
}

static double distance(algo_t a, const hash_set_t *x, const hash_set_t *y) {
    /* Radial is the one algorithm here whose digest is not a bit vector: its bytes are
     * quantised DCT coefficients, so Hamming distance over them means nothing and the
     * comparison is L2, normalised by the largest L2 two 40-byte digests can be apart. */
    /* Radial is compared the way its source specifies -- the peak of the
     * cross-correlation -- mapped from [-1, 1] onto a distance in [0, 1] so that it sits
     * on the same scale as every other row here. There is deliberately no L2 row for it:
     * the digest is tagged PH_DIGEST_KIND_COEFFICIENTS and ph_l2_distance() refuses it,
     * which is the point of the tag. The pre-2.0.0 L2 numbers are recorded in
     * docs/algorithm-provenance.md section 7 instead. */
    /* ColorHash is a histogram: the measure is its intersection, turned into a distance
     * so that it sits on the same scale as the rest. */
    if (a == A_COLOR) {
        double inter = 0.0;
        ASSERT_OK(ph_histogram_intersection(&x[a].dig, &y[a].dig, &inter));
        return 1.0 - inter;
    }
    if (a == A_RADIAL) {
        double pcc = 0.0;
        ASSERT_OK(ph_radial_similarity(&x[a].dig, &y[a].dig, &pcc));
        return (1.0 - pcc) / 2.0;
    }
    if (a == A_BMH || a == A_MHASH) {
        int d = ph_hamming_distance_digest(&x[a].dig, &y[a].dig);
        ASSERT(d >= 0);
        return (double)d / (x[a].dig.size * 8.0);
    }
    return ph_hamming_distance(x[a].bits, y[a].bits) / 64.0;
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
    [A_AHASH] = {2.50, 0.100, 0.400},  /* 3.54 / 0.052 / 0.488 */
    [A_DHASH] = {2.50, 0.120, 0.380},  /* 3.60 / 0.066 / 0.469 */
    [A_PHASH] = {1.80, 0.260, 0.400},  /* 2.48 / 0.177 / 0.490 */
    [A_WHASH] = {3.00, 0.080, 0.390},  /* 4.34 / 0.036 / 0.480 */
    [A_MHASH] = {1.80, 0.200, 0.380},  /* 2.49 / 0.165 / 0.487 -- but read the note */
    [A_BMH] = {3.50, 0.070, 0.390},    /* 5.21 / 0.031 / 0.478 */
    [A_COLOR] = {2.80, 0.130, 0.700},  /* 3.95 / 0.089 / 0.849 */
    [A_RADIAL] = {1.80, 0.070, 0.180}, /* 2.46 / 0.032 / 0.263, by cross-correlation */
};

/* This corpus understates any algorithm that normalises to a fixed size larger than
 * IMG_W. mHash normalises to 512 and these images are 128, so every one of them is
 * upscaled fourfold before it is filtered, while the benign transformations resample them
 * again on top of that. Measured: on this corpus mHash separates at 2.49; on the same
 * corpus generated at 300x300 it separates at 2.70 and is second only to BMH, against
 * aHash 2.31, dHash 2.07 and pHash 1.89. The absolute numbers of every algorithm move
 * with the corpus size, so they are comparable within one run of this file and nowhere
 * else -- which is what the thresholds below are for. Making the corpus resolution
 * representative is filed separately; changing it here would move every number in this
 * file and in the documentation at once.
 *
 * Three things the measurement says that are worth reading off it rather than assuming.
 *
 * ColorHash's inter-distance is the highest here -- 0.849, where the structural hashes
 * sit near 0.48 -- because its distance is one minus a histogram intersection, and two
 * unrelated pictures share little colour. That is a different scale from a normalised
 * Hamming distance, whose expectation between unrelated hashes is 0.5 by construction.
 * Compare its separability with the others; do not compare its raw distances with
 * theirs. Until 2.0.0 it read 1.89 / 0.031 / 0.123, when it was the 42-bit ImageHash
 * port -- most of whose bins were empty for most images, so unrelated pictures agreed on
 * a great many zeroes.
 *
 * Radial's distances are not bits but quantised DCT coefficients compared by L2, so its
 * row is on a different footing from the rest even after normalisation; read its
 * separability, and do not compare its absolute distances with anyone else's. Its inter
 * mean is low for the same reason ColorHash's is -- one byte of its digest (coefficient
 * 0) is 255 for every image, and the affine quantisation squeezes the rest of the
 * coefficients into whatever range is left under it. Applying the DCT of the source in
 * 2.0.0 moved it from 0.021 / 0.344 / 2.80 to 0.007 / 0.188 / 2.12: three times more
 * robust, less well discriminated, and measured under a comparison the source does not
 * use -- see docs/algorithm-provenance.md section 7.
 *
 * pHash has the worst robustness of the structural hashes here -- mean intra-distance
 * 0.177 against 0.03-0.07 for the others -- and the second-lowest separability. The DC
 * coefficient was the suspect and has been ruled out: taking it out of the median leaves
 * this number at 0.177 to three decimals, and taking it out of the hash entirely (the
 * 8x8 block at DCT(1,1)) makes it worse, 0.190 with separability 2.27. A median is not
 * dragged by an outlier, whatever the received explanation says. The cause is elsewhere
 * and has not been found; docs/algorithm-provenance.md section 3 has the measurement. */

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
 * Radial: how much of a rotation the transform absorbs on its own
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

/* Raw projection variances, before the transform -- the vector a rotation acts on. */
static void variance_vector(const image_t *im, double *out, int n) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, im->px, im->w, im->h, 3, 0));
    /* Same geometry as src/hashes/radial.c, on the plain grayscale buffer: this is about
     * the projection geometry, not about the blur and gamma in front of it. */
    uint8_t *gray = (uint8_t *)malloc((size_t)im->w * im->h);
    ASSERT_PTR_NOT_NULL(gray);
    for (int i = 0; i < im->w * im->h; i++) {
        const uint8_t *p = &im->px[(size_t)i * 3];
        gray[i] = (uint8_t)((38 * p[0] + 75 * p[1] + 15 * p[2]) >> 7);
    }
    double cx = im->w / 2.0, cy = im->h / 2.0;
    double mr = (im->w < im->h ? im->w : im->h) / 2.0;
    for (int i = 0; i < n; i++) {
        double t = i * M_PI / n;
        out[i] = ph_projection_variance(gray, im->w, im->h, cx, cy, mr, (float)cos(t),
                                        (float)sin(t), 128);
    }
    free(gray);
    ph_free(ctx);
}

static double best_cyclic_correlation(const double *a, const double *b, int n, int *out_shift) {
    double ma = 0.0, mb = 0.0;
    for (int i = 0; i < n; i++) {
        ma += a[i];
        mb += b[i];
    }
    ma /= n;
    mb /= n;
    double va = 0.0, vb = 0.0;
    for (int i = 0; i < n; i++) {
        va += (a[i] - ma) * (a[i] - ma);
        vb += (b[i] - mb) * (b[i] - mb);
    }
    double best = -2.0;
    for (int d = 0; d < n; d++) {
        double num = 0.0;
        for (int i = 0; i < n; i++)
            num += (a[i] - ma) * (b[(n + i - d) % n] - mb);
        double r = num / sqrt(va * vb);
        if (r > best) {
            best = r;
            *out_shift = d;
        }
    }
    return best;
}

/* Rotation about the centre with bilinear resampling. The radial hash only samples
 * within min(w,h)/2 of the centre, so every point it reads comes from inside the source's
 * own inscribed disc and the corners the rotation leaves empty are never looked at. */
static image_t rotate_by(const image_t *s, double deg) {
    image_t o = image_new(s->w, s->h);
    memset(o.px, 0, (size_t)s->w * s->h * 3);
    double a = deg * M_PI / 180.0, ca = cos(a), sa = sin(a);
    double cx = (s->w - 1) / 2.0, cy = (s->h - 1) / 2.0;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            double dx = x - cx, dy = y - cy;
            double sx = cx + dx * ca + dy * sa;
            double sy = cy - dx * sa + dy * ca;
            if (sx < 0 || sy < 0 || sx >= s->w - 1 || sy >= s->h - 1)
                continue;
            int x0 = (int)sx, y0 = (int)sy;
            double fx = sx - x0, fy = sy - y0;
            for (int c = 0; c < 3; c++) {
                double p = s->px[((size_t)y0 * s->w + x0) * 3 + c] * (1 - fx) * (1 - fy) +
                           s->px[((size_t)y0 * s->w + x0 + 1) * 3 + c] * fx * (1 - fy) +
                           s->px[((size_t)(y0 + 1) * s->w + x0) * 3 + c] * (1 - fx) * fy +
                           s->px[((size_t)(y0 + 1) * s->w + x0 + 1) * 3 + c] * fx * fy;
                o.px[((size_t)y * o.w + x) * 3 + c] = (uint8_t)(p + 0.5);
            }
        }
    return o;
}

/* The rotation profile over the corpus.
 *
 * Read this as a lower bound, not as the algorithm's behaviour on photographs. Every
 * image here is 128x128 and deliberately high-frequency -- 3-pixel stripes, 4-pixel
 * checkerboards, additive noise -- and on that content a one-degree resampling changes
 * the pixels enough to move the variance profile on its own. The same measurement on the
 * real photographs in tests/data (tests/src/test_radial.c) gives 0.99 at 1 degree and
 * 0.94 at 3. Both are worth having: this one says what happens when the content is all
 * detail, that one says what happens on a picture. */
static void test_radial_rotation_profile(void) {
    static const double ANGLES[] = {1, 2, 5, 10, 15, 30, 45, 90, 180};
    const int NA = (int)(sizeof(ANGLES) / sizeof(ANGLES[0]));

    image_t base[NUM_BASE];
    ph_digest_t ref[NUM_BASE];
    for (int i = 0; i < NUM_BASE; i++) {
        base[i] = make_base(i);
        /* The reference goes through the same resampler at 0 degrees, so the numbers
         * below measure the rotation and not the interpolator. */
        image_t r0 = rotate_by(&base[i], 0.0);
        radial_of(&r0, &ref[i]);
        image_free(&r0);
    }

    /* Baseline: what an unrelated image scores. A rotation is only "absorbed" if it
     * scores clearly above this. */
    stats_t unrel;
    stats_init(&unrel);
    for (int i = 0; i < NUM_BASE; i++)
        for (int j = i + 1; j < NUM_BASE; j++) {
            double p = 0.0;
            ASSERT_OK(ph_radial_similarity(&ref[i], &ref[j], &p));
            stats_add(&unrel, p);
        }

    printf("\n  radial: peak cross-correlation against rotation (%d images)\n", NUM_BASE);
    printf("    unrelated images: mean %.3f, max %.3f\n", stats_mean(&unrel), unrel.max);
    printf("    %8s %8s %8s %8s\n", "angle", "mean", "min", ">=0.9");

    double mean_at[16];
    for (int a = 0; a < NA; a++) {
        stats_t st;
        stats_init(&st);
        int matched = 0;
        for (int i = 0; i < NUM_BASE; i++) {
            image_t r = rotate_by(&base[i], ANGLES[a]);
            ph_digest_t d;
            radial_of(&r, &d);
            double p = 0.0;
            ASSERT_OK(ph_radial_similarity(&ref[i], &d, &p));
            stats_add(&st, p);
            if (p >= PH_RADIAL_PCC_THRESHOLD)
                matched++;
            image_free(&r);
        }
        mean_at[a] = stats_mean(&st);
        printf("    %6.0f\u00b0 %8.3f %8.3f %6d/%d\n", ANGLES[a], stats_mean(&st), st.min, matched,
               NUM_BASE);
    }

    for (int i = 0; i < NUM_BASE; i++)
        image_free(&base[i]);

    /* A half turn is the identity on the projections -- the line at alpha and at
     * alpha+180 is the same line -- so it must match on every image, whatever the
     * content. Measured: mean 0.986, worst 0.943. */
    if (mean_at[NA - 1] < PH_RADIAL_PCC_THRESHOLD) {
        fprintf(stderr, "[FAIL] a half turn averages %.3f, below the threshold %.2f\n",
                mean_at[NA - 1], PH_RADIAL_PCC_THRESHOLD);
        exit(1);
    }
    /* And a small rotation still has to beat an unrelated image even here. Measured:
     * 0.764 at one degree against 0.599 for an unrelated pair. */
    if (mean_at[0] <= stats_mean(&unrel)) {
        fprintf(stderr,
                "[FAIL] a one-degree rotation averages %.3f, no better than the %.3f an "
                "unrelated image scores\n",
                mean_at[0], stats_mean(&unrel));
        exit(1);
    }

    printf("test_radial_rotation_profile: PASSED\n");
}

/* Where the rotation tolerance comes from, and where it stops.
 *
 * The mechanism the literature describes is real and this implementation has it: a
 * rotation cyclically shifts the vector of per-angle variances. The first half of this
 * test measures that directly -- a quarter turn shifts the 180-element vector by exactly
 * 90 places, and the two vectors correlate at 0.9997.
 *
 * The hash is not that vector. It is 40 DCT coefficients of it, and the DCT is not
 * shift-equivariant: a cyclic shift of a signal is not a cyclic shift of its transform.
 * So the tolerance the algorithm actually delivers is the tolerance of a transform to a
 * small perturbation -- a few degrees, measured in test_radial_rotation_profile() and on
 * real photographs in tests/src/test_radial.c -- and not invariance to an arbitrary
 * rotation. A quarter turn is not absorbed, and no comparison of these 40 coefficients
 * can absorb it, pHash's peak of cross-correlation included; that comparison maximises
 * over shifts of the coefficients, which is not the group a rotation acts through.
 *
 * A half turn is the exception and it has nothing to do with the transform: a projection
 * line at alpha and at alpha+180 is the same line, so a half turn is the identity on the
 * variance vector before the DCT ever runs.
 *
 * The second half pins that boundary, so that a change of representation -- to something
 * a cyclic shift does not destroy -- has to come through this test. */
static void test_radial_rotation_survives_the_projections_not_the_transform(void) {
    image_t im = image_new(128, 128);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            int on = ((x / 6) & 1); /* vertical stripes -- maximally direction-dependent */
            put(&im, x, y, on ? 240 : 15, on ? 240 : 15, on ? 240 : 15);
        }
    image_t r90 = rotate_90(&im);
    image_t r180 = rotate_90(&r90);
    image_t r270 = rotate_90(&r180);
    image_t other = make_base(4); /* a different family entirely */

    /* 1. The variance vector still carries the rotation, as a clean cyclic shift. */
    const int N = 180;
    double va[180], vb[180];
    variance_vector(&im, va, N);
    variance_vector(&r90, vb, N);
    int shift = -1;
    double raw = best_cyclic_correlation(va, vb, N, &shift);
    printf("\n  radial rotation\n");
    printf("    variance vector, quarter turn: correlation %.4f at shift %d of %d\n", raw, shift,
           N);
    if (raw < 0.95 || shift != N / 2) {
        fprintf(stderr,
                "[FAIL] a quarter turn no longer shifts the variance vector by a quarter of the "
                "circle (%.4f at shift %d, expected ~1.0 at %d) -- the projection geometry "
                "changed\n",
                raw, shift, N / 2);
        exit(1);
    }

    /* 2. The transform does not carry it, and the source's comparison cannot get it back. */
    ph_digest_t a, b90, b180, b270, c;
    radial_of(&im, &a);
    radial_of(&r90, &b90);
    radial_of(&r180, &b180);
    radial_of(&r270, &b270);
    radial_of(&other, &c);

    double p90 = 0, p180 = 0, p270 = 0, punrel = 0;
    ASSERT_OK(ph_radial_similarity(&a, &b90, &p90));
    ASSERT_OK(ph_radial_similarity(&a, &b180, &p180));
    ASSERT_OK(ph_radial_similarity(&a, &b270, &p270));
    ASSERT_OK(ph_radial_similarity(&a, &c, &punrel));
    printf("    digest: 90 %.4f  180 %.4f  270 %.4f   unrelated %.4f\n", p90, p180, p270, punrel);

    /* A half turn is the identity on the projections, so it must match. Measured 0.9951. */
    if (p180 < PH_RADIAL_PCC_THRESHOLD) {
        fprintf(stderr, "[FAIL] a half turn scores %.4f, below the source's threshold %.2f\n", p180,
                PH_RADIAL_PCC_THRESHOLD);
        exit(1);
    }
    /* And the quarter turns do not. Measured 0.19 and 0.13 against 0.33 for an unrelated
     * image. Asserted as the current, defective behaviour: if a representation that
     * survives a shift is ever adopted, this has to be replaced by a test of invariance. */
    if (p90 >= PH_RADIAL_PCC_THRESHOLD || p270 >= PH_RADIAL_PCC_THRESHOLD) {
        fprintf(stderr,
                "[FAIL] quarter turns now score %.4f / %.4f, at or above the threshold -- if the "
                "representation was changed to survive a shift, replace this test with one "
                "asserting invariance\n",
                p90, p270);
        exit(1);
    }

    printf("test_radial_rotation_survives_the_projections_not_the_transform: PASSED (divergence "
           "pinned)\n");

    image_free(&im);
    image_free(&r90);
    image_free(&r180);
    image_free(&r270);
    image_free(&other);
}

int main(void) {
    test_robustness_discrimination_separability();
    test_radial_rotation_profile();
    test_radial_rotation_survives_the_projections_not_the_transform();
    printf("ALL HASH PROPERTY TESTS PASSED\n");
    return 0;
}
