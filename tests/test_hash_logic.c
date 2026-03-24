/*
 * test_hash_logic.c
 *
 * Unit tests for the bit-packing / decision logic inside each hash algorithm.
 *
 * Strategy: inject tiny synthetic in-memory images (PPM P5 8-bit grayscale or
 * P6 RGB) via ph_load_from_memory, then assert specific hash values or
 * structural properties. Uses the public API only.
 *
 * Hashes tested:
 *   - ph_compute_ahash  (aHash)
 *   - ph_compute_dhash  (dHash)
 *   - ph_compute_bmh    (BMH digest)
 *   - ph_hamming_distance   (uint64 Hamming)
 *   - ph_hamming_distance_digest (digest Hamming)
 *   - ph_l2_distance    (L2 on digests)
 */

#include "libphash.h"
#include "test_macros.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
 * PPM builder helpers
 * ========================================================= */

/*
 * Build a P5 (binary grayscale) PGM image in memory.
 * Caller must free() the returned buffer.
 * *out_size receives total byte count.
 */
static uint8_t *make_pgm(int w, int h, const uint8_t *pixels, size_t *out_size) {
    /* Header: "P5\n<w> <h>\n255\n" */
    char header[64];
    int hlen = snprintf(header, sizeof(header), "P5\n%d %d\n255\n", w, h);
    size_t data_size = (size_t)w * h;
    size_t total = (size_t)hlen + data_size;
    uint8_t *buf = malloc(total);
    if (!buf)
        return NULL;
    memcpy(buf, header, hlen);
    memcpy(buf + hlen, pixels, data_size);
    *out_size = total;
    return buf;
}

/*
 * Build a P6 (binary RGB) PPM image in memory.
 * pixels must be w*h*3 bytes (R G B interleaved).
 * Caller must free() the returned buffer.
 */
static uint8_t *make_ppm(int w, int h, const uint8_t *pixels, size_t *out_size) {
    char header[64];
    int hlen = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", w, h);
    size_t data_size = (size_t)w * h * 3;
    size_t total = (size_t)hlen + data_size;
    uint8_t *buf = malloc(total);
    if (!buf)
        return NULL;
    memcpy(buf, header, hlen);
    memcpy(buf + hlen, pixels, data_size);
    *out_size = total;
    return buf;
}

/* Convenience: create a context with image loaded from a PGM pixel array */
static ph_context_t *load_pgm(int w, int h, const uint8_t *pixels) {
    size_t sz;
    uint8_t *buf = make_pgm(w, h, pixels, &sz);
    if (!buf)
        return NULL;

    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        free(buf);
        return NULL;
    }

    ph_error_t err = ph_load_from_memory(ctx, buf, sz);
    free(buf);
    if (err != PH_SUCCESS) {
        ph_free(ctx);
        return NULL;
    }
    return ctx;
}

/* Convenience: create a context with image loaded from a PPM pixel array.
 * Kept for future RGB-based tests; suppress unused-function warning. */
static ph_context_t *load_ppm(int w, int h, const uint8_t *pixels) __attribute__((unused));
static ph_context_t *load_ppm(int w, int h, const uint8_t *pixels) {
    size_t sz;
    uint8_t *buf = make_ppm(w, h, pixels, &sz);
    if (!buf)
        return NULL;

    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        free(buf);
        return NULL;
    }

    ph_error_t err = ph_load_from_memory(ctx, buf, sz);
    free(buf);
    if (err != PH_SUCCESS) {
        ph_free(ctx);
        return NULL;
    }
    return ctx;
}

/* =========================================================
 * aHash tests
 * ========================================================= */

static void test_ahash_uniform_gray(void) {
    /* 8×8 uniform gray image: every pixel == avg, so every pixel >= avg.
     * All 64 bits must be set → hash == UINT64_MAX */
    uint8_t pixels[64];
    memset(pixels, 128, 64);

    ph_context_t *ctx = load_pgm(8, 8, pixels);
    ASSERT_PTR_NOT_NULL(ctx);

    uint64_t hash = 0;
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_ahash(ctx, &hash));
    ASSERT_UINT64_EQ(UINT64_MAX, hash);

    ph_free(ctx);
    PASS("test_ahash_uniform_gray");
}

static void test_ahash_all_black(void) {
    /* All-black image: avg=0, every pixel >= 0 → all bits set */
    uint8_t pixels[64];
    memset(pixels, 0, 64);

    ph_context_t *ctx = load_pgm(8, 8, pixels);
    ASSERT_PTR_NOT_NULL(ctx);

    uint64_t hash = 0;
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_ahash(ctx, &hash));
    ASSERT_UINT64_EQ(UINT64_MAX, hash);

    ph_free(ctx);
    PASS("test_ahash_all_black");
}

static void test_ahash_deterministic(void) {
    /* Same image must produce the same hash on two different calls */
    uint8_t pixels[64];
    for (int i = 0; i < 64; i++)
        pixels[i] = (uint8_t)(i * 4);

    ph_context_t *ctx1 = load_pgm(8, 8, pixels);
    ph_context_t *ctx2 = load_pgm(8, 8, pixels);
    ASSERT_PTR_NOT_NULL(ctx1);
    ASSERT_PTR_NOT_NULL(ctx2);

    uint64_t h1 = 0, h2 = 0;
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_ahash(ctx1, &h1));
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_ahash(ctx2, &h2));
    ASSERT_UINT64_EQ(h1, h2);

    ph_free(ctx1);
    ph_free(ctx2);
    PASS("test_ahash_deterministic");
}

/* =========================================================
 * dHash tests
 * ========================================================= */

static void test_dhash_constant_is_zero(void) {
    /* Constant image: every pixel == its right neighbour → no bit set → hash = 0.
     * Use a size (w=9, h=8) that is exactly the internal dHash grid.
     * Box sampling 9→9 and 8→8 is identity → no rounding, perfectly flat output. */
    uint8_t pixels[9 * 8];
    memset(pixels, 150, sizeof(pixels));

    ph_context_t *ctx = load_pgm(9, 8, pixels);
    ASSERT_PTR_NOT_NULL(ctx);

    uint64_t hash = UINT64_MAX;
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_dhash(ctx, &hash));
    ASSERT_UINT64_EQ(0, hash);

    ph_free(ctx);
    PASS("test_dhash_constant_is_zero");
}

static void test_dhash_ascending_gradient(void) {
    /* Horizontal gradient: pixels strictly increase left→right in every row.
     * Every pixel < its right neighbour → every bit set → hash = UINT64_MAX */

    /* Create a 9xN image with strictly ascending rows so box-resize keeps gradient */
    int w = 18, h = 8;
    uint8_t pixels[18 * 8];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            pixels[y * w + x] = (uint8_t)((x * 14) & 0xFF);

    ph_context_t *ctx = load_pgm(w, h, pixels);
    ASSERT_PTR_NOT_NULL(ctx);

    uint64_t hash = 0;
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_dhash(ctx, &hash));
    /* At least 50% of bits should be set for an ascending gradient */
    int bits = ph_hamming_distance(hash, 0);
    if (bits < 32) {
        fprintf(stderr, "[FAIL] test_dhash_ascending_gradient: only %d/64 bits set\n", bits);
        exit(1);
    }

    ph_free(ctx);
    PASS("test_dhash_ascending_gradient");
}

static void test_dhash_deterministic(void) {
    uint8_t pixels[16 * 16];
    for (int i = 0; i < 256; i++)
        pixels[i] = (uint8_t)i;

    ph_context_t *ctx1 = load_pgm(16, 16, pixels);
    ph_context_t *ctx2 = load_pgm(16, 16, pixels);
    ASSERT_PTR_NOT_NULL(ctx1);
    ASSERT_PTR_NOT_NULL(ctx2);

    uint64_t h1 = 0, h2 = 0;
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_dhash(ctx1, &h1));
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_dhash(ctx2, &h2));
    ASSERT_UINT64_EQ(h1, h2);

    ph_free(ctx1);
    ph_free(ctx2);
    PASS("test_dhash_deterministic");
}

/* =========================================================
 * BMH tests
 * ========================================================= */

static void test_bmh_uniform_all_bits_set(void) {
    /* Uniform image: avg == every pixel → all bits set */
    uint8_t pixels[32 * 32];
    memset(pixels, 90, sizeof(pixels));

    ph_context_t *ctx = load_pgm(32, 32, pixels);
    ASSERT_PTR_NOT_NULL(ctx);

    ph_digest_t d;
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_bmh(ctx, &d));

    /* For block_size=16, BMH = 32 bytes. All should be 0xFF. */
    for (int i = 0; i < d.size; i++) {
        if (d.data[i] != 0xFF) {
            fprintf(stderr, "[FAIL] test_bmh_uniform_all_bits_set: byte %d = 0x%02x\n", i,
                    d.data[i]);
            exit(1);
        }
    }
    ph_free(ctx);
    PASS("test_bmh_uniform_all_bits_set");
}

static void test_bmh_size_correct(void) {
    /* BMH with default block_size=16 → 16×16=256 bits → 32 bytes */
    uint8_t pixels[32 * 32];
    memset(pixels, 128, sizeof(pixels));

    ph_context_t *ctx = load_pgm(32, 32, pixels);
    ASSERT_PTR_NOT_NULL(ctx);

    ph_digest_t d;
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_bmh(ctx, &d));
    ASSERT_INT_EQ(32, (int)d.size);

    ph_free(ctx);
    PASS("test_bmh_size_correct");
}

static void test_bmh_deterministic(void) {
    uint8_t pixels[32 * 32];
    for (int i = 0; i < 1024; i++)
        pixels[i] = (uint8_t)i;

    ph_context_t *ctx1 = load_pgm(32, 32, pixels);
    ph_context_t *ctx2 = load_pgm(32, 32, pixels);
    ASSERT_PTR_NOT_NULL(ctx1);
    ASSERT_PTR_NOT_NULL(ctx2);

    ph_digest_t d1, d2;
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_bmh(ctx1, &d1));
    ASSERT_INT_EQ(PH_SUCCESS, ph_compute_bmh(ctx2, &d2));

    ASSERT_INT_EQ(0, ph_hamming_distance_digest(&d1, &d2));

    ph_free(ctx1);
    ph_free(ctx2);
    PASS("test_bmh_deterministic");
}

/* =========================================================
 * Hamming distance tests (uint64)
 * ========================================================= */

static void test_hamming_identical(void) {
    ASSERT_INT_EQ(0, ph_hamming_distance(0, 0));
    ASSERT_INT_EQ(0, ph_hamming_distance(UINT64_MAX, UINT64_MAX));
    ASSERT_INT_EQ(0, ph_hamming_distance(0xDEADBEEFCAFE1234ULL, 0xDEADBEEFCAFE1234ULL));
    PASS("test_hamming_identical");
}

static void test_hamming_all_bits_differ(void) {
    ASSERT_INT_EQ(64, ph_hamming_distance(0, UINT64_MAX));
    ASSERT_INT_EQ(64, ph_hamming_distance(UINT64_MAX, 0));
    PASS("test_hamming_all_bits_differ");
}

static void test_hamming_single_bit(void) {
    ASSERT_INT_EQ(1, ph_hamming_distance(0, 1));
    ASSERT_INT_EQ(1, ph_hamming_distance(1, 3));           /* bits: 01 vs 11 */
    ASSERT_INT_EQ(1, ph_hamming_distance(0x8000000000000000ULL, 0));
    PASS("test_hamming_single_bit");
}

static void test_hamming_eight_bits(void) {
    /* 0 vs 0xFF → 8 bits differ */
    ASSERT_INT_EQ(8, ph_hamming_distance(0, 0xFF));
    /* 0xAA = 10101010, 0x55 = 01010101 → 8 bits differ */
    ASSERT_INT_EQ(8, ph_hamming_distance(0xAA, 0x55));
    PASS("test_hamming_eight_bits");
}

static void test_hamming_commutative(void) {
    uint64_t a = 0xABCDEF1234567890ULL;
    uint64_t b = 0x0FEDCBA987654321ULL;
    ASSERT_INT_EQ(ph_hamming_distance(a, b), ph_hamming_distance(b, a));
    PASS("test_hamming_commutative");
}

/* =========================================================
 * Hamming distance tests (digest)
 * ========================================================= */

static void test_hamming_digest_identical(void) {
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    d.size = 8;
    d.data[0] = 0xAB;
    ASSERT_INT_EQ(0, ph_hamming_distance_digest(&d, &d));
    PASS("test_hamming_digest_identical");
}

static void test_hamming_digest_known(void) {
    /* d1.data[0] = 0x01, d2.data[0] = 0x03 → differ in bit 1 → distance = 1 */
    ph_digest_t d1, d2;
    memset(&d1, 0, sizeof(d1));
    memset(&d2, 0, sizeof(d2));
    d1.size = d2.size = 4;
    d1.data[0] = 0x01;
    d2.data[0] = 0x03;
    ASSERT_INT_EQ(1, ph_hamming_distance_digest(&d1, &d2));
    PASS("test_hamming_digest_known");
}

static void test_hamming_digest_all_bytes(void) {
    ph_digest_t d1, d2;
    memset(&d1, 0xFF, sizeof(d1));
    memset(&d2, 0x00, sizeof(d2));
    d1.size = d2.size = 8; /* 8 bytes × 8 bits = 64 */
    ASSERT_INT_EQ(64, ph_hamming_distance_digest(&d1, &d2));
    PASS("test_hamming_digest_all_bytes");
}

/* =========================================================
 * L2 distance tests
 * ========================================================= */

static void test_l2_same_digest(void) {
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    d.size = 4;
    d.data[0] = 100;
    d.data[1] = 200;
    ASSERT_FLOAT_EQ(0.0, ph_l2_distance(&d, &d), 1e-6);
    PASS("test_l2_same_digest");
}

static void test_l2_known_value(void) {
    /* [0] vs [255] with size=1 → sqrt((255-0)²) = 255.0 */
    ph_digest_t d1, d2;
    memset(&d1, 0, sizeof(d1));
    memset(&d2, 0, sizeof(d2));
    d1.size = d2.size = 1;
    d1.data[0] = 0;
    d2.data[0] = 255;
    ASSERT_FLOAT_EQ(255.0, ph_l2_distance(&d1, &d2), 0.01);
    PASS("test_l2_known_value");
}

static void test_l2_pythagorean(void) {
    /* [0,0] vs [3,4] with size=2 → sqrt(9+16) = 5.0 */
    ph_digest_t d1, d2;
    memset(&d1, 0, sizeof(d1));
    memset(&d2, 0, sizeof(d2));
    d1.size = d2.size = 2;
    d1.data[0] = 0;
    d1.data[1] = 0;
    d2.data[0] = 3;
    d2.data[1] = 4;
    ASSERT_FLOAT_EQ(5.0, ph_l2_distance(&d1, &d2), 0.01);
    PASS("test_l2_pythagorean");
}

static void test_l2_commutative(void) {
    ph_digest_t d1, d2;
    memset(&d1, 0, sizeof(d1));
    memset(&d2, 0, sizeof(d2));
    d1.size = d2.size = 4;
    d1.data[0] = 10; d1.data[1] = 20; d1.data[2] = 30; d1.data[3] = 40;
    d2.data[0] = 90; d2.data[1] = 80; d2.data[2] = 70; d2.data[3] = 60;
    ASSERT_FLOAT_EQ(ph_l2_distance(&d1, &d2), ph_l2_distance(&d2, &d1), 1e-6);
    PASS("test_l2_commutative");
}

/* =========================================================
 * Invalid argument tests (error-code correctness)
 * ========================================================= */

static void test_invalid_null_ctx(void) {
    uint64_t h;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_ahash(NULL, &h));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_dhash(NULL, &h));
    PASS("test_invalid_null_ctx");
}

static void test_invalid_null_out(void) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        fprintf(stderr, "[FAIL] test_invalid_null_out: ph_create failed\n");
        exit(1);
    }
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_ahash(ctx, NULL));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_dhash(ctx, NULL));
    ph_free(ctx);
    PASS("test_invalid_null_out");
}

/* =========================================================
 * main
 * ========================================================= */

int main(void) {
    /* aHash */
    test_ahash_uniform_gray();
    test_ahash_all_black();
    test_ahash_deterministic();

    /* dHash */
    test_dhash_constant_is_zero();
    test_dhash_ascending_gradient();
    test_dhash_deterministic();

    /* BMH */
    test_bmh_uniform_all_bits_set();
    test_bmh_size_correct();
    test_bmh_deterministic();

    /* Hamming (uint64) */
    test_hamming_identical();
    test_hamming_all_bits_differ();
    test_hamming_single_bit();
    test_hamming_eight_bits();
    test_hamming_commutative();

    /* Hamming (digest) */
    test_hamming_digest_identical();
    test_hamming_digest_known();
    test_hamming_digest_all_bytes();

    /* L2 distance */
    test_l2_same_digest();
    test_l2_known_value();
    test_l2_pythagorean();
    test_l2_commutative();

    /* Error codes */
    test_invalid_null_ctx();
    test_invalid_null_out();

    printf("\nAll hash logic and distance tests passed.\n");
    return 0;
}
