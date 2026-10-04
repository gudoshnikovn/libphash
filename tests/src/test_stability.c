/*
 * test_stability.c
 *
 * The same file loaded in colour and loaded as grayscale (ph_context_set_load_grayscale())
 * must hash alike. Where both loads reach the gray values through the same arithmetic,
 * "alike" is exact:
 *
 *   - PNG: every PNG backend folds colour to gray with the library's own weights,
 *     38/75/15 over 128, the ones ph_to_grayscale() uses on the colour load;
 *   - WebP: libwebp has no gray output, so the core converts, as on the colour load;
 *   - JPEG through stb_image: decoded to RGB and folded with the same weights.
 *
 * JPEG through libjpeg-turbo is the one real difference: asked for grayscale, it returns
 * the luma it decoded from YCbCr, not a gray computed from the RGB it would otherwise have
 * produced, so a pixel can land a level apart. That moves a hash only where values sit
 * next to its threshold. Measured on photo.jpeg with the vendored libjpeg-turbo
 * (2026-10-04): at most 1 bit over the four hashes, from dHash. The bound is 3 bits:
 * above the measurement, and still a small fraction of the ~30 bits between unrelated
 * images.
 *
 * The test prints ph_get_build_info() first, so a log shows which decoder gave which
 * distance.
 */

#include "libphash.h"
#include "test_macros.h"

#include <stdio.h>
#include <stdlib.h>

static void verify_hash_consistency(const char *image_path, const char *algo_name,
                                    ph_error_t (*hash_func)(ph_context_t *, uint64_t *),
                                    int max_distance) {
    ph_context_t *ctx_rgb = NULL;
    ph_context_t *ctx_gray = NULL;
    uint64_t hash_rgb = 0, hash_gray = 0;

    ASSERT_OK(ph_create(&ctx_rgb));
    ASSERT_OK(ph_create(&ctx_gray));
    ASSERT_OK(ph_context_set_load_grayscale(ctx_rgb, 0));
    ASSERT_OK(ph_load_from_file(ctx_rgb, image_path));
    ASSERT_OK(ph_context_set_load_grayscale(ctx_gray, 1));
    ASSERT_OK(ph_load_from_file(ctx_gray, image_path));

    ASSERT_OK(hash_func(ctx_rgb, &hash_rgb));
    ASSERT_OK(hash_func(ctx_gray, &hash_gray));

    int distance = ph_hamming_distance(hash_rgb, hash_gray);
    printf("[%s] %s: distance %d (bound %d)\n", algo_name, image_path, distance, max_distance);
    if (distance > max_distance) {
        fprintf(stderr,
                "[FAIL] %s: colour and grayscale loads of %s differ by %d bits (bound %d)\n",
                algo_name, image_path, distance, max_distance);
        exit(1);
    }

    ph_free(ctx_rgb);
    ph_free(ctx_gray);
}

int main(void) {
    const char *jpeg = TEST_DATA_DIR "/photo.jpeg";
    const char *png = TEST_DATA_DIR "/photo_complex.png";
#ifdef PH_USE_LIBJPEG_TURBO
    const int jpeg_bound = 3;
#else
    const int jpeg_bound = 0;
#endif

    printf("--- Checking Consistency: colour load vs grayscale load ---\n");
    printf("build: %s\n", ph_get_build_info());

    struct {
        const char *name;
        ph_error_t (*fn)(ph_context_t *, uint64_t *);
    } algos[] = {{"aHash", ph_compute_ahash},
                 {"dHash", ph_compute_dhash},
                 {"pHash", ph_compute_phash},
                 {"wHash", ph_compute_whash}};

    for (size_t i = 0; i < sizeof(algos) / sizeof(algos[0]); i++) {
        verify_hash_consistency(jpeg, algos[i].name, algos[i].fn, jpeg_bound);
        verify_hash_consistency(png, algos[i].name, algos[i].fn, 0);
#ifdef PH_USE_WEBP
        verify_hash_consistency(TEST_DATA_DIR "/photo.webp", algos[i].name, algos[i].fn, 0);
#endif
    }

    printf("test_stability: PASSED\n");
    return 0;
}
