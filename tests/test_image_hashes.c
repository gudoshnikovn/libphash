#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>

void test_hash_algorithm(const char *name, ph_error_t (*hash_func)(ph_context_t *, uint64_t *)) {
    ph_context_t *ctx1 = NULL;
    ph_context_t *ctx2 = NULL;
    uint64_t hash1 = 0, hash2 = 0;

    ASSERT_OK(ph_create(&ctx1));
    ASSERT_OK(ph_create(&ctx2));

    ASSERT_OK(ph_load_from_file(ctx1, "tests/photo.jpeg"));
    ASSERT_OK(ph_load_from_file(ctx2, "tests/photo_copy.jpeg"));

    ASSERT_OK(hash_func(ctx1, &hash1));
    ASSERT_OK(hash_func(ctx2, &hash2));

    int distance = ph_hamming_distance(hash1, hash2);

    printf("[%s] Hash1: %llu, Hash2: %llu, Distance: %d\n", name, (unsigned long long)hash1,
           (unsigned long long)hash2, distance);

    if (distance > 10) {
        fprintf(stderr, "[FAIL] %s: Images are too different (distance %d)\n", name, distance);
        exit(1);
    }

    ph_free(ctx1);
    ph_free(ctx2);
    printf("test_%s: PASSED\n", name);
}

int main() {
    test_hash_algorithm("aHash", ph_compute_ahash);
    test_hash_algorithm("dHash", ph_compute_dhash);
    test_hash_algorithm("pHash", ph_compute_phash);
    return 0;
}
