#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <string.h>

/* Every combination of the 6 ph_hash_flags_t bits, including the empty and full sets. */
#define ALL_FLAGS_MASK (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH)

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
        for (uint32_t bit = 1; bit <= PH_HASH_WHASH; bit <<= 1) {
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

/* ph_compute_multi() must write exactly one slot per flag set, and touch nothing beyond
 * that. A caller sizes `out` by the bits it asked for, so a fifth write on a four-bit
 * request is a buffer overrun in the caller's frame; and a slot it did not ask for being
 * clobbered would make the "ascending bit order" packing unreadable. */
static void test_multi_writes_exactly_one_slot_per_flag(void) {
    const uint64_t SENTINEL = 0xA5A5A5A5A5A5A5A5ULL;

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    for (uint32_t flags = 1; flags <= ALL_FLAGS_MASK; flags++) {
        if (flags & ~(uint32_t)ALL_FLAGS_MASK)
            continue;
        int nset = PH_TEST_POPCOUNT(flags);

        /* One slot of slack past the array the caller would legitimately size, so an
         * off-by-one write lands somewhere observable rather than in the caller's stack. */
        uint64_t out[PH_HASH_FLAGS_COUNT + 1];
        for (size_t i = 0; i < sizeof(out) / sizeof(out[0]); i++)
            out[i] = SENTINEL;

        ASSERT_OK(ph_compute_multi(ctx, flags, out));

        for (int i = nset; i < PH_HASH_FLAGS_COUNT + 1; i++) {
            if (out[i] != SENTINEL) {
                fprintf(stderr,
                        "[FAIL] test_multi_writes_exactly_one_slot_per_flag: flags=0x%x wrote "
                        "slot %d, only %d were requested\n",
                        flags, i, nset);
                exit(1);
            }
        }
    }

    ph_free(ctx);
    PASS("test_multi_writes_exactly_one_slot_per_flag");
}

/* The failure contract: computation stops at the first algorithm that fails, its error
 * comes out as the return value, and the slots of the algorithms that had already run
 * keep their (correct) values while the rest stay untouched.
 *
 * Reaching a failing algorithm at all takes some doing in 2.0.0: the four hashes the
 * bitfield can still express cannot fail on a loaded image, and the setters now reject
 * every out-of-range configuration. pHash's own guard against an invalid dct_size is
 * therefore driven the only way it is reachable -- by writing the config field directly,
 * the same "config written by some other route" case bmh.c documents. What is being
 * tested is ph_compute_multi's propagation, not how the field got there. */
static void test_multi_propagates_algorithm_failure(void) {
    const uint64_t SENTINEL = 0xA5A5A5A5A5A5A5A5ULL;

    ph_context_t *ctx = NULL;
    ph_context_t *ref = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_create(&ref));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_OK(ph_load_from_file(ref, TEST_DATA_DIR "/photo.jpeg"));

    /* Rejected by ph_context_set_phash_params(), so it has to be planted here. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_phash_params(ctx, 0, 8));
    ctx->config.phash_dct_size = 0;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_phash(ctx, &(uint64_t){0}));

    /* pHash alone: nothing ran before it, so nothing was written. */
    uint64_t out[PH_HASH_FLAGS_COUNT] = {SENTINEL, SENTINEL, SENTINEL, SENTINEL};
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_multi(ctx, PH_HASH_PHASH, out));
    ASSERT_UINT64_EQ(SENTINEL, out[0]);

    /* pHash third of four: aHash and dHash ran and are valid, wHash never ran. */
    for (int i = 0; i < PH_HASH_FLAGS_COUNT; i++)
        out[i] = SENTINEL;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_multi(ctx, ALL_FLAGS_MASK, out));

    uint64_t expect_a = reference_hash(ref, PH_HASH_AHASH);
    uint64_t expect_d = reference_hash(ref, PH_HASH_DHASH);
    ASSERT_UINT64_EQ(expect_a, out[0]);
    ASSERT_UINT64_EQ(expect_d, out[1]);
    ASSERT_UINT64_EQ(SENTINEL, out[2]); /* the algorithm that failed */
    ASSERT_UINT64_EQ(SENTINEL, out[3]); /* and the one after it */

    /* A combination that skips the broken algorithm still succeeds: the failure is
     * pHash's, not the context's. */
    for (int i = 0; i < PH_HASH_FLAGS_COUNT; i++)
        out[i] = SENTINEL;
    ASSERT_OK(ph_compute_multi(ctx, PH_HASH_AHASH | PH_HASH_WHASH, out));
    ASSERT_UINT64_EQ(expect_a, out[0]);
    ASSERT_UINT64_EQ(reference_hash(ref, PH_HASH_WHASH), out[1]);

    ph_free(ctx);
    ph_free(ref);
    PASS("test_multi_propagates_algorithm_failure");
}

/* The four algorithms the bitfield still holds are all grayscale ones, so a context
 * loaded with load_grayscale enabled must serve every one of them -- and give the same
 * answers as the individual entry points do on that same context. (The colour algorithms,
 * which are what fails under grayscale, left the bitfield in 2.0.0; they are called
 * directly and tested with their own algorithms.) */
static void test_multi_on_grayscale_loaded_image(void) {
    ph_context_t *ctx = NULL;
    ph_context_t *ref = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_create(&ref));
    ASSERT_OK(ph_context_set_load_grayscale(ctx, 1));
    ASSERT_OK(ph_context_set_load_grayscale(ref, 1));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_OK(ph_load_from_file(ref, TEST_DATA_DIR "/photo.jpeg"));

    int w, h, ch;
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    ASSERT_INT_EQ(1, ch);

    uint64_t out[PH_HASH_FLAGS_COUNT] = {0};
    ASSERT_OK(ph_compute_multi(ctx, ALL_FLAGS_MASK, out));
    ASSERT_UINT64_EQ(reference_hash(ref, PH_HASH_AHASH), out[0]);
    ASSERT_UINT64_EQ(reference_hash(ref, PH_HASH_DHASH), out[1]);
    ASSERT_UINT64_EQ(reference_hash(ref, PH_HASH_PHASH), out[2]);
    ASSERT_UINT64_EQ(reference_hash(ref, PH_HASH_WHASH), out[3]);

    ph_free(ctx);
    ph_free(ref);
    PASS("test_multi_on_grayscale_loaded_image");
}

/* The retired bits (1 << 4 was PH_HASH_MHASH, 1 << 5 was PH_HASH_COLOR_HASH) must be
 * refused rather than quietly ignored: a caller compiled against 1.x that still passes
 * them is asking for a hash this function cannot produce, and silently returning the
 * remaining three would hand it a differently-packed out[] under a PH_SUCCESS. */
static void test_multi_rejects_retired_flag_bits(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    uint64_t out[PH_HASH_FLAGS_COUNT] = {0};
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_multi(ctx, 1u << 4, out));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_multi(ctx, 1u << 5, out));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_multi(ctx, ALL_FLAGS_MASK | (1u << 4), out));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_multi(ctx, 0xFFFFFFFFu, out));

    /* Nothing was computed on the way to the refusal. */
    ASSERT_UINT64_EQ(0, out[0]);

    ph_free(ctx);
    PASS("test_multi_rejects_retired_flag_bits");
}

int main() {
    test_multi_matches_individual_calls(TEST_DATA_DIR "/photo.jpeg");
    test_multi_matches_individual_calls(TEST_DATA_DIR "/photo_complex.png");
    test_multi_invalid_args();
    test_multi_writes_exactly_one_slot_per_flag();
    test_multi_propagates_algorithm_failure();
    test_multi_on_grayscale_loaded_image();
    test_multi_rejects_retired_flag_bits();
    return 0;
}
