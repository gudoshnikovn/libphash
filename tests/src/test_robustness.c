// Task 13: the 28+ existing test_*.c files check *mechanics* (correct return
// codes, DCT/Haar math) but nothing checks the one property that actually
// makes a perceptual hash useful: similar images hash close together, and
// different images hash far apart. This file is that contract.
//
// Base pixels are decoded once (via stb_image, declared here but already
// linked in via libphash.a -- see src/core.c's STB_IMAGE_IMPLEMENTATION) and
// then perturbed with plain, dependency-free C: resize (nearest-neighbor),
// crop, gamma, box blur, a synthetic watermark overlay. No Python, no
// external image tools, no new vendored dependency -- deliberately simpler
// transforms than a real JPEG re-encode at multiple quality levels, which
// would need vendoring an encoder or wiring one test binary to link
// TurboJPEG directly; not worth it for what this test needs to prove (see
// tasks/PROGRESS.md, task 13 notes).
#include "libphash.h"
#include "test_macros.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../vendor/stb_image.h"

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
    if (out.w < 1)
        out.w = 1;
    if (out.h < 1)
        out.h = 1;
    out.pixels = malloc((size_t)out.w * out.h * 3);
    for (int y = 0; y < out.h; y++) {
        int sy = (int)((double)y * src->h / out.h);
        if (sy >= src->h)
            sy = src->h - 1;
        for (int x = 0; x < out.w; x++) {
            int sx = (int)((double)x * src->w / out.w);
            if (sx >= src->w)
                sx = src->w - 1;
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
    for (size_t i = 0; i < n; i++)
        out.pixels[i] = lut[src->pixels[i]];
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
                if (ny < 0 || ny >= src->h)
                    continue;
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx;
                    if (nx < 0 || nx >= src->w)
                        continue;
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
            for (int c = 0; c < 3; c++)
                p[c] = (uint8_t)(p[c] * 0.6 + 80 * 0.4);
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
// order: [aHash, dHash, pHash, wHash, mHash, ColorHash].
static const char *ALGO_NAMES[PH_HASH_FLAGS_COUNT] = {"aHash", "dHash", "pHash",
                                                      "wHash", "mHash", "ColorHash"};

// Contract thresholds (out of 64 bits), per algorithm: a same-scene transform
// must stay at or under MAX_SIMILAR_DIST[algo], distinct images must clear
// MIN_DIFFERENT_DIST. Not uniform on purpose -- algorithms genuinely differ
// in robustness (e.g. mHash, being edge/gradient-based, is measurably more
// sensitive to a 5% crop than aHash or ColorHash are). Each threshold has
// headroom over what was actually measured against these fixtures +
// transforms during development (see tasks/PROGRESS.md, task 13 notes for
// the measured baseline); MIN_DIFFERENT_DIST is comfortably below the
// weakest observed different-image separation (ColorHash, ~13).
static const int MAX_SIMILAR_DIST[PH_HASH_FLAGS_COUNT] = {
    10, // aHash
    14, // dHash
    18, // pHash
    14, // wHash
    24, // mHash
    8,  // ColorHash
};
#define MIN_DIFFERENT_DIST 8

static void assert_similar(const char *label, const uint64_t base[PH_HASH_FLAGS_COUNT],
                           const uint64_t variant[PH_HASH_FLAGS_COUNT]) {
    for (int i = 0; i < PH_HASH_FLAGS_COUNT; i++) {
        int dist = ph_hamming_distance(base[i], variant[i]);
        if (dist > MAX_SIMILAR_DIST[i]) {
            fprintf(stderr,
                    "[FAIL] test_robustness - %s: %s distance %d exceeds contract (max %d)\n",
                    label, ALGO_NAMES[i], dist, MAX_SIMILAR_DIST[i]);
            exit(1);
        }
    }
    printf("  %s: OK (all algorithms within contract)\n", label);
}

void test_transform_robustness(void) {
    rgb_image_t base = load_base(TEST_DATA_DIR "/photo.jpeg");
    uint64_t base_hashes[PH_HASH_FLAGS_COUNT];
    hashes_of(&base, base_hashes);

    printf("test_transform_robustness:\n");

    rgb_image_t v;

    v = resize_nn(&base, 0.5);
    uint64_t h1[PH_HASH_FLAGS_COUNT];
    hashes_of(&v, h1);
    assert_similar("resize 50%", base_hashes, h1);
    free_image(&v);

    v = resize_nn(&base, 2.0);
    hashes_of(&v, h1);
    assert_similar("resize 200%", base_hashes, h1);
    free_image(&v);

    v = crop_pct(&base, 5);
    hashes_of(&v, h1);
    assert_similar("crop 5%", base_hashes, h1);
    free_image(&v);

    v = apply_gamma(&base, 1.2);
    hashes_of(&v, h1);
    assert_similar("gamma +20%", base_hashes, h1);
    free_image(&v);

    v = apply_gamma(&base, 0.8);
    hashes_of(&v, h1);
    assert_similar("gamma -20%", base_hashes, h1);
    free_image(&v);

    v = box_blur3(&base);
    hashes_of(&v, h1);
    assert_similar("light blur", base_hashes, h1);
    free_image(&v);

    v = add_watermark(&base);
    hashes_of(&v, h1);
    assert_similar("watermark", base_hashes, h1);
    free_image(&v);

    free_image(&base);
    printf("test_transform_robustness: PASSED\n");
}

void test_different_images_diverge(void) {
    rgb_image_t a = load_base(TEST_DATA_DIR "/photo.jpeg");
    rgb_image_t b = load_base(TEST_DATA_DIR "/photo_complex.png");

    uint64_t ha[PH_HASH_FLAGS_COUNT], hb[PH_HASH_FLAGS_COUNT];
    hashes_of(&a, ha);
    hashes_of(&b, hb);

    for (int i = 0; i < PH_HASH_FLAGS_COUNT; i++) {
        int dist = ph_hamming_distance(ha[i], hb[i]);
        if (dist < MIN_DIFFERENT_DIST) {
            fprintf(stderr,
                    "[FAIL] test_different_images_diverge - %s distance %d is below contract "
                    "(min %d) for genuinely different images\n",
                    ALGO_NAMES[i], dist, MIN_DIFFERENT_DIST);
            exit(1);
        }
    }

    free_image(&a);
    free_image(&b);
    printf("test_different_images_diverge: PASSED\n");
}

int main(void) {
    test_transform_robustness();
    test_different_images_diverge();
    return 0;
}
