#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>

void test_color_difference() {
    ph_context_t *ctx_orig = NULL;
    ph_context_t *ctx_color = NULL;

    // CHANGE: Allocate digests on the stack. Initialize to 0 for safety.
    ph_digest_t digest_orig = {0};
    ph_digest_t digest_color = {0};

    uint64_t phash_orig, phash_color;

    ASSERT_OK(ph_create(&ctx_orig));
    ASSERT_OK(ph_create(&ctx_color));

    /* 1. Load original and color-shifted images */
    ASSERT_OK(ph_load_from_file(ctx_orig, "tests/photo.jpeg"));
    ASSERT_OK(ph_load_from_file(ctx_color, "tests/photo_color_changed.jpeg"));

    /* 2. Compute pHash (Structural) */
    ASSERT_OK(ph_compute_phash(ctx_orig, &phash_orig));
    ASSERT_OK(ph_compute_phash(ctx_color, &phash_color));

    /* 3. Compute Color Moment Hash (Color distribution) */
    // CHANGE: Pass the address of the stack-allocated structs
    ASSERT_OK(ph_compute_color_hash(ctx_orig, &digest_orig));
    ASSERT_OK(ph_compute_color_hash(ctx_color, &digest_color));

    /* 4. Compare results */
    int p_dist = ph_hamming_distance(phash_orig, phash_color);
    double c_dist = ph_l2_distance(&digest_orig, &digest_color);

    printf("[pHash] Structural distance: %d bits\n", p_dist);
    printf("[ColorHash] L2 Color distance: %.2f\n", c_dist);

    /*
     * In this scenario, pHash distance should be low (same shapes),
     * but ColorHash distance should be significant (different colors).
     */
    if (p_dist < 5 && c_dist > 10.0) {
        printf("Test Logic: Images are structurally similar but color-distinct. "
               "SUCCESS.\n");
    }

    /* Cleanup */
    ph_free(ctx_orig);
    ph_free(ctx_color);
}

int main() {
    test_color_difference();
    return 0;
}
