#include "../../include/libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>

void verify_hash_consistency(const char *image_path, const char *algo_name,
                             ph_error_t (*hash_func)(ph_context_t *, uint64_t *)) {
    ph_context_t *ctx_rgb = NULL;
    ph_context_t *ctx_gray = NULL;
    uint64_t hash_rgb = 0, hash_gray = 0;

    ASSERT_OK(ph_create(&ctx_rgb));
    ASSERT_OK(ph_create(&ctx_gray));

    // Load as RGB (default)
    ph_context_set_load_grayscale(ctx_rgb, 0);
    ASSERT_OK(ph_load_from_file(ctx_rgb, image_path));

    // Load as Grayscale (decoder-level)
    ph_context_set_load_grayscale(ctx_gray, 1);
    ASSERT_OK(ph_load_from_file(ctx_gray, image_path));

    ASSERT_OK(hash_func(ctx_rgb, &hash_rgb));
    ASSERT_OK(hash_func(ctx_gray, &hash_gray));

    int distance = ph_hamming_distance(hash_rgb, hash_gray);
    printf("[%s] %s: Dist=%d\n", algo_name, image_path, distance);

    // Threshold of 20 bits (out of 64) is needed for noisy images + different conversion weights
    // This often happens in the minimal (stb_image) build where we don't control the decoder
    // coefficients.
    if (distance > 20) {
        fprintf(stderr, "[FAIL] %s: RGB and Grayscale hashes differ too much (dist %d)\n",
                algo_name, distance);
        exit(1);
    }

    ph_free(ctx_rgb);
    ph_free(ctx_gray);
}

int main() {
    const char *jpeg = TEST_DATA_DIR "/photo.jpeg";
    const char *png = TEST_DATA_DIR "/photo_complex.png";
#ifdef PH_USE_WEBP
    const char *webp = TEST_DATA_DIR "/photo.webp";
#endif

    printf("--- Checking Consistency: RGB vs Decoder-Grayscale ---\n");

    verify_hash_consistency(jpeg, "aHash", ph_compute_ahash);
    verify_hash_consistency(png, "aHash", ph_compute_ahash);
#ifdef PH_USE_WEBP
    verify_hash_consistency(webp, "aHash", ph_compute_ahash);
#endif

    verify_hash_consistency(jpeg, "pHash", ph_compute_phash);
    verify_hash_consistency(png, "pHash", ph_compute_phash);
#ifdef PH_USE_WEBP
    verify_hash_consistency(webp, "pHash", ph_compute_phash);
#endif

    verify_hash_consistency(jpeg, "dHash", ph_compute_dhash);
    verify_hash_consistency(png, "dHash", ph_compute_dhash);
#ifdef PH_USE_WEBP
    verify_hash_consistency(webp, "dHash", ph_compute_dhash);
#endif

    printf("test_stability: PASSED\n");
    return 0;
}
