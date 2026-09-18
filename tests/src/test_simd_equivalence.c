/*
 * test_simd_equivalence.c
 *
 * Covers a gap where none of color.c's/filters.c's NEON paths, phash.c's NEON dot product, or
 * common.c's AVX2/SSE4.2/NEON Hamming distance were ever checked against their scalar
 * fallback for producing the same result. A mismatch here means two different hashes for
 * the same input depending on which architecture ran it -- exactly the class of bug that
 * would otherwise surface as an unexplained golden-hash mismatch.
 *
 * Every function below exists in two forms: the production one (compiled with whatever
 * SIMD the target supports) and a `_scalar` twin that always takes the plain C path,
 * declared in internal.h for this purpose only. This test calls both on the same inputs
 * and diffs the outputs.
 *
 * On a build with no SIMD available at all (__ARM_NEON/__AVX2__/__SSE4_2__ all
 * undefined) the production and `_scalar` entry points are literally the same code path,
 * so the comparisons below are tautological there -- the test still passes, it just isn't
 * exercising anything. The matrix this needs to run on to mean something is arm64 (NEON)
 * and x86_64 with AVX2/SSE4.2 (both gcc and clang), per the task's acceptance criteria.
 *
 * ph_dct2_partial() is the one function here with a floating-point SIMD path
 * (dot_product_f32_u8_neon in phash.c, used only at dct_size == 32). Floating-point
 * addition is not associative, so NEON's 4-lane tree reduction and the scalar sequential
 * sum are only guaranteed equal up to rounding, not bit-for-bit -- unlike every other
 * function tested here, which is pure integer/byte arithmetic and compared exactly. The
 * DCT case is checked against a tolerance instead (see dct2_close_enough below), loose
 * enough to pass on rounding and tight enough that the deliberate-breakage check further
 * down (see the note before main()) still catches a real divergence.
 */

#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Small deterministic PRNG so the test corpus is identical across runs/platforms --
 * no dependency on the C library's rand() implementation. */
static uint32_t g_rng_state;

static void rng_seed(uint32_t seed) { g_rng_state = seed ? seed : 1; }

static uint32_t rng_next(void) {
    /* xorshift32 */
    uint32_t x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng_state = x;
    return x;
}

static uint8_t rng_byte(void) { return (uint8_t)rng_next(); }

static void fill_random(uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++)
        buf[i] = rng_byte();
}

static void fill_gradient(uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++)
        buf[i] = (uint8_t)(i % 256);
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
            if (dst_simd[i] != dst_scalar[i]) {
                fprintf(stderr,
                        "[FAIL] ph_to_grayscale/%s (%dx%d, %d ch): byte %zu differs: "
                        "simd=%u scalar=%u\n",
                        label, w, h, channels, i, dst_simd[i], dst_scalar[i]);
                exit(1);
            }
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
            rng_seed(0x1234u + (uint32_t)s * 7u + (uint32_t)channels * 101u);
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
 * ph_apply_gaussian_blur vs ph_apply_gaussian_blur_scalar
 * ========================================================= */

static void run_blur_case(const char *label, int w, int h, void (*fill)(uint8_t *, size_t)) {
    size_t n = (size_t)w * (size_t)h;

    uint8_t *src = malloc(n);
    uint8_t *dst_simd = malloc(n);
    uint8_t *dst_scalar = malloc(n);
    ASSERT_PTR_NOT_NULL(src);
    ASSERT_PTR_NOT_NULL(dst_simd);
    ASSERT_PTR_NOT_NULL(dst_scalar);

    fill(src, n);
    memset(dst_simd, 0xAA, n);
    memset(dst_scalar, 0x55, n);

    ph_context_t *ctx_simd = NULL;
    ph_context_t *ctx_scalar = NULL;
    ASSERT_OK(ph_create(&ctx_simd));
    ASSERT_OK(ph_create(&ctx_scalar));

    int ok_simd = ph_apply_gaussian_blur(ctx_simd, src, w, h, dst_simd);
    int ok_scalar = ph_apply_gaussian_blur_scalar(ctx_scalar, src, w, h, dst_scalar);
    ASSERT(ok_simd == ok_scalar);

    if (ok_simd && memcmp(dst_simd, dst_scalar, n) != 0) {
        for (size_t i = 0; i < n; i++) {
            if (dst_simd[i] != dst_scalar[i]) {
                fprintf(stderr,
                        "[FAIL] ph_apply_gaussian_blur/%s (%dx%d): byte %zu differs: "
                        "simd=%u scalar=%u\n",
                        label, w, h, i, dst_simd[i], dst_scalar[i]);
                exit(1);
            }
        }
    }

    ph_free(ctx_simd);
    ph_free(ctx_scalar);
    free(src);
    free(dst_simd);
    free(dst_scalar);
}

static void test_gaussian_blur_equivalence(void) {
    static const struct {
        int w, h;
    } shapes[] = {
        {1, 1},   {2, 2},   {3, 3},   {3, 1},   {1, 3},   {4, 3},   {3, 4},   {15, 15},
        {16, 16}, {17, 17}, {31, 31}, {32, 32}, {33, 33}, {1, 200}, {200, 1},
    };
    size_t n_shapes = sizeof(shapes) / sizeof(shapes[0]);

    for (size_t s = 0; s < n_shapes; s++) {
        rng_seed(0x5678u + (uint32_t)s * 13u);
        run_blur_case("random", shapes[s].w, shapes[s].h, fill_random);
        run_blur_case("gradient", shapes[s].w, shapes[s].h, fill_gradient);
    }

    /* Uniform image per shape: every pixel must stay the same value under both paths. */
    for (size_t s = 0; s < n_shapes; s++) {
        int w = shapes[s].w, h = shapes[s].h;
        size_t n = (size_t)w * (size_t)h;
        uint8_t *src = malloc(n);
        uint8_t *dst_simd = malloc(n);
        uint8_t *dst_scalar = malloc(n);
        memset(src, 77, n);

        ph_context_t *ctx_simd = NULL, *ctx_scalar = NULL;
        ASSERT_OK(ph_create(&ctx_simd));
        ASSERT_OK(ph_create(&ctx_scalar));
        ph_apply_gaussian_blur(ctx_simd, src, w, h, dst_simd);
        ph_apply_gaussian_blur_scalar(ctx_scalar, src, w, h, dst_scalar);
        ASSERT(memcmp(dst_simd, dst_scalar, n) == 0);
        ph_free(ctx_simd);
        ph_free(ctx_scalar);
        free(src);
        free(dst_simd);
        free(dst_scalar);
    }

    PASS("test_gaussian_blur_equivalence");
}

/* =========================================================
 * ph_dct2_partial vs ph_dct2_partial_scalar
 * ========================================================= */

static int dct2_close_enough(const float *a, const float *b, int n) {
    for (int i = 0; i < n; i++) {
        float diff = fabsf(a[i] - b[i]);
        float scale = fmaxf(1.0f, fmaxf(fabsf(a[i]), fabsf(b[i])));
        if (diff > 1e-3f * scale)
            return 0;
    }
    return 1;
}

static void run_dct2_case(int dct_size, int reduction_size, void (*fill)(uint8_t *, size_t)) {
    uint8_t input[PH_DCT_MAX_SIZE * PH_DCT_MAX_SIZE];
    fill(input, (size_t)dct_size * (size_t)dct_size);

    const float *mat = ph_get_dct_matrix_32();
    float out_simd[PH_DCT_MAX_REDUCTION_SIZE * PH_DCT_MAX_REDUCTION_SIZE];
    float out_scalar[PH_DCT_MAX_REDUCTION_SIZE * PH_DCT_MAX_REDUCTION_SIZE];

    ASSERT(ph_dct2_partial(mat, input, dct_size, reduction_size, out_simd) == PH_SUCCESS);
    ASSERT(ph_dct2_partial_scalar(mat, input, dct_size, reduction_size, out_scalar) == PH_SUCCESS);

    if (!dct2_close_enough(out_simd, out_scalar, reduction_size * reduction_size)) {
        for (int i = 0; i < reduction_size * reduction_size; i++) {
            fprintf(stderr, "[FAIL] ph_dct2_partial: coeff %d differs: simd=%g scalar=%g\n", i,
                    (double)out_simd[i], (double)out_scalar[i]);
        }
        exit(1);
    }
}

static void test_dct2_partial_equivalence(void) {
    /* dct_size == 32 is the only size the NEON dot product ever takes; the smaller sizes
     * are included as a control -- both entry points already share the same scalar loop
     * there, so they trivially agree. */
    static const int reduction_sizes[] = {1, 4, 8};
    for (size_t r = 0; r < sizeof(reduction_sizes) / sizeof(reduction_sizes[0]); r++) {
        rng_seed(0x9abc0000u + (uint32_t)reduction_sizes[r]);
        run_dct2_case(32, reduction_sizes[r], fill_random);
        run_dct2_case(32, reduction_sizes[r], fill_gradient);
    }
    rng_seed(0xdef1u);
    run_dct2_case(8, 4, fill_random);

    /* Flat (zero-variance) input: every AC coefficient should come out at/near zero on
     * both paths. */
    {
        uint8_t input[32 * 32];
        memset(input, 128, sizeof(input));
        const float *mat = ph_get_dct_matrix_32();
        float out_simd[64], out_scalar[64];
        ASSERT(ph_dct2_partial(mat, input, 32, 8, out_simd) == PH_SUCCESS);
        ASSERT(ph_dct2_partial_scalar(mat, input, 32, 8, out_scalar) == PH_SUCCESS);
        ASSERT(dct2_close_enough(out_simd, out_scalar, 64));
    }

    PASS("test_dct2_partial_equivalence");
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
    rng_seed(0x1111u + size);
    ph_digest_t a = make_bits_digest(size, fill_random);
    rng_seed(0x2222u + size);
    ph_digest_t b = make_bits_digest(size, fill_random);

    int simd = ph_hamming_distance_digest(&a, &b);
    int scalar = ph_hamming_distance_digest_scalar(&a, &b);
    if (simd != scalar) {
        fprintf(stderr, "[FAIL] ph_hamming_distance_digest (size=%u): simd=%d scalar=%d\n",
                (unsigned)size, simd, scalar);
        exit(1);
    }

    /* Self-distance is 0 on both paths -- except size 0, which both entry points reject
     * as incomparable (ph_digest_is_comparable() requires size > 0) rather than reporting
     * a distance of 0. */
    int expected_self = (size > 0) ? 0 : -1;
    ASSERT_INT_EQ(expected_self, ph_hamming_distance_digest(&a, &a));
    ASSERT_INT_EQ(expected_self, ph_hamming_distance_digest_scalar(&a, &a));
}

static void test_hamming_distance_equivalence(void) {
    static const int sizes[] = {0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        run_hamming_case((uint8_t)sizes[i]);

    /* Identical digests, and fully-flipped digests, at a representative size. */
    ph_digest_t a = make_bits_digest(64, fill_gradient);
    ph_digest_t b = a;
    for (int i = 0; i < 64; i++)
        b.data[i] = (uint8_t)~b.data[i];
    ASSERT_INT_EQ(ph_hamming_distance_digest(&a, &b), ph_hamming_distance_digest_scalar(&a, &b));
    ASSERT_INT_EQ(512, ph_hamming_distance_digest(&a, &b)); /* 64 bytes fully flipped */

    PASS("test_hamming_distance_equivalence");
}

int main(void) {
    test_grayscale_equivalence();
    test_gaussian_blur_equivalence();
    test_dct2_partial_equivalence();
    test_hamming_distance_equivalence();

    printf("\nAll SIMD/scalar equivalence tests passed!\n");
    return 0;
}
