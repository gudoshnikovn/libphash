#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>

void test_mhash_logic() {
    ph_context_t *ctx = NULL;
    uint64_t hash_orig = 0, hash_mod = 0;

    ASSERT_OK(ph_create(&ctx));

    // Test 1: Base image
    ASSERT_OK(ph_load_from_file(ctx, "tests/photo.jpeg"));
    ASSERT_OK(ph_compute_mhash(ctx, &hash_orig));

    // Test 2: Color changed image (MHash should be resistant to this as it tracks
    // edges)
    ASSERT_OK(ph_load_from_file(ctx, "tests/photo_color_changed.jpeg"));
    ASSERT_OK(ph_compute_mhash(ctx, &hash_mod));

    int dist = ph_hamming_distance(hash_orig, hash_mod);
    printf("[MHash] Original: %llu, Color-Changed: %llu, Distance: %d\n",
           (unsigned long long)hash_orig, (unsigned long long)hash_mod, dist);

    // MHash tracks structural edges, so color shifts shouldn't change the edge
    // skeleton much
    if (dist > 12) {
        fprintf(stderr, "MHash failed: too sensitive to color changes (dist: %d)\n", dist);
        exit(1);
    }

    ph_free(ctx);
    printf("test_mhash: PASSED\n");
}

int main() {
    test_mhash_logic();
    return 0;
}
