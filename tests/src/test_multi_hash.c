#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>

/* Every combination of the 6 ph_hash_flags_t bits, including the empty and full sets. */
#define ALL_FLAGS_MASK                                                                             \
    (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH | PH_HASH_MHASH |               \
     PH_HASH_COLOR_HASH)

static uint64_t reference_hash(ph_context_t *ctx, uint32_t flag) {
    uint64_t h = 0;
    ph_error_t err;
    switch (flag) {
        case PH_HASH_AHASH:
            err = ph_compute_ahash(ctx, &h);
            break;
        case PH_HASH_DHASH:
            err = ph_compute_dhash(ctx, &h);
            break;
        case PH_HASH_PHASH:
            err = ph_compute_phash(ctx, &h);
            break;
        case PH_HASH_WHASH:
            err = ph_compute_whash(ctx, &h);
            break;
        case PH_HASH_MHASH:
            err = ph_compute_mhash(ctx, &h);
            break;
        case PH_HASH_COLOR_HASH:
            err = ph_compute_color_hash(ctx, &h);
            break;
        default:
            fprintf(stderr, "reference_hash: unexpected flag %u\n", flag);
            exit(1);
    }
    ASSERT_OK(err);
    return h;
}

/* For every non-empty subset of ALL_FLAGS_MASK, ph_compute_multi() must return exactly
 * the same uint64_t values, in the same order, as calling the single-hash functions
 * directly. */
static void test_multi_matches_individual_calls(const char *filepath) {
    for (uint32_t flags = 1; flags <= ALL_FLAGS_MASK; flags++) {
        if (flags & ~(uint32_t)ALL_FLAGS_MASK)
            continue;

        ph_context_t *ctx_multi = NULL;
        ph_context_t *ctx_ref = NULL;
        ASSERT_OK(ph_create(&ctx_multi));
        ASSERT_OK(ph_create(&ctx_ref));
        ASSERT_OK(ph_load_from_file(ctx_multi, filepath));
        ASSERT_OK(ph_load_from_file(ctx_ref, filepath));

        uint64_t multi_out[PH_HASH_FLAGS_COUNT] = {0};
        ASSERT_OK(ph_compute_multi(ctx_multi, flags, multi_out));

        int idx = 0;
        for (uint32_t bit = 1; bit <= PH_HASH_COLOR_HASH; bit <<= 1) {
            if (!(flags & bit))
                continue;
            uint64_t expected = reference_hash(ctx_ref, bit);
            if (multi_out[idx] != expected) {
                fprintf(stderr,
                        "[FAIL] test_multi_matches_individual_calls: flags=0x%x bit=0x%x "
                        "expected=%llu got=%llu\n",
                        flags, bit, (unsigned long long)expected,
                        (unsigned long long)multi_out[idx]);
                exit(1);
            }
            idx++;
        }

        ph_free(ctx_multi);
        ph_free(ctx_ref);
    }

    printf("test_multi_matches_individual_calls(%s): PASSED\n", filepath);
}

static void test_multi_invalid_args() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    uint64_t out[PH_HASH_FLAGS_COUNT] = {0};

    /* No image loaded yet. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_multi(ctx, PH_HASH_AHASH, out));

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    /* NULL ctx / out. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_multi(NULL, PH_HASH_AHASH, out));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_multi(ctx, PH_HASH_AHASH, NULL));

    /* Empty flag set. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_multi(ctx, 0, out));

    /* Unknown bit set alongside a valid one. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_multi(ctx, PH_HASH_AHASH | (1u << 31), out));

    ph_free(ctx);
    PASS("test_multi_invalid_args");
}

int main() {
    test_multi_matches_individual_calls(TEST_DATA_DIR "/photo.jpeg");
    test_multi_matches_individual_calls(TEST_DATA_DIR "/photo_complex.png");
    test_multi_invalid_args();
    return 0;
}
