#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>

void test_config_gray_weights() {
    ph_context_t *ctx;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_color_changed.jpeg"));

    uint64_t hash_default, hash_custom;
    ASSERT_OK(ph_compute_ahash(ctx, &hash_default));

    // Change weights to be very red-sensitive
    ph_context_set_gray_weights(ctx, 128, 0, 0);
    ASSERT_OK(ph_compute_ahash(ctx, &hash_custom));

    if (hash_default == hash_custom) {
        fprintf(stderr, "Gray weights test failed: hashes are identical\n");
        exit(1);
    }

    ph_free(ctx);
    printf("  [PASS] Custom Gray Weights\n");
}

void test_config_phash_params() {
    ph_context_t *ctx;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    uint64_t hash1, hash2;
    ASSERT_OK(ph_compute_phash(ctx, &hash1));

    // Change DCT size
    ph_context_set_phash_params(ctx, 16, 8);
    ASSERT_OK(ph_compute_phash(ctx, &hash2));

    if (hash1 == hash2) {
        fprintf(stderr, "pHash params test failed: hashes are identical\n");
        exit(1);
    }

    ph_free(ctx);
    printf("  [PASS] pHash Parameters\n");
}

void test_config_radial_params() {
    ph_context_t *ctx;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    ph_digest_t d1, d2;
    ASSERT_OK(ph_compute_radial_hash(ctx, &d1));

    // Increase projections
    ph_context_set_radial_params(ctx, 60, 128);
    ASSERT_OK(ph_compute_radial_hash(ctx, &d2));

    if (d1.size != 40 || d2.size != 60) {
        fprintf(stderr, "Radial size test failed: %d vs %d\n", d1.size, d2.size);
        exit(1);
    }

    ph_free(ctx);
    printf("  [PASS] Radial Parameters\n");
}

void test_config_block_size() {
    ph_context_t *ctx;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    ph_digest_t d1, d2;
    ASSERT_OK(ph_compute_bmh(ctx, &d1)); // Default 16x16 -> 32 bytes

    ph_context_set_block_params(ctx, 8);
    ASSERT_OK(ph_compute_bmh(ctx, &d2)); // 8x8 -> 8 bytes

    if (d1.size != 32 || d2.size != 8) {
        fprintf(stderr, "Block size test failed: %d vs %d\n", d1.size, d2.size);
        exit(1);
    }

    ph_free(ctx);
    printf("  [PASS] Block Size (BMH)\n");
}

int main() {
    printf("Running Configuration Tests...\n");
    test_config_gray_weights();
    test_config_phash_params();
    test_config_radial_params();
    test_config_block_size();
    printf("All Configuration Tests Passed!\n");
    return 0;
}
