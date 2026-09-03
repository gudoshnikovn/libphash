/* R03 / H6: pixel counts must never be computed in `int`.
 *
 * Every case below used to trip UBSan (signed integer overflow) or slip past the old
 * PH_SAFE_ALLOC_SIZE guard straight into a wrapped malloc(). They now have to end in a
 * clean error code instead. Run under `make debug` to get the UBSan check as well;
 * a plain Release run only verifies the returned error codes.
 */

#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Under a sanitizer build this test binary configures the runtime itself, because the
 * paths it exercises deliberately ask for absurd allocations:
 *  - allocator_may_return_null: an oversized malloc must hand NULL back to the library
 *    so we can assert PH_ERR_ALLOCATION_FAILED, instead of ASan aborting the process;
 *  - max_allocation_size_mb: keeps the "just past the int boundary" case (46341, whose
 *    honest size_t product is a mere 2 GB) from actually allocating and resizing two
 *    gigapixels, which takes ~35 s under -O0 +ASan;
 *  - halt_on_error for UBSan: a signed-overflow report must FAIL the test, not just
 *    print a line that scrolls past in CI. That is the whole point of this file.
 * A non-sanitizer build ignores all of this. */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PH_TEST_ASAN 1
#endif
#if __has_feature(undefined_behavior_sanitizer)
#define PH_TEST_UBSAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define PH_TEST_ASAN 1
#endif
#if defined(__SANITIZE_UNDEFINED__)
#define PH_TEST_UBSAN 1
#endif

#if defined(PH_TEST_ASAN)
const char *__asan_default_options(void);
const char *__asan_default_options(void) {
    return "allocator_may_return_null=1:max_allocation_size_mb=512";
}
#endif
#if defined(PH_TEST_UBSAN)
const char *__ubsan_default_options(void);
const char *__ubsan_default_options(void) { return "halt_on_error=1:print_stacktrace=1"; }
#endif

/* A tiny loaded image, enough for any hash to have something to resize from. */
static ph_context_t *make_ctx_with_tiny_image(int w, int h, int channels) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    size_t n = (size_t)w * (size_t)h * (size_t)channels;
    uint8_t *px = malloc(n);
    ASSERT_PTR_NOT_NULL(px);
    for (size_t i = 0; i < n; i++)
        px[i] = (uint8_t)(i * 7 + 3);
    ASSERT_OK(ph_load_from_pixels(ctx, px, w, h, channels, 0));
    free(px);
    return ctx;
}

/* H6, the exact repro from the review: 46341 * 46341 overflows int.
 * The product is now size_t, so the request is simply too big for the arena.
 *
 * Since R04 these values can no longer reach ph_compute_bmh() through the public API --
 * ph_context_set_block_params() rejects anything above PH_BLOCK_MAX_SIZE. The config field
 * is therefore poisoned directly here, exactly as test_phash.c does for the pHash guard:
 * the point of this file is that the *arithmetic* is well-defined whatever the field
 * holds, which is defence in depth behind the setter and outlives it. That the setter now
 * closes the door is asserted separately, in test_setter_bounds_reject_out_of_range(). */
void test_bmh_block_size_overflows_int(void) {
    /* Squares that no allocator will ever serve (2^60 and ~2^62 bytes), so the
     * expected outcome is identical in every build configuration. */
    const int sizes[] = {1 << 30, INT_MAX};

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        ph_context_t *ctx = make_ctx_with_tiny_image(8, 8, 3);
        ctx->config.block_size = sizes[i]; /* bypasses the setter on purpose */

        ph_digest_t d;
        ph_error_t err = ph_compute_bmh(ctx, &d);
        /* Must not be UB and must not silently succeed on a wrapped size. */
        ASSERT_INT_EQ(PH_ERR_ALLOCATION_FAILED, err);
        ph_free(ctx);
    }

#if defined(PH_TEST_ASAN)
    /* The review's exact repro: 46341 * 46341 == 2147488281 overflows int32 by 4633.
     * Only run where max_allocation_size_mb above makes it cheap -- in a plain Release
     * build this really does allocate 2 GB and box-resize 2.1 gigapixels. */
    {
        ph_context_t *ctx = make_ctx_with_tiny_image(8, 8, 3);
        ctx->config.block_size = 46341;
        ph_digest_t d;
        ASSERT_INT_EQ(PH_ERR_ALLOCATION_FAILED, ph_compute_bmh(ctx, &d));
        ph_free(ctx);
    }
#endif

    PASS("test_bmh_block_size_overflows_int");
}

/* R04: the values above are unreachable through the public API now. The setter refuses
 * them and leaves the configuration exactly as it was -- no clamping to the ceiling. */
void test_setter_bounds_reject_out_of_range(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    const int bad_blocks[] = {1 << 30, INT_MAX, 46341, PH_BLOCK_MAX_SIZE + 1};
    for (size_t i = 0; i < sizeof(bad_blocks) / sizeof(bad_blocks[0]); i++) {
        ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_block_params(ctx, bad_blocks[i]));
        ASSERT_INT_EQ(PH_BLOCK_SIZE, ctx->config.block_size);
    }
    /* And the ceiling itself is accepted: (22*22 + 7) / 8 == 61 bytes still fits. */
    ASSERT_OK(ph_context_set_block_params(ctx, PH_BLOCK_MAX_SIZE));
    ASSERT_INT_EQ(PH_BLOCK_MAX_SIZE, ctx->config.block_size);

    /* Both ends: fewer angles than the DCT has coefficients is as invalid as more angles
     * than the largest supported image can resolve. */
    const int bad_projections[] = {1, 39, 200000, INT_MAX, PH_RADIAL_MAX_PROJECTIONS + 1};
    for (size_t i = 0; i < sizeof(bad_projections) / sizeof(bad_projections[0]); i++) {
        ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                      ph_context_set_radial_params(ctx, bad_projections[i], 64));
        ASSERT_INT_EQ(PH_RADIAL_PROJECTIONS, ctx->config.radial_projections);
        ASSERT_INT_EQ(PH_RADIAL_SAMPLES, ctx->config.radial_samples);
    }
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                  ph_context_set_radial_params(ctx, 180, PH_RADIAL_MAX_SAMPLES + 1));
    ASSERT_INT_EQ(PH_RADIAL_SAMPLES, ctx->config.radial_samples);
    ASSERT_OK(ph_context_set_radial_params(ctx, PH_RADIAL_MAX_PROJECTIONS, PH_RADIAL_MAX_SAMPLES));
    ASSERT_INT_EQ(PH_RADIAL_MAX_PROJECTIONS, ctx->config.radial_projections);
    ASSERT_INT_EQ(PH_RADIAL_MAX_SAMPLES, ctx->config.radial_samples);

    ph_free(ctx);
    PASS("test_setter_bounds_reject_out_of_range");
}

/* Sanity: a legitimate block_size still works (the guard must not over-reject). */
void test_bmh_normal_block_size_still_works(void) {
    ph_context_t *ctx = make_ctx_with_tiny_image(64, 48, 3);
    ph_context_set_block_params(ctx, 16);
    ph_digest_t d;
    ASSERT_OK(ph_compute_bmh(ctx, &d));
    ASSERT_INT_EQ(32, d.size); /* (16*16 + 7) / 8 */
    ph_free(ctx);
    PASS("test_bmh_normal_block_size_still_works");
}

/* Radial: a huge projection count must fail cleanly, not wrap the byte count.
 * As above, the config is poisoned directly since R04 -- the setter rejects these values,
 * and this test is about the arithmetic behind it. */
void test_radial_huge_projections(void) {
    /* Far more projections than the digest could ever hold: the byte count must be
     * computed in size_t (it is `projections * sizeof(double)`) and must not wrap. Since
     * 2.0.0 the digest is the DCT coefficients, so its size is PH_RADIAL_COEFFS whatever
     * the angle count -- no truncation, nothing to wrap through the uint8_t cast. */
    const int projections[] = {257, 4096, 200000};

    for (size_t i = 0; i < sizeof(projections) / sizeof(projections[0]); i++) {
        ph_context_t *ctx = make_ctx_with_tiny_image(32, 32, 3);
        ctx->config.radial_projections = projections[i];
        ctx->config.radial_samples = 64;
        ph_digest_t d;
        ASSERT_OK(ph_compute_radial_hash(ctx, &d));
        ASSERT_INT_EQ(PH_RADIAL_COEFFS, d.size);
        ph_free(ctx);
    }

    /* And the other end: too few angles to transform is refused outright rather than
     * answered with a short digest. */
    {
        ph_context_t *ctx = make_ctx_with_tiny_image(32, 32, 3);
        ctx->config.radial_projections = PH_RADIAL_COEFFS - 1;
        ctx->config.radial_samples = 64;
        ph_digest_t d;
        ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_radial_hash(ctx, &d));
        ph_free(ctx);
    }

#if defined(PH_TEST_ASAN)
    /* projections = INT_MAX asks for 16 GiB of doubles. Only assertable where
     * max_allocation_size_mb makes the refusal deterministic; a Release build may
     * well get the mapping from the OS and then spin for hours. */
    {
        ph_context_t *ctx = make_ctx_with_tiny_image(32, 32, 3);
        ctx->config.radial_projections = INT_MAX;
        ctx->config.radial_samples = 64;
        ph_digest_t d;
        ASSERT_INT_EQ(PH_ERR_ALLOCATION_FAILED, ph_compute_radial_hash(ctx, &d));
        ph_free(ctx);
    }
#endif

    /* And the normal path is untouched. */
    ph_context_t *ctx = make_ctx_with_tiny_image(32, 32, 3);
    ASSERT_OK(ph_context_set_radial_params(ctx, 180, 64));
    ph_digest_t d;
    ASSERT_OK(ph_compute_radial_hash(ctx, &d));
    ASSERT_INT_EQ(PH_RADIAL_COEFFS, d.size);
    ph_free(ctx);
    PASS("test_radial_huge_projections");
}

/* L6: ph_load_from_pixels() was the only load path without bomb protection. */
void test_load_from_pixels_respects_max_pixels(void) {
    uint8_t px[32 * 32 * 3];
    memset(px, 0x5A, sizeof(px));

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    /* 32*32 = 1024 pixels, limit 1023 -> rejected. */
    ph_context_set_max_pixels(ctx, 1023);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_pixels(ctx, px, 32, 32, 3, 0));
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    /* Exactly at the limit -> accepted (the check is `>`, like the file paths). */
    ph_context_set_max_pixels(ctx, 1024);
    ASSERT_OK(ph_load_from_pixels(ctx, px, 32, 32, 3, 0));
    ASSERT_INT_EQ(1, ph_is_loaded(ctx));

    ph_free(ctx);
    PASS("test_load_from_pixels_respects_max_pixels");
}

/* The default limit applies too, without the caller configuring anything --
 * this is the path that made H6 reachable with the default configuration. */
void test_load_from_pixels_default_limit(void) {
    uint8_t probe = 0;
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    /* 65536 * 65536 = 2^32 pixels, way over PH_DEFAULT_MAX_PIXELS (2^28).
     * Rejected before a single byte is read from `pixels`. */
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_pixels(ctx, &probe, 65536, 65536, 1, 0));

    /* Same product, but reached through int-overflowing dimensions. */
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_pixels(ctx, &probe, INT_MAX, INT_MAX, 1, 0));

    ph_free(ctx);
    PASS("test_load_from_pixels_default_limit");
}

/* max_pixels == 0 is documented as "unlimited". It must still degrade to a clean
 * PH_ERR_ALLOCATION_FAILED rather than a wrapped/undersized allocation. */
void test_load_from_pixels_unlimited(void) {
    uint8_t probe = 0;
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ph_context_set_max_pixels(ctx, 0);

    /* Since R48, max_pixels = 0 means "no limit of MY own", not "no limit at all":
     * the implementation ceiling of INT_MAX pixels still applies, and an image above
     * it is refused as too large rather than attempted. Before R48 this reached the
     * allocator (PH_ERR_ALLOCATION_FAILED) -- or, on a host that served the request,
     * overflowed int index arithmetic. */
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_pixels(ctx, &probe, INT_MAX, INT_MAX, 1, 0));
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    /* Unlimited still accepts an ordinary image. */
    uint8_t px[8 * 8 * 3];
    memset(px, 0x11, sizeof(px));
    ASSERT_OK(ph_load_from_pixels(ctx, px, 8, 8, 3, 0));

    ph_free(ctx);
    PASS("test_load_from_pixels_unlimited");
}

/* R48: the PH_MAX_SUPPORTED_PIXELS ceiling applies uniformly -- to max_pixels = 0
 * ("no caller limit") and to an explicitly configured value above it. Without it,
 * `y * w + x` in the hot loops overflows int, which is undefined behaviour reachable
 * through a documented configuration. */
void test_implementation_ceiling_applies_uniformly(void) {
    uint8_t probe = 0;
    /* 46342^2 = 2147580964 > INT_MAX (2147483647); 46341^2 = 2147488281 also just
     * above it. Both are the smallest square dimensions that cross the ceiling. */
    const int over = 46342;
    ph_context_t *ctx = NULL;

    ASSERT_OK(ph_create(&ctx));

    /* (a) explicit limit set far ABOVE the ceiling must not raise it */
    ph_context_set_max_pixels(ctx, (uint64_t)1 << 40);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_pixels(ctx, &probe, over, over, 1, 0));
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    /* (b) 0 ("no caller limit") must not raise it either */
    ph_context_set_max_pixels(ctx, 0);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_pixels(ctx, &probe, over, over, 1, 0));
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    /* (c) a stricter caller limit still wins over the ceiling */
    ph_context_set_max_pixels(ctx, 64);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_pixels(ctx, &probe, 16, 16, 1, 0));

    /* (d) and the ceiling does not get in the way of ordinary images */
    uint8_t px[4 * 4];
    memset(px, 0x7f, sizeof(px));
    ph_context_set_max_pixels(ctx, 0);
    ASSERT_OK(ph_load_from_pixels(ctx, px, 4, 4, 1, 0));
    ASSERT_INT_EQ(1, ph_is_loaded(ctx));

    ph_free(ctx);
    PASS("test_implementation_ceiling_applies_uniformly");
}

/* The ceiling limits width * height, not either dimension on its own: a very wide,
 * one-pixel-tall image is fine, which is also the shape a decompression bomb takes
 * (see M1 -- 268435456 x 1).
 *
 * Note this cannot be probed with a stub buffer the way the "too large" cases can:
 * ph_load_from_pixels() takes no buffer length, so it must trust the caller's
 * dimensions and will read w*h*channels bytes. Anything expected to SUCCEED therefore
 * needs a real buffer of that size. */
void test_ceiling_is_on_area_not_dimension(void) {
    const int wide = 1 << 20; /* 1048576 x 1: far past any single-dimension intuition */
    uint8_t probe = 0;
    ph_context_t *ctx = NULL;
    uint8_t *row = malloc((size_t)wide);

    ASSERT_PTR_NOT_NULL(row);
    memset(row, 0x40, (size_t)wide);

    ASSERT_OK(ph_create(&ctx));
    ph_context_set_max_pixels(ctx, 0);

    /* Extreme aspect ratio, area well under the ceiling -> accepted. */
    ASSERT_OK(ph_load_from_pixels(ctx, row, wide, 1, 1, 0));
    ASSERT_INT_EQ(1, ph_is_loaded(ctx));

    /* Same width, but an area past the ceiling -> refused before any pixel is touched,
     * so a stub buffer is safe here. */
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_pixels(ctx, &probe, INT_MAX, 2, 1, 0));

    ph_free(ctx);
    free(row);
    PASS("test_ceiling_is_on_area_not_dimension");
}

/* The size_t rewrite of ph_to_grayscale() must not let the SIMD tail condition
 * (`i <= n - 8`) wrap for images with fewer than 8 pixels. */
void test_tiny_images_grayscale(void) {
    for (int channels = 1; channels <= 4; channels++) {
        if (channels == 2)
            continue;
        for (int n = 1; n <= 9; n++) {
            ph_context_t *ctx = make_ctx_with_tiny_image(n, 1, channels);
            uint8_t *gray = ph_get_gray(ctx);
            ASSERT_PTR_NOT_NULL(gray);
            ph_apply_gamma(ctx, gray, n, 1);
            ph_free(ctx);
        }
    }
    PASS("test_tiny_images_grayscale");
}

int main(void) {
    test_bmh_block_size_overflows_int();
    test_setter_bounds_reject_out_of_range();
    test_bmh_normal_block_size_still_works();
    test_radial_huge_projections();
    test_load_from_pixels_respects_max_pixels();
    test_load_from_pixels_default_limit();
    test_load_from_pixels_unlimited();
    test_implementation_ceiling_applies_uniformly();
    test_ceiling_is_on_area_not_dimension();
    test_tiny_images_grayscale();
    printf("ALL PIXEL-COUNT OVERFLOW TESTS PASSED\n");
    return 0;
}
