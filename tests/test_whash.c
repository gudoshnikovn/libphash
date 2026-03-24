#include "libphash.h"
#include "test_macros.h"
#include "../src/internal.h"
#include <stdio.h>
#include <string.h>

void test_haar_1d_unit() {
    float data[2] = {100.0f, 50.0f};
    float temp[2];
    ph_haar_1d_float(data, 2, temp);

    // (100+50)/sqrt(2) ≈ 106.066
    // (100-50)/sqrt(2) ≈ 35.355
    ASSERT_FLOAT_EQ(106.066, data[0], 0.01);
    ASSERT_FLOAT_EQ(35.355, data[1], 0.01);

    PASS("test_haar_1d_unit");
}

void test_whash_e2e() {
    ph_context_t *ctx = NULL;
    uint64_t hash1, hash2;

    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, "tests/photo.jpeg"));

    // FAST mode (default or explicit)
    ph_context_set_whash_mode(ctx, PH_WHASH_FAST);
    ASSERT_OK(ph_compute_whash(ctx, &hash1));

    uint64_t hash_copy;
    ph_context_t *ctx_copy = NULL;
    ASSERT_OK(ph_create(&ctx_copy));
    ASSERT_OK(ph_load_from_file(ctx_copy, "tests/photo_copy.jpeg"));
    ph_context_set_whash_mode(ctx_copy, PH_WHASH_FAST);
    ASSERT_OK(ph_compute_whash(ctx_copy, &hash_copy));
    ASSERT_UINT64_EQ(hash1, hash_copy);

    // FULL mode
    ph_context_set_whash_mode(ctx, PH_WHASH_FULL);
    ASSERT_OK(ph_compute_whash(ctx, &hash1));

    ph_context_set_whash_mode(ctx_copy, PH_WHASH_FULL);
    ASSERT_OK(ph_compute_whash(ctx_copy, &hash2));
    ASSERT_UINT64_EQ(hash1, hash2);

    ph_free(ctx);
    ph_free(ctx_copy);
    PASS("test_whash_e2e");
}

int main() {
    test_haar_1d_unit();
    test_whash_e2e();
    return 0;
}
