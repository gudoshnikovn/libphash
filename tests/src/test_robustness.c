// The other tests check *mechanics* (correct return codes, DCT/Haar math); this
// file checks the one property that actually makes a perceptual hash useful:
// similar images hash close together, and different images hash far apart.
//
// Base pixels are decoded once (via stb_image, declared here but already
// linked in via libphash.a -- see src/loaders/stb_image_impl.c) and
// then perturbed with plain, dependency-free C: resize (nearest-neighbor),
// crop, gamma, box blur, a synthetic watermark overlay. No Python, no
// external image tools, no new vendored dependency -- deliberately simpler
// transforms than a real JPEG re-encode at multiple quality levels, which
// would need vendoring an encoder or wiring one test binary to link
// libjpeg-turbo directly; not worth it for what this test needs to prove.
#include "libphash.h"
#include "test_macros.h"

#include "../../vendor/stb_image.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *pixels; // interleaved RGB, 3 bytes/pixel
    int w, h;
} rgb_image_t;

static rgb_image_t load_base(const char *path) {
    rgb_image_t img = {0};
    int channels;
    img.pixels = stbi_load(path, &img.w, &img.h, &channels, 3);
    if (!img.pixels) {
        fprintf(stderr, "[FAIL] test_robustness - could not decode fixture %s\n", path);
        exit(1);
    }
    return img;
}

static void free_image(rgb_image_t *img) {
    free(img->pixels);
    img->pixels = NULL;
}

static rgb_image_t resize_nn(const rgb_image_t *src, double scale) {
    rgb_image_t out;
    out.w = (int)(src->w * scale);
    out.h = (int)(src->h * scale);
    if (out.w < 1) {
        out.w = 1;
    }
    if (out.h < 1) {
        out.h = 1;
    }
    out.pixels = malloc((size_t)out.w * out.h * 3);
    for (int y = 0; y < out.h; y++) {
        int sy = (int)((double)y * src->h / out.h);
        if (sy >= src->h) {
            sy = src->h - 1;
        }
        for (int x = 0; x < out.w; x++) {
            int sx = (int)((double)x * src->w / out.w);
            if (sx >= src->w) {
                sx = src->w - 1;
            }
            memcpy(out.pixels + ((size_t)y * out.w + x) * 3,
                   src->pixels + ((size_t)sy * src->w + sx) * 3, 3);
        }
    }
    return out;
}

// Crops `pct` off each side (e.g. 5 => keeps the central 90% x 90%).
static rgb_image_t crop_pct(const rgb_image_t *src, int pct) {
    int dx = src->w * pct / 100;
    int dy = src->h * pct / 100;
    rgb_image_t out;
    out.w = src->w - 2 * dx;
    out.h = src->h - 2 * dy;
    out.pixels = malloc((size_t)out.w * out.h * 3);
    for (int y = 0; y < out.h; y++) {
        memcpy(out.pixels + (size_t)y * out.w * 3,
               src->pixels + ((size_t)(y + dy) * src->w + dx) * 3, (size_t)out.w * 3);
    }
    return out;
}

static rgb_image_t apply_gamma(const rgb_image_t *src, double gamma) {
    rgb_image_t out = {malloc((size_t)src->w * src->h * 3), src->w, src->h};
    size_t n = (size_t)src->w * src->h * 3;
    uint8_t lut[256];
    for (int i = 0; i < 256; i++) {
        double v = pow(i / 255.0, 1.0 / gamma) * 255.0;
        lut[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    for (size_t i = 0; i < n; i++) {
        out.pixels[i] = lut[src->pixels[i]];
    }
    return out;
}

// Cheap separable-ish 3x3 box blur (single pass, clamped edges) -- enough to
// simulate mild softening, not meant to be a quality filter.
static rgb_image_t box_blur3(const rgb_image_t *src) {
    rgb_image_t out = {malloc((size_t)src->w * src->h * 3), src->w, src->h};
    for (int y = 0; y < src->h; y++) {
        for (int x = 0; x < src->w; x++) {
            int sum[3] = {0, 0, 0};
            int count = 0;
            for (int dy = -1; dy <= 1; dy++) {
                int ny = y + dy;
                if (ny < 0 || ny >= src->h) {
                    continue;
                }
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx;
                    if (nx < 0 || nx >= src->w) {
                        continue;
                    }
                    const uint8_t *p = src->pixels + ((size_t)ny * src->w + nx) * 3;
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    count++;
                }
            }
            uint8_t *o = out.pixels + ((size_t)y * src->w + x) * 3;
            o[0] = (uint8_t)(sum[0] / count);
            o[1] = (uint8_t)(sum[1] / count);
            o[2] = (uint8_t)(sum[2] / count);
        }
    }
    return out;
}

// Alpha-blends a small gray patch (~1/8 x 1/8, 40% opacity) into a corner --
// standing in for a small semi-transparent logo/text watermark, not a large
// opaque occlusion.
static rgb_image_t add_watermark(const rgb_image_t *src) {
    rgb_image_t out = {malloc((size_t)src->w * src->h * 3), src->w, src->h};
    memcpy(out.pixels, src->pixels, (size_t)src->w * src->h * 3);
    int bw = src->w / 8, bh = src->h / 8;
    for (int y = src->h - bh; y < src->h; y++) {
        for (int x = src->w - bw; x < src->w; x++) {
            uint8_t *p = out.pixels + ((size_t)y * src->w + x) * 3;
            for (int c = 0; c < 3; c++) {
                p[c] = (uint8_t)(p[c] * 0.6 + 80 * 0.4);
            }
        }
    }
    return out;
}

static void hashes_of(const rgb_image_t *img, uint64_t out[PH_HASH_FLAGS_COUNT]) {
    ph_context_t *ctx;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, img->pixels, img->w, img->h, 3, 0));
    uint32_t flags = PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH;
    ASSERT_OK(ph_compute_multi(ctx, flags, out));
    ph_free(ctx);
}

// out[] layout matches PH_HASH_FLAGS_COUNT / ph_compute_multi's ascending-bit
// order: [aHash, dHash, pHash, wHash]. The arrays below are sized by
// PH_HASH_FLAGS_COUNT, so an extra initializer is a compile error on MSVC and a
// warning elsewhere.
static const char *ALGO_NAMES[PH_HASH_FLAGS_COUNT] = {"aHash", "dHash", "pHash", "wHash"};

typedef rgb_image_t (*transform_fn)(const rgb_image_t *);

static rgb_image_t t_half(const rgb_image_t *s) { return resize_nn(s, 0.5); }

static rgb_image_t t_double(const rgb_image_t *s) { return resize_nn(s, 2.0); }

static rgb_image_t t_crop5(const rgb_image_t *s) { return crop_pct(s, 5); }

static rgb_image_t t_gamma_up(const rgb_image_t *s) { return apply_gamma(s, 1.2); }

static rgb_image_t t_gamma_down(const rgb_image_t *s) { return apply_gamma(s, 0.8); }

static const struct {
    const char *name;
    transform_fn fn;
} TRANSFORMS[] = {
    {"resize 50%", t_half},       {"resize 200%", t_double},    {"crop 5%", t_crop5},
    {"gamma +20%", t_gamma_up},   {"gamma -20%", t_gamma_down}, {"light blur", box_blur3},
    {"watermark", add_watermark},
};

#define NUM_TRANSFORMS ((int)(sizeof(TRANSFORMS) / sizeof(TRANSFORMS[0])))

// Pairwise different pictures: a photograph, a smooth colour gradient and a synthetic
// wave texture.
static const char *DIFFERENT[] = {
    TEST_DATA_DIR "/photo.jpeg",
    TEST_DATA_DIR "/photo_complex.png",
    TEST_DATA_DIR "/photo_large.jpeg",
};
#define NUM_DIFFERENT ((int)(sizeof(DIFFERENT) / sizeof(DIFFERENT[0])))
#define NUM_PAIRS     (NUM_DIFFERENT * (NUM_DIFFERENT - 1) / 2)

// What this test measures, in bits out of 64, per algorithm: the largest distance
// between photo.jpeg and any transform of it, and the smallest between two different
// pictures. The test checks that its measurement still equals these numbers and prints
// the replacement rows when it does not: decoding goes through stb_image and every hash
// computes the same on every platform, so the numbers are exact everywhere.
//
// The contract is derived from them: a transform may land PH_ROB_MARGIN (30%) of the
// gap further out than measured, two different pictures 30% of the gap closer, and the
// two limits never meet -- a distance cannot satisfy both "the same picture" and "a
// different picture".
typedef struct {
    int max_similar;
    int min_different;
} observed_t;

// One row per line, in the layout the stale-measurement message prints.
// clang-format off
static const observed_t OBSERVED[PH_HASH_FLAGS_COUNT] = {
    {7, 21},  // aHash
    {9, 29},  // dHash
    {11, 30}, // pHash
    {9, 22},  // wHash
};
// clang-format on

#define PH_ROB_MARGIN 0.30

static void hashes_of_file(const char *path, uint64_t out[PH_HASH_FLAGS_COUNT]) {
    rgb_image_t img = load_base(path);
    hashes_of(&img, out);
    free_image(&img);
}

static void test_similar_and_different_separate(void) {
    int similar[NUM_TRANSFORMS][PH_HASH_FLAGS_COUNT];
    int different[NUM_PAIRS][PH_HASH_FLAGS_COUNT];
    int max_similar[PH_HASH_FLAGS_COUNT], min_different[PH_HASH_FLAGS_COUNT];
    uint64_t base_hashes[PH_HASH_FLAGS_COUNT], h[PH_HASH_FLAGS_COUNT];

    rgb_image_t base = load_base(DIFFERENT[0]);
    hashes_of(&base, base_hashes);
    for (int t = 0; t < NUM_TRANSFORMS; t++) {
        rgb_image_t v = TRANSFORMS[t].fn(&base);
        hashes_of(&v, h);
        free_image(&v);
        for (int a = 0; a < PH_HASH_FLAGS_COUNT; a++) {
            similar[t][a] = ph_hamming_distance(base_hashes[a], h[a]);
        }
    }
    free_image(&base);

    uint64_t picture[NUM_DIFFERENT][PH_HASH_FLAGS_COUNT];
    for (int i = 0; i < NUM_DIFFERENT; i++) {
        hashes_of_file(DIFFERENT[i], picture[i]);
    }
    int pair = 0;
    for (int i = 0; i < NUM_DIFFERENT; i++) {
        for (int j = i + 1; j < NUM_DIFFERENT; j++, pair++) {
            for (int a = 0; a < PH_HASH_FLAGS_COUNT; a++) {
                different[pair][a] = ph_hamming_distance(picture[i][a], picture[j][a]);
            }
        }
    }

    printf("  bits out of 64    ");
    for (int a = 0; a < PH_HASH_FLAGS_COUNT; a++) {
        printf("%7s", ALGO_NAMES[a]);
    }
    printf("\n");
    for (int t = 0; t < NUM_TRANSFORMS; t++) {
        printf("  %-18s", TRANSFORMS[t].name);
        for (int a = 0; a < PH_HASH_FLAGS_COUNT; a++) {
            printf("%7d", similar[t][a]);
        }
        printf("\n");
    }
    pair = 0;
    for (int i = 0; i < NUM_DIFFERENT; i++) {
        for (int j = i + 1; j < NUM_DIFFERENT; j++, pair++) {
            printf("  different %d-%d     ", i, j);
            for (int a = 0; a < PH_HASH_FLAGS_COUNT; a++) {
                printf("%7d", different[pair][a]);
            }
            printf("\n");
        }
    }

    int stale = 0;
    for (int a = 0; a < PH_HASH_FLAGS_COUNT; a++) {
        max_similar[a] = 0;
        min_different[a] = 64;
        for (int t = 0; t < NUM_TRANSFORMS; t++) {
            if (similar[t][a] > max_similar[a]) {
                max_similar[a] = similar[t][a];
            }
        }
        for (int p = 0; p < NUM_PAIRS; p++) {
            if (different[p][a] < min_different[a]) {
                min_different[a] = different[p][a];
            }
        }

        const observed_t *o = &OBSERVED[a];
        int gap = o->min_different - o->max_similar;
        int allowance = (int)(PH_ROB_MARGIN * gap); /* rounds down: the stricter side */
        int similar_limit = o->max_similar + allowance;
        int different_limit = o->min_different - allowance;

        if (gap <= 0 || similar_limit >= different_limit) {
            fprintf(stderr,
                    "[FAIL] test_robustness - %s: recorded transforms reach %d bits and "
                    "different pictures come as close as %d; the two do not separate\n",
                    ALGO_NAMES[a], o->max_similar, o->min_different);
            exit(1);
        }
        if (max_similar[a] > similar_limit) {
            fprintf(stderr,
                    "[FAIL] test_robustness - %s: a transform moves the hash %d bits, "
                    "beyond the limit %d\n",
                    ALGO_NAMES[a], max_similar[a], similar_limit);
            exit(1);
        }
        if (min_different[a] < different_limit) {
            fprintf(stderr,
                    "[FAIL] test_robustness - %s: two different pictures hash %d bits apart, "
                    "under the limit %d\n",
                    ALGO_NAMES[a], min_different[a], different_limit);
            exit(1);
        }
        if (max_similar[a] != o->max_similar || min_different[a] != o->min_different) {
            fprintf(stderr, "[STALE] %s: measured {%d, %d}, OBSERVED says {%d, %d}\n",
                    ALGO_NAMES[a], max_similar[a], min_different[a], o->max_similar,
                    o->min_different);
            stale = 1;
        }
    }
    if (stale) {
        fprintf(stderr, "\nOBSERVED does not match the measurement. If the change is "
                        "intended, replace the rows with:\n\n");
        for (int a = 0; a < PH_HASH_FLAGS_COUNT; a++) {
            char row[32];
            snprintf(row, sizeof(row), "{%d, %d},", max_similar[a], min_different[a]);
            fprintf(stderr, "    %-10s// %s\n", row, ALGO_NAMES[a]);
        }
        fprintf(stderr, "\nand say in the commit why the measurement moved.\n");
        exit(1);
    }

    printf("test_similar_and_different_separate: PASSED\n");
}

int main(void) {
    test_similar_and_different_separate();
    return 0;
}
