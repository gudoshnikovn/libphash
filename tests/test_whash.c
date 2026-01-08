#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>

void test_whash_logic() {
    ph_context_t *ctx1 = NULL;
    ph_context_t *ctx2 = NULL;
    uint64_t hash1 = 0, hash2 = 0;

    ASSERT_OK(ph_create(&ctx1));
    ASSERT_OK(ph_create(&ctx2));

    // Load original and a copy
    ASSERT_OK(ph_load_from_file(ctx1, "tests/photo.jpeg"));
    ASSERT_OK(ph_load_from_file(ctx2, "tests/photo_copy.jpeg"));

    ASSERT_OK(ph_compute_whash(ctx1, &hash1));
    ASSERT_OK(ph_compute_whash(ctx2, &hash2));

    int dist = ph_hamming_distance(hash1, hash2);
    printf("[WHash] Hash: %llu, Distance: %d\n", (unsigned long long)hash1, dist);

    // WHash is very stable for copies
    if (dist > 5) {
        fprintf(stderr, "WHash distance too high for identical images: %d\n", dist);
        exit(1);
    }

    ph_free(ctx1);
    ph_free(ctx2);
    printf("test_whash: PASSED\n");
}

int main() {
    test_whash_logic();
    return 0;
}
