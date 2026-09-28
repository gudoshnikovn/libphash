#include "context.h"
#include "hashes/hashes.h"
#include "image/image.h"
#include "libphash.h"
#include "safety.h"
#include "test_macros.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_scratchpad_management(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // 1. Initial allocation
    uint8_t *p1 = ph_get_scratchpad(ctx, 100);
    ASSERT_PTR_NOT_NULL(p1);
    // Capacity should be at least 1024 (as per implementation)
    if (ph_arena_capacity(ctx) < 1024) {
        fprintf(stderr, "[FAIL] Initial capacity too small: %zu\n", ph_arena_capacity(ctx));
        exit(1);
    }
    // Verify 32-byte alignment
    if (((uintptr_t)p1 & 31) != 0) {
        fprintf(stderr, "[FAIL] Scratchpad not 32-byte aligned\n");
        exit(1);
    }

    // 2. Reuse WITHIN capacity
    uint8_t *p2 = ph_get_scratchpad(ctx, 200);
    ASSERT_PTR_NOT_NULL(p2);
    // The next block starts at the first aligned offset past the previous one.
    if (p2 != p1 + ph_arena_align_up(100)) {
        fprintf(stderr, "[FAIL] Scratchpad did not increment offset correctly\n");
        exit(1);
    }

    // 3. Growth triggering reallocation
    // Current offset is 128 + 200. Max capacity is at least 1024.
    // Let's request something huge.
    uint8_t *p3 = ph_get_scratchpad(ctx, 2000);
    ASSERT_PTR_NOT_NULL(p3);
    if (ph_arena_capacity(ctx) < ph_arena_align_up(128 + 200) + 2000) {
        fprintf(stderr, "[FAIL] Capacity did not grow: %zu\n", ph_arena_capacity(ctx));
        exit(1);
    }

    ph_free(ctx);
    PASS("test_scratchpad_management");
}

void test_scratchpad_autotrim(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Allocate a large buffer
    ph_get_scratchpad(ctx, 10000);
    size_t large_capacity = ph_arena_capacity(ctx);

    // Reset offset
    ph_arena_release(ctx, 0);

    // Call ph_get_scratchpad with a small size.
    // If offset == 0 and capacity > size * 4, it trims: capacity ~10240 > 100 * 4.
    ph_get_scratchpad(ctx, 100);

    if (ph_arena_capacity(ctx) >= large_capacity) {
        fprintf(stderr, "[FAIL] Scratchpad did not auto-trim (cap=%zu)\n", ph_arena_capacity(ctx));
        // Note: Implementation might use a minimum 1024, so as long as it's smaller than
        // large_capacity it's fine.
    }

    ph_free(ctx);
    PASS("test_scratchpad_autotrim");
}

void test_parameter_validation(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Gamma validation
    float old_gamma = ctx->config.gamma;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gamma(ctx, 0.0f));
    ASSERT_FLOAT_EQ((double)old_gamma, (double)ctx->config.gamma, 0.0001);
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gamma(ctx, -1.0f));
    ASSERT_FLOAT_EQ((double)old_gamma, (double)ctx->config.gamma, 0.0001);

    /* Gray weights validation. A zero sum is an error, NOT a silent reset to
     * the BT.601 defaults -- so custom weights are installed first and have to survive
     * the rejected call. Asserting against the defaults would pass either way. */
    ASSERT_OK(ph_context_set_gray_weights(ctx, 128, 0, 0));
    ASSERT_INT_EQ(128, ctx->config.gray_r);
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gray_weights(ctx, 0, 0, 0));
    if (ctx->config.gray_r != 128) {
        fprintf(stderr, "[FAIL] Rejected gray weights still changed the configuration\n");
        exit(1);
    }

    // pHash validation (reduction > dct)
    int old_red = ctx->config.phash_reduction_size;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_phash_params(ctx, 8, 16));
    if (ctx->config.phash_reduction_size != old_red) {
        fprintf(stderr, "[FAIL] pHash params accepted invalid reduction size\n");
        exit(1);
    }

    // Radial validation
    int old_proj = ctx->config.radial_projections;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                  ph_context_set_radial_params(ctx, 0, 128, PH_RADIAL_DEFAULT_SIGMA));
    if (ctx->config.radial_projections != old_proj) {
        fprintf(stderr, "[FAIL] Radial params accepted 0 projections\n");
        exit(1);
    }

    ph_free(ctx);
    PASS("test_parameter_validation");
}

/* Every ph_context_set_* reports an invalid argument and leaves the
 * configuration untouched. NULL context first, then per-setter ranges. */
void test_setter_error_contract(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    /* --- NULL context: every setter, no exceptions --- */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gamma(NULL, 2.2f));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gray_weights(NULL, 1, 1, 1));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_phash_params(NULL, 32, 8));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                  ph_context_set_radial_params(NULL, 40, 128, PH_RADIAL_DEFAULT_SIGMA));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_block_params(NULL, 16));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_whash_mode(NULL, PH_WHASH_FULL));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_load_grayscale(NULL, 1));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_auto_orient(NULL, 1));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_max_pixels(NULL, 1024));

    /* --- gamma: non-finite values --- */
    /* Every comparison against NaN is false, so a bare `gamma <= PH_GAMMA_EPSILON` would
     * accept NAN, and INFINITY is above it. Both must be rejected with ctx->config.gamma
     * left untouched. */
    float good_gamma = ctx->config.gamma;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gamma(ctx, (float)NAN));
    ASSERT_FLOAT_EQ((double)good_gamma, (double)ctx->config.gamma, 0.0001);
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gamma(ctx, (float)INFINITY));
    ASSERT_FLOAT_EQ((double)good_gamma, (double)ctx->config.gamma, 0.0001);
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gamma(ctx, -(float)INFINITY));
    ASSERT_FLOAT_EQ((double)good_gamma, (double)ctx->config.gamma, 0.0001);

    /* gamma bounds: the ceiling is accepted, one step above it is not. */
    ASSERT_OK(ph_context_set_gamma(ctx, PH_GAMMA_MAX));
    ASSERT_FLOAT_EQ((double)PH_GAMMA_MAX, (double)ctx->config.gamma, 0.0001);
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gamma(ctx, PH_GAMMA_MAX * 1.001f));
    ASSERT_FLOAT_EQ((double)PH_GAMMA_MAX, (double)ctx->config.gamma, 0.0001);
    ASSERT_OK(ph_context_set_gamma(ctx, PH_DEFAULT_GAMMA));

    /* --- gray weights: negative components and an overflowing sum --- */
    ASSERT_OK(ph_context_set_gray_weights(ctx, 30, 60, 10));
    int keep_r = ctx->config.gray_r, keep_g = ctx->config.gray_g, keep_b = ctx->config.gray_b;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gray_weights(ctx, -1, 60, 10));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gray_weights(ctx, 30, -1, 10));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gray_weights(ctx, 30, 60, -1));
    /* Sum one past INT_MAX / 255: rejected. Also note each component is individually
     * valid here, which is why the sum has to be checked in a wider type. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                  ph_context_set_gray_weights(ctx, (int)PH_GRAY_WEIGHT_MAX_SUM, 1, 0));
    /* And INT_MAX in every component: the naive int sum would wrap negative. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                  ph_context_set_gray_weights(ctx, INT_MAX, INT_MAX, INT_MAX));
    ASSERT_INT_EQ(keep_r, ctx->config.gray_r);
    ASSERT_INT_EQ(keep_g, ctx->config.gray_g);
    ASSERT_INT_EQ(keep_b, ctx->config.gray_b);
    /* The sum boundary itself is accepted and normalizes to 128. */
    ASSERT_OK(ph_context_set_gray_weights(ctx, (int)PH_GRAY_WEIGHT_MAX_SUM, 0, 0));
    ASSERT_INT_EQ(128, ctx->config.gray_r + ctx->config.gray_g + ctx->config.gray_b);

    /* --- whash_mode: only the declared enumerators --- */
    ASSERT_OK(ph_context_set_whash_mode(ctx, PH_WHASH_FULL));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_whash_mode(ctx, (ph_whash_mode_t)2));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_whash_mode(ctx, (ph_whash_mode_t)-1));
    ASSERT_INT_EQ(PH_WHASH_FULL, ctx->config.whash_mode);
    ASSERT_OK(ph_context_set_whash_mode(ctx, PH_WHASH_FAST));

    /* --- flags and max_pixels: no value to reject, only normalization --- */
    ASSERT_OK(ph_context_set_load_grayscale(ctx, 7));
    ASSERT_INT_EQ(1, ctx->config.load_grayscale);
    ASSERT_OK(ph_context_set_load_grayscale(ctx, 0));
    ASSERT_INT_EQ(0, ctx->config.load_grayscale);
    ASSERT_OK(ph_context_set_auto_orient(ctx, -3));
    ASSERT_INT_EQ(1, ctx->config.auto_orient);
    ASSERT_OK(ph_context_set_auto_orient(ctx, 0));
    ASSERT_INT_EQ(0, ctx->config.auto_orient);
    /* Above the implementation ceiling is still a valid request: the library applies the ceiling
     * at load time, so there is nothing for the setter to refuse. */
    ASSERT_OK(ph_context_set_max_pixels(ctx, 0));
    ASSERT_OK(ph_context_set_max_pixels(ctx, UINT64_MAX));
    ASSERT_OK(ph_context_set_max_pixels(ctx, PH_MAX_SUPPORTED_PIXELS + 1));

    ph_free(ctx);
    PASS("test_setter_error_contract");
}

/* What a non-finite gamma would do, measured through the public API.
 *
 * Radial is the only algorithm that applies gamma (ph_apply_gamma() is called from
 * src/hashes/radial.c only). Without the isfinite() check, a NaN or infinite gamma
 * yields PH_SUCCESS from the setter and from the hash and an all-zero Radial digest
 * (and the uint8_t conversion of a NaN is undefined into the bargain). A rejected setter
 * call must leave the digest bit-for-bit identical. */
static void assert_radial_digests_equal(const ph_digest_t *a, const ph_digest_t *b) {
    ASSERT_UINT8_EQ(a->size, b->size);
    for (int i = 0; i < a->size; i++) {
        ASSERT_UINT8_EQ(a->data[i], b->data[i]);
    }
}

void test_gamma_nan_cannot_corrupt_hash(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    ph_digest_t before, after;
    ASSERT_OK(ph_compute_radial_hash(ctx, &before));
    /* The baseline must not itself be degenerate, or this test would pass vacuously. */
    int nonzero = 0;
    for (int i = 0; i < before.size; i++) {
        nonzero |= before.data[i];
    }
    ASSERT(nonzero != 0);

    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gamma(ctx, (float)NAN));
    ASSERT_OK(ph_compute_radial_hash(ctx, &after));
    assert_radial_digests_equal(&before, &after);

    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gamma(ctx, (float)INFINITY));
    ASSERT_OK(ph_compute_radial_hash(ctx, &after));
    assert_radial_digests_equal(&before, &after);

    /* aHash is unaffected either way -- it never applies gamma -- but assert it too so
     * that a future change routing gamma into the grayscale path stays covered. */
    uint64_t a1 = 0, a2 = 0;
    ASSERT_OK(ph_compute_ahash(ctx, &a1));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_gamma(ctx, -(float)INFINITY));
    ASSERT_OK(ph_compute_ahash(ctx, &a2));
    ASSERT_UINT64_EQ(a1, a2);

    ph_free(ctx);
    PASS("test_gamma_nan_cannot_corrupt_hash");
}

void test_error_handling(void) {
    // Unknown error string
    const char *err = ph_get_error_string((ph_error_t)999);
    if (strcmp(err, "Unknown error") != 0) {
        fprintf(stderr, "[FAIL] Unexpected error string for unknown code: %s\n", err);
        exit(1);
    }

    // Load from non-existent file
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ph_error_t e = ph_load_from_file(ctx, "/path/to/nothing/that/exists.jpg");
    if (e == PH_SUCCESS) {
        fprintf(stderr, "[FAIL] ph_load_from_file should have failed for non-existent path\n");
        exit(1);
    }

    // NULL arguments
    if (ph_create(NULL) != PH_ERR_INVALID_ARGUMENT) {
        exit(1);
    }
    if (ph_load_from_file(NULL, "test.jpg") != PH_ERR_INVALID_ARGUMENT) {
        exit(1);
    }
    if (ph_load_from_memory(NULL, (uint8_t *)"abc", 3) != PH_ERR_INVALID_ARGUMENT) {
        exit(1);
    }

    ph_free(ctx);
    PASS("test_error_handling");
}

/* A failed file/memory load drops the previous image and its dimensions with it, so
 * ph_context_get_dimensions() reports 0/0/0, not the size of an image that is gone. A
 * failed ph_load_from_pixels() is deliberately different -- it keeps
 * the previous image -- and that asymmetry is pinned here too. */
void test_dimensions_follow_the_loaded_image(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    int w = -1, h = -1, c = -1;

    ph_context_get_dimensions(ctx, &w, &h, &c);
    ASSERT(w == 0 && h == 0 && c == 0);
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    uint8_t pixels[7 * 5 * 3];
    memset(pixels, 0x80, sizeof(pixels));
    ASSERT_OK(ph_load_from_pixels(ctx, pixels, 7, 5, 3, 7 * 3));
    ph_context_get_dimensions(ctx, &w, &h, &c);
    ASSERT(w == 7 && h == 5 && c == 3);

    static const uint8_t junk[8] = {'n', 'o', 't', ' ', 'a', 'n', ' ', 'i'};
    ASSERT_INT_EQ(PH_ERR_UNSUPPORTED_FORMAT, ph_load_from_memory(ctx, junk, sizeof(junk)));
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));
    w = h = c = -1;
    ph_context_get_dimensions(ctx, &w, &h, &c);
    ASSERT(w == 0 && h == 0 && c == 0);

    ASSERT_OK(ph_load_from_pixels(ctx, pixels, 7, 5, 3, 7 * 3));
    ASSERT_INT_EQ(PH_ERR_IO, ph_load_from_file(ctx, TEST_DATA_DIR "/does_not_exist.png"));
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));
    ph_context_get_dimensions(ctx, &w, &h, &c);
    ASSERT(w == 0 && h == 0 && c == 0);

    /* The exception: a rejected ph_load_from_pixels() leaves the image it found. */
    ASSERT_OK(ph_load_from_pixels(ctx, pixels, 7, 5, 3, 7 * 3));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_load_from_pixels(ctx, pixels, 7, 5, 2, 7 * 3));
    ASSERT_INT_EQ(1, ph_is_loaded(ctx));
    ph_context_get_dimensions(ctx, &w, &h, &c);
    ASSERT(w == 7 && h == 5 && c == 3);

    /* NULL-tolerant both ways: no context writes nothing, NULL outputs are skipped. */
    w = h = c = 42;
    ph_context_get_dimensions(NULL, &w, &h, &c);
    ASSERT(w == 42 && h == 42 && c == 42);
    ph_context_get_dimensions(ctx, NULL, NULL, NULL);
    ASSERT_INT_EQ(0, ph_is_loaded(NULL));

    /* Both accessors take a const context. */
    const ph_context_t *view = ctx;
    ASSERT_INT_EQ(1, ph_is_loaded(view));
    ph_context_get_dimensions(view, &w, NULL, NULL);
    ASSERT_INT_EQ(7, w);

    ph_free(ctx);
    PASS("test_dimensions_follow_the_loaded_image");
}

int main(void) {
    test_dimensions_follow_the_loaded_image();
    test_scratchpad_management();
    test_scratchpad_autotrim();
    test_parameter_validation();
    test_setter_error_contract();
    test_gamma_nan_cannot_corrupt_hash();
    test_error_handling();
    printf("\nAll extended core tests passed.\n");
    return 0;
}
