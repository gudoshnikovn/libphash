#include "../../include/libphash.h"
#include "../../src/internal.h"
#include "test_macros.h"
#include <stdio.h>
#include <string.h>

void test_laplacian_scan_unit() {
    uint8_t grid[18 * 18];

    // Test 1: Uniform grid (all same value)
    memset(grid, 100, sizeof(grid));
    // center*4 = 400, neighbors = 400. 400-400=0, not > 0.
    ASSERT_UINT64_EQ(0, ph_laplacian_scan(grid, 18, 2));

    // Test 2: Single bright center pixel
    memset(grid, 0, sizeof(grid));
    grid[9 * 18 + 9] = 100;
    // For 18x18, step=2:
    // y in {1, 3, 5, 7, 9, 11, 13, 15}
    // x in {1, 3, 5, 7, 9, 11, 13, 15}
    // (9, 9) is y_idx=4, x_idx=4. bit_idx = 4 * 8 + 4 = 36.
    uint64_t hash = ph_laplacian_scan(grid, 18, 2);
    ASSERT_UINT64_EQ(1ULL << 36, hash);

    // Test 3: step=1 vs step=2 produce different hashes
    memset(grid, 0, sizeof(grid));
    for (int i = 0; i < 18 * 18; i++)
        grid[i] = (uint8_t)(i % 255);
    uint64_t h1 = ph_laplacian_scan(grid, 18, 1);
    uint64_t h2 = ph_laplacian_scan(grid, 18, 2);
    if (h1 == h2) {
        fprintf(stderr, "MHash failure: step=1 and step=2 produced same hash\n");
        exit(1);
    }

    PASS("test_laplacian_scan_unit");
}

void test_mhash_e2e() {
    ph_context_t *ctx = NULL;
    uint64_t hash_orig = 0, hash_copy = 0, hash_mod = 0;

    ASSERT_OK(ph_create(&ctx));

    // Base image
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_OK(ph_compute_mhash(ctx, &hash_orig));

    // Identical image
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_copy.jpeg"));
    ASSERT_OK(ph_compute_mhash(ctx, &hash_copy));
    ASSERT_UINT64_EQ(hash_orig, hash_copy);

    // Color changed image
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_color_changed.jpeg"));
    ASSERT_OK(ph_compute_mhash(ctx, &hash_mod));

    int dist = ph_hamming_distance(hash_orig, hash_mod);
    // MHash tracks structural edges, so color shifts shouldn't change it much
    if (dist > 12) {
        fprintf(stderr, "MHash failed: too sensitive to color changes (dist: %d)\n", dist);
        exit(1);
    }

    ph_free(ctx);
    PASS("test_mhash_e2e");
}

int main() {
    test_laplacian_scan_unit();
    test_mhash_e2e();
    return 0;
}
