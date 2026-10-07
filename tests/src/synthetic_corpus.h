/*
 * synthetic_corpus.h
 *
 * The synthetic corpus: 24 generated images, and the benign edits applied to them. One
 * definition, used by tests/src/test_hash_properties.c, which measures robustness,
 * discrimination and separability on it, and by the documentation site's measuring tool
 * (tools/site/stages.c), which draws the same measurements, so a chart on the site and
 * the test's OBSERVED table describe the same images.
 *
 * Generated rather than loaded, from a fixed seed: it needs no network, adds nothing to
 * the repository, and reproduces byte for byte on every platform. Synthetic images are
 * not photographs; docs/methodology.md, "The corpus", records what that costs.
 *
 * Header-only (static inline), so each program that includes it gets its own copy and
 * neither has to link the other.
 */
#ifndef PH_TESTS_SRC_SYNTHETIC_CORPUS_H
#define PH_TESTS_SRC_SYNTHETIC_CORPUS_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 160x160, deliberately not equal to any normalisation preset in the library (8 for
 * aHash/dHash, 16 for BMH's default block_size, 32 for pHash's default dct_size, 512 for
 * mHash) and not a power of two like every one of them, so no algorithm's resize from
 * this corpus is ever a no-op or a suspiciously round ratio. mHash upsamples it 3.2x to
 * reach its 512 default, which understates algorithms that normalise larger than the
 * corpus -- see the note on BASE_RES below for why the absolute resolution matters.
 *
 * 160 is also close to the largest this corpus can be raised without retuning anything
 * else: the pinned half-turn floor of test_radial_rotation_profile() in
 * test_hash_properties.c (PH_RADIAL_PCC_THRESHOLD,
 * an independent threshold) is a fixed-sample-count property of
 * ph_compute_radial_hash() (PH_RADIAL_SAMPLES = 128 samples per projection, not scaled to
 * image size), so a bigger corpus radius means coarser sampling relative to its own
 * content and a noisier half-turn match: measured mean peak correlation at a half turn
 * across the corpus, same generator, only IMG_W varied -- 144: 0.984, 160: 0.965, 176:
 * 0.928, 192: 0.867 (fails the 0.90 floor), 200: 0.849 (fails). 160 keeps a real margin
 * (0.965) without changing that threshold. */
#define IMG_W    160
#define IMG_H    160
#define NUM_BASE 24

/* The reference resolution for make_base() feature sizes: checkerboard cells, stripe
 * widths, ring periods, disc radii and sinusoid frequencies are all expressed as
 * fractions of the frame via (IMG_W / BASE_RES), so every family's structure scales with
 * IMG_W and changing IMG_W on its own is safe. Sizes hardcoded in pixels would make the
 * feature size in fraction-of-frame terms change silently with the resolution. */
#define BASE_RES 128.0

typedef struct {
    uint8_t *px; /* interleaved RGB */
    int w, h;
} image_t;

/* Deterministic PRNG. Not a good one; it only has to be the same everywhere. */
static uint32_t rng_state;

static inline void rng_seed(uint32_t s) { rng_state = s ? s : 1u; }

static inline uint32_t rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static inline image_t image_new(int w, int h) {
    image_t im;
    im.w = w;
    im.h = h;
    im.px = (uint8_t *)malloc((size_t)w * h * 3);
    if (!im.px) {
        fprintf(stderr, "synthetic corpus: out of memory for a %dx%d image\n", w, h);
        exit(1);
    }
    return im;
}

static inline void image_free(image_t *im) {
    free(im->px);
    im->px = NULL;
}

static inline void put(image_t *im, int x, int y, int r, int g, int b) {
    if (x < 0 || y < 0 || x >= im->w || y >= im->h) {
        return;
    }
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
 * Every family is also coloured, and differently. A mostly grayscale corpus would put
 * every pixel in ColorHash's black and grey buckets and measure it on input it cannot
 * see; a colour algorithm needs colour in the corpus or the measurement means nothing.
 * ------------------------------------------------------------------------ */
static inline image_t make_base(int index) {
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
                    int cell = (int)((4 << variant) * (IMG_W / BASE_RES) + 0.5);
                    if (cell < 1) {
                        cell = 1;
                    }
                    int on = ((x / cell) + (y / cell)) & 1;
                    r = on ? 235 : 30;
                    g = on ? 40 : 200;
                    b = on ? 90 : 60;
                    break;
                }
                case 2: { /* concentric rings, varying period, cyan/magenta */
                    int dx = x - IMG_W / 2, dy = y - IMG_H / 2;
                    double d = sqrt((double)(dx * dx + dy * dy));
                    double period = (3.0 + variant * 2.0) * (IMG_W / BASE_RES);
                    double w = sin(d / period);
                    r = (int)(127.5 + 110.0 * w);
                    g = (int)(127.5 - 110.0 * w);
                    b = (int)(127.5 + 110.0 * sin(d / 7.0));
                    break;
                }
                case 3: { /* vertical stripes, varying width and colour */
                    int w = (int)((3 + variant * 4) * (IMG_W / BASE_RES) + 0.5);
                    if (w < 1) {
                        w = 1;
                    }
                    int on = (x / w) & 1;
                    r = on ? 200 : 40;
                    g = on ? 60 : 180;
                    b = on ? 120 : 90;
                    break;
                }
                case 4: { /* filled disc on a flat field, varying radius and colour */
                    int dx = x - IMG_W / 3, dy = y - IMG_H / 2;
                    int rad = (int)((20 + variant * 12) * (IMG_W / BASE_RES) + 0.5);
                    int inside = dx * dx + dy * dy < rad * rad;
                    r = inside ? 220 : 25;
                    g = inside ? 30 : 150;
                    b = inside ? 60 : 230;
                    break;
                }
                case 5: { /* smooth 2-D sinusoid, varying frequency, channels out of phase */
                    double f = (0.05 + variant * 0.04) * (BASE_RES / IMG_W);
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
 * Deliberately no rotation: only pHash and Radial tolerate one at all, and Radial only a
 * few degrees and a half turn (see the dedicated tests in test_hash_properties.c). Mixing
 * a rotation into the "benign" set would make every algorithm look bad for a reason that
 * has nothing to do with the property being measured.
 * ------------------------------------------------------------------------ */

static inline image_t xf_identity(const image_t *s) {
    image_t o = image_new(s->w, s->h);
    memcpy(o.px, s->px, (size_t)s->w * s->h * 3);
    return o;
}

static inline image_t xf_scale(const image_t *s, double f) {
    int w = (int)(s->w * f), h = (int)(s->h * f);
    if (w < 8) {
        w = 8;
    }
    if (h < 8) {
        h = 8;
    }
    image_t o = image_new(w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sx = (int)((x + 0.5) / f), sy = (int)((y + 0.5) / f);
            if (sx >= s->w) {
                sx = s->w - 1;
            }
            if (sy >= s->h) {
                sy = s->h - 1;
            }
            size_t si = ((size_t)sy * s->w + sx) * 3;
            put(&o, x, y, s->px[si], s->px[si + 1], s->px[si + 2]);
        }
    }
    return o;
}

static inline image_t xf_scale_down(const image_t *s) { return xf_scale(s, 0.5); }

static inline image_t xf_scale_up(const image_t *s) { return xf_scale(s, 1.75); }

static inline image_t xf_crop(const image_t *s) { /* 4% off every edge */
    int mx = s->w * 4 / 100, my = s->h * 4 / 100;
    int w = s->w - 2 * mx, h = s->h - 2 * my;
    image_t o = image_new(w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t si = ((size_t)(y + my) * s->w + (x + mx)) * 3;
            put(&o, x, y, s->px[si], s->px[si + 1], s->px[si + 2]);
        }
    }
    return o;
}

static inline image_t xf_brighter(const image_t *s) {
    image_t o = xf_identity(s);
    for (size_t i = 0; i < (size_t)s->w * s->h * 3; i++) {
        int v = o.px[i] + 25;
        o.px[i] = (uint8_t)(v > 255 ? 255 : v);
    }
    return o;
}

static inline image_t xf_gamma(const image_t *s) { /* gamma 1.4, a non-linear tone change */
    image_t o = xf_identity(s);
    uint8_t lut[256];
    for (int i = 0; i < 256; i++) {
        lut[i] = (uint8_t)(pow(i / 255.0, 1.0 / 1.4) * 255.0 + 0.5);
    }
    for (size_t i = 0; i < (size_t)s->w * s->h * 3; i++) {
        o.px[i] = lut[o.px[i]];
    }
    return o;
}

static inline image_t xf_blur(const image_t *s) { /* 3x3 box blur */
    image_t o = image_new(s->w, s->h);
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            for (int c = 0; c < 3; c++) {
                int sum = 0, n = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= s->w || yy >= s->h) {
                            continue;
                        }
                        sum += s->px[((size_t)yy * s->w + xx) * 3 + c];
                        n++;
                    }
                }
                o.px[((size_t)y * o.w + x) * 3 + c] = (uint8_t)(sum / n);
            }
        }
    }
    return o;
}

static inline image_t xf_noise(const image_t *s) { /* +/-12 of additive noise */
    image_t o = xf_identity(s);
    rng_seed(0xBEEFu);
    for (size_t i = 0; i < (size_t)s->w * s->h * 3; i++) {
        int v = o.px[i] + (int)(rng_next() % 25u) - 12;
        o.px[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    return o;
}

#endif /* PH_TESTS_SRC_SYNTHETIC_CORPUS_H */
