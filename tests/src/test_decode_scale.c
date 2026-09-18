/* ph_context_set_decode_scale(): opt-in reduced-resolution JPEG decode. Default is
 * PH_DECODE_SCALE_FULL, so the invariant this file exists to pin is "nothing changes
 * unless a caller explicitly asks" -- see the setter's doc comment in libphash.h for
 * the measurements behind that default. */
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>

/* tests/data/photo.jpeg is 400x400; tests/data/photo.png is 100x100 (checked once
 * here, not re-derived per test, since both fixtures are relied on unchanged by other
 * test files too). */

void test_decode_scale_default_is_full(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    int w, h, ch;
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    ASSERT_INT_EQ(400, w);
    ASSERT_INT_EQ(400, h);

    ph_free(ctx);
    PASS("test_decode_scale_default_is_full");
}

void test_decode_scale_setter_validation(void) {
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_decode_scale(NULL, PH_DECODE_SCALE_HALF));

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    /* Not one of the declared enumerators -- same contract as
     * ph_context_set_whash_mode(). */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_decode_scale(ctx, (ph_decode_scale_t)99));

    ASSERT_OK(ph_context_set_decode_scale(ctx, PH_DECODE_SCALE_FULL));
    ASSERT_OK(ph_context_set_decode_scale(ctx, PH_DECODE_SCALE_HALF));
    ASSERT_OK(ph_context_set_decode_scale(ctx, PH_DECODE_SCALE_QUARTER));
    ASSERT_OK(ph_context_set_decode_scale(ctx, PH_DECODE_SCALE_EIGHTH));

    ph_free(ctx);
    PASS("test_decode_scale_setter_validation");
}

/* On a build without the native JPEG backend (PH_USE_TURBOJPEG), decode_scale is
 * documented as ignored -- stb_image has no scaled-decode path, so the loaded image
 * stays at full resolution whatever the setting. Gate the scaled-size assertions on
 * ph_can_use_libjpeg() so this test asserts the right thing in both build
 * configurations rather than only passing on one of them. */
void test_decode_scale_jpeg_scaled(void) {
    struct {
        ph_decode_scale_t scale;
        int expected_side_if_native;
    } cases[] = {
        {PH_DECODE_SCALE_HALF, 200},
        {PH_DECODE_SCALE_QUARTER, 100},
        {PH_DECODE_SCALE_EIGHTH, 50},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_context_set_decode_scale(ctx, cases[i].scale));
        ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

        int w, h, ch;
        ph_context_get_dimensions(ctx, &w, &h, &ch);
        int expected = ph_can_use_libjpeg() ? cases[i].expected_side_if_native : 400;
        ASSERT_INT_EQ(expected, w);
        ASSERT_INT_EQ(expected, h);

        /* The scaled buffer must still be a valid, fully-hashable image -- not just
         * the right dimensions. */
        uint64_t hash;
        ASSERT_OK(ph_compute_ahash(ctx, &hash));

        ph_free(ctx);
    }
    PASS("test_decode_scale_jpeg_scaled");
}

/* PNG has no format-level scaled decode; decode_scale must be a silent no-op there
 * regardless of which JPEG backend this build has. */
void test_decode_scale_png_ignored(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_context_set_decode_scale(ctx, PH_DECODE_SCALE_EIGHTH));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.png"));

    int w, h, ch;
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    ASSERT_INT_EQ(100, w);
    ASSERT_INT_EQ(100, h);

    ph_free(ctx);
    PASS("test_decode_scale_png_ignored");
}

int main(void) {
    test_decode_scale_default_is_full();
    test_decode_scale_setter_validation();
    test_decode_scale_jpeg_scaled();
    test_decode_scale_png_ignored();
    printf("ALL TESTS PASSED\n");
    return 0;
}
