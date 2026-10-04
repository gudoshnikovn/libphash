/*
 * test_simd_equivalence.c
 *
 * Checks color.c's NEON grayscale path against its scalar fallback, and compare.c's
 * word-at-a-time Hamming distance against its byte-at-a-time twin. A mismatch here means
 * two different hashes (or distances) for the same input depending on which
 * architecture ran it -- exactly the class of bug that would otherwise surface as an
 * unexplained golden-hash mismatch.
 *
 * Every function below exists in two forms: the production one and a `_scalar` twin that
 * always takes the plain C path, declared in the src/ header next to it for this purpose
 * only. This test calls both on the same inputs and compares the outputs exactly: both
 * are integer arithmetic.
 *
 * Without NEON (__ARM_NEON undefined) the grayscale pair is literally the same code path,
 * so that comparison is tautological there -- the test still passes, it just isn't
 * exercising it. The Hamming pair differs on every target. There is no floating-point
 * SIMD path in the library to compare: the one place it would matter, pHash's DCT, is a
 * single plain loop on every architecture.
 */

#include "digest.h"
#include "hashes/hashes.h"
#include "image/image.h"
#include "libphash.h"
#include "test_macros.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The generator is shared from test_macros.h, so the corpus is identical across runs and
 * platforms. fill_random() has the same signature as the other fillers, hence the
 * file-scope state. */
static ph_test_rng_t g_rng;

static void fill_random(uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        buf[i] = ph_test_rng_byte(&g_rng);
    }
}

static void fill_gradient(uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)(i % 256);
    }
}

/* =========================================================
 * ph_to_grayscale vs ph_to_grayscale_scalar
 * ========================================================= */

static void run_grayscale_case(const char *label, int w, int h, int channels,
                               void (*fill)(uint8_t *, size_t)) {
    size_t num_pixels = (size_t)w * (size_t)h;
    size_t src_size = num_pixels * (size_t)channels;

    uint8_t *src = malloc(src_size);
    uint8_t *dst_simd = malloc(num_pixels);
    uint8_t *dst_scalar = malloc(num_pixels);
    ASSERT_PTR_NOT_NULL(src);
    ASSERT_PTR_NOT_NULL(dst_simd);
    ASSERT_PTR_NOT_NULL(dst_scalar);

    fill(src, src_size);
    /* poison both outputs differently so an unwritten byte shows up as a mismatch */
    memset(dst_simd, 0xAA, num_pixels);
    memset(dst_scalar, 0x55, num_pixels);

    ph_to_grayscale(NULL, src, w, h, channels, dst_simd);
    ph_to_grayscale_scalar(NULL, src, w, h, channels, dst_scalar);

    if (memcmp(dst_simd, dst_scalar, num_pixels) != 0) {
        for (size_t i = 0; i < num_pixels; i++) {
            ASSERT_MSG(dst_simd[i] == dst_scalar[i],
                       "ph_to_grayscale/%s (%dx%d, %d ch): byte %zu: simd=%u scalar=%u", label, w,
                       h, channels, i, dst_simd[i], dst_scalar[i]);
        }
    }

    free(src);
    free(dst_simd);
    free(dst_scalar);
}

static void test_grayscale_equivalence(void) {
    static const struct {
        int w, h;
    } shapes[] = {
        {1, 1},   {1, 7},  {7, 1},  {1, 8},   {8, 1},   {1, 9},   {9, 1},
        {7, 7},   {8, 8},  {9, 9},  {15, 15}, {16, 16}, {17, 17}, {32, 32},
        {33, 31}, {64, 1}, {1, 64}, {100, 3}, {3, 100},
    };

    size_t n_shapes = sizeof(shapes) / sizeof(shapes[0]);

    for (int channels = 3; channels <= 4; channels++) {
        for (size_t s = 0; s < n_shapes; s++) {
            g_rng = ph_test_rng(0x1234u + (uint32_t)s * 7u + (uint32_t)channels * 101u);
            run_grayscale_case("random", shapes[s].w, shapes[s].h, channels, fill_random);
            run_grayscale_case("gradient", shapes[s].w, shapes[s].h, channels, fill_gradient);
        }

        /* Monochrome buffers: every channel pinned to the same constant. */
        for (size_t s = 0; s < n_shapes; s++) {
            size_t num_pixels = (size_t)shapes[s].w * (size_t)shapes[s].h;
            size_t src_size = num_pixels * (size_t)channels;
            uint8_t *src = malloc(src_size);
            ASSERT_PTR_NOT_NULL(src);
            memset(src, 128, src_size);

            uint8_t *dst_simd = malloc(num_pixels);
            uint8_t *dst_scalar = malloc(num_pixels);
            ph_to_grayscale(NULL, src, shapes[s].w, shapes[s].h, channels, dst_simd);
            ph_to_grayscale_scalar(NULL, src, shapes[s].w, shapes[s].h, channels, dst_scalar);
            ASSERT(memcmp(dst_simd, dst_scalar, num_pixels) == 0);

            free(src);
            free(dst_simd);
            free(dst_scalar);
        }
    }

    PASS("test_grayscale_equivalence");
}

/* =========================================================
 * ph_hamming_distance_digest vs ph_hamming_distance_digest_scalar
 * ========================================================= */

static ph_digest_t make_bits_digest(uint8_t size, void (*fill)(uint8_t *, size_t)) {
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    fill(d.data, size);
    d.size = size;
    d.kind = PH_DIGEST_KIND_BITS;
    return d;
}

static void run_hamming_case(uint8_t size) {
    g_rng = ph_test_rng(0x1111u + size);
    ph_digest_t a = make_bits_digest(size, fill_random);
    g_rng = ph_test_rng(0x2222u + size);
    ph_digest_t b = make_bits_digest(size, fill_random);

    int simd = ph_hamming_distance_digest(&a, &b);
    int scalar = ph_hamming_distance_digest_scalar(&a, &b);
    ASSERT_MSG(simd == scalar, "ph_hamming_distance_digest (size=%u): simd=%d scalar=%d",
               (unsigned)size, simd, scalar);

    /* Self-distance is 0 on both paths -- except size 0, which both entry points reject
     * as incomparable (ph_digest_is_comparable() requires size > 0) rather than reporting
     * a distance of 0. */
    int expected_self = (size > 0) ? 0 : -1;
    ASSERT_INT_EQ(expected_self, ph_hamming_distance_digest(&a, &a));
    ASSERT_INT_EQ(expected_self, ph_hamming_distance_digest_scalar(&a, &a));
}

static void test_hamming_distance_equivalence(void) {
    static const int sizes[] = {0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        run_hamming_case((uint8_t)sizes[i]);
    }

    /* Identical digests, and fully-flipped digests, at a representative size. */
    ph_digest_t a = make_bits_digest(64, fill_gradient);
    ph_digest_t b = a;
    for (int i = 0; i < 64; i++) {
        b.data[i] = (uint8_t)~b.data[i];
    }
    ASSERT_INT_EQ(ph_hamming_distance_digest(&a, &b), ph_hamming_distance_digest_scalar(&a, &b));
    ASSERT_INT_EQ(512, ph_hamming_distance_digest(&a, &b)); /* 64 bytes fully flipped */

    PASS("test_hamming_distance_equivalence");
}

int main(void) {
    test_grayscale_equivalence();
    test_hamming_distance_equivalence();

    printf("\nAll SIMD/scalar equivalence tests passed!\n");
    return 0;
}
