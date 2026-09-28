/* Algorithms as values: ph_algorithm_t, ph_compute_digest(), ph_digest_info() and the
 * names.
 *
 * The central check is the reconciliation: for every algorithm, and for BMH at every
 * block size the setter accepts, the digest ph_compute_digest() returns has exactly the
 * size and kind ph_digest_info() predicted, and is exactly what the algorithm's own
 * ph_compute_* function returns. Both answers come from one function in the library
 * (ph_digest_shape()); this test is what keeps it that way if someone sets a size by
 * hand. */

#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <string.h>

static ph_error_t compute_direct(ph_context_t *ctx, ph_algorithm_t algo, ph_digest_t *out,
                                 uint64_t *out_hash) {
    switch (algo) {
        case PH_ALGO_AHASH:
            return ph_compute_ahash(ctx, out_hash);
        case PH_ALGO_DHASH:
            return ph_compute_dhash(ctx, out_hash);
        case PH_ALGO_PHASH:
            return ph_compute_phash(ctx, out_hash);
        case PH_ALGO_WHASH:
            return ph_compute_whash(ctx, out_hash);
        case PH_ALGO_BMH:
            return ph_compute_bmh(ctx, out);
        case PH_ALGO_MHASH:
            return ph_compute_mhash(ctx, out);
        case PH_ALGO_RADIAL:
            return ph_compute_radial_hash(ctx, out);
        case PH_ALGO_COLOR_HASH:
            return ph_compute_color_hash(ctx, out);
        case PH_ALGO_COLOR_MOMENTS:
            return ph_compute_color_moments_hash(ctx, out);
        default:
            return PH_ERR_INVALID_ARGUMENT;
    }
}

static int is_uint64_algorithm(int algo) { return algo <= PH_ALGO_WHASH; }

/* One algorithm on one configured, loaded context: info == computed shape, and the
 * dispatcher == the direct function. */
static void reconcile(ph_context_t *ctx, ph_algorithm_t algo, const char *what) {
    size_t size = 0;
    ph_digest_kind_t kind = PH_DIGEST_KIND_UNSPECIFIED;
    ASSERT_OK(ph_digest_info(ctx, algo, &size, &kind));

    ph_digest_t via_dispatch;
    memset(&via_dispatch, 0xCD, sizeof(via_dispatch));
    ASSERT_OK(ph_compute_digest(ctx, algo, &via_dispatch));
    if (via_dispatch.size != size || via_dispatch.kind != (uint8_t)kind) {
        fprintf(stderr,
                "[FAIL] %s (%s): ph_digest_info() says size %zu kind %d, "
                "ph_compute_digest() returned size %d kind %d\n",
                ph_algorithm_name(algo), what, size, (int)kind, via_dispatch.size,
                via_dispatch.kind);
        exit(1);
    }
    for (size_t i = size; i < PH_DIGEST_MAX_BYTES; i++)
        ASSERT_UINT8_EQ(0, via_dispatch.data[i]);

    ph_digest_t direct;
    memset(&direct, 0xCD, sizeof(direct));
    uint64_t hash = 0;
    ASSERT_OK(compute_direct(ctx, algo, &direct, &hash));
    if (is_uint64_algorithm(algo)) {
        ASSERT_INT_EQ(8, (int)size);
        ASSERT_INT_EQ(PH_DIGEST_KIND_BITS, (int)kind);
        for (int i = 0; i < 8; i++)
            ASSERT_UINT8_EQ((uint8_t)(hash >> (56 - 8 * i)), via_dispatch.data[i]);
        /* The same bytes as the text ph_hash_to_hex() writes for it. */
        char from_hash[17], text[PH_DIGEST_HEX_BUFFER_SIZE];
        ASSERT_OK(ph_hash_to_hex(hash, from_hash, sizeof(from_hash)));
        ASSERT_OK(ph_digest_to_hex(&via_dispatch, text, sizeof(text)));
        ASSERT_STR_EQ(from_hash, strchr(text, ':') + 1);
    } else {
        ASSERT_INT_EQ(0, memcmp(&direct, &via_dispatch, sizeof(direct)));
    }
}

static void test_info_matches_every_computed_digest(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    for (int a = 0; a < PH_ALGORITHM_COUNT; a++)
        reconcile(ctx, (ph_algorithm_t)a, "defaults");

    /* BMH is the one algorithm whose size follows the configuration: every block size the
     * setter accepts, including the ones whose bit count is not a multiple of 8. */
    int bmh_sizes = 0;
    for (int bs = 1; bs <= 40; bs++) {
        if (ph_context_set_block_params(ctx, bs) != PH_SUCCESS)
            continue;
        char what[32];
        snprintf(what, sizeof(what), "block_size %d", bs);
        reconcile(ctx, PH_ALGO_BMH, what);
        size_t size = 0;
        ASSERT_OK(ph_digest_info(ctx, PH_ALGO_BMH, &size, NULL));
        ASSERT_INT_EQ((bs * bs + 7) / 8, (int)size);
        bmh_sizes++;
    }
    ASSERT(bmh_sizes >= 20); /* the setter's range, not a couple of lucky values */

    /* The other parameters move no size: the rest reconcile under a changed configuration
     * too. */
    ASSERT_OK(ph_context_set_block_params(ctx, 16));
    ASSERT_OK(ph_context_set_phash_params(ctx, 16, 6));
    ASSERT_OK(ph_context_set_whash_mode(ctx, PH_WHASH_FULL));
    for (int a = 0; a < PH_ALGORITHM_COUNT; a++)
        reconcile(ctx, (ph_algorithm_t)a, "configured");

    ph_free(ctx);
    PASS("test_info_matches_every_computed_digest");
}

/* No image needed, and NULL means the defaults. */
static void test_info_without_an_image(void) {
    ph_context_t *fresh = NULL;
    ASSERT_OK(ph_create(&fresh));
    for (int a = 0; a < PH_ALGORITHM_COUNT; a++) {
        size_t s_null = 0, s_ctx = 0;
        ph_digest_kind_t k_null = PH_DIGEST_KIND_UNSPECIFIED, k_ctx = PH_DIGEST_KIND_UNSPECIFIED;
        ASSERT_OK(ph_digest_info(NULL, (ph_algorithm_t)a, &s_null, &k_null));
        ASSERT_OK(ph_digest_info(fresh, (ph_algorithm_t)a, &s_ctx, &k_ctx));
        ASSERT_INT_EQ((int)s_ctx, (int)s_null);
        ASSERT_INT_EQ((int)k_ctx, (int)k_null);
        ASSERT(s_null > 0 && s_null <= PH_DIGEST_MAX_BYTES);
        ASSERT(k_null != PH_DIGEST_KIND_UNSPECIFIED);
        /* Either output may be skipped. */
        ASSERT_OK(ph_digest_info(NULL, (ph_algorithm_t)a, NULL, NULL));
    }

    /* Values that are not algorithms are refused and nothing is written. */
    const int bad[] = {-1, PH_ALGORITHM_COUNT, 100, (int)PH_ALGO_FORCE_INT32_};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        size_t size = 77;
        ph_digest_kind_t kind = PH_DIGEST_KIND_VECTOR;
        ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                      ph_digest_info(NULL, (ph_algorithm_t)bad[i], &size, &kind));
        ASSERT(size == 77 && kind == PH_DIGEST_KIND_VECTOR);
    }
    ph_free(fresh);
    PASS("test_info_without_an_image");
}

/* The dispatcher keeps each algorithm's own error contract. */
static void test_dispatch_errors(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ph_digest_t d;
    for (int a = 0; a < PH_ALGORITHM_COUNT; a++) {
        ASSERT_INT_EQ(PH_ERR_EMPTY_IMAGE, ph_compute_digest(ctx, (ph_algorithm_t)a, &d));
        ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_digest(ctx, (ph_algorithm_t)a, NULL));
        ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_digest(NULL, (ph_algorithm_t)a, &d));
    }
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_digest(ctx, (ph_algorithm_t)-1, &d));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                  ph_compute_digest(ctx, (ph_algorithm_t)PH_ALGORITHM_COUNT, &d));

    ASSERT_OK(ph_context_set_load_grayscale(ctx, 1));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.png"));
    ASSERT_INT_EQ(PH_ERR_REQUIRES_COLOR, ph_compute_digest(ctx, PH_ALGO_COLOR_HASH, &d));
    ASSERT_INT_EQ(PH_ERR_REQUIRES_COLOR, ph_compute_digest(ctx, PH_ALGO_COLOR_MOMENTS, &d));
    ASSERT_OK(ph_compute_digest(ctx, PH_ALGO_AHASH, &d));
    ph_free(ctx);
    PASS("test_dispatch_errors");
}

/* A uint64_t algorithm through the digest API compares exactly as through the uint64_t
 * API: Hamming distance on the 8-byte digests equals the one on the hashes. */
static void test_uint64_digests_compare_like_hashes(void) {
    ph_context_t *a = NULL, *b = NULL;
    ASSERT_OK(ph_create(&a));
    ASSERT_OK(ph_create(&b));
    ASSERT_OK(ph_load_from_file(a, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_OK(ph_load_from_file(b, TEST_DATA_DIR "/photo_rotated_90.jpeg"));
    for (int algo = PH_ALGO_AHASH; algo <= PH_ALGO_WHASH; algo++) {
        uint64_t ha = 0, hb = 0;
        ph_digest_t da, db;
        ASSERT_OK(compute_direct(a, (ph_algorithm_t)algo, NULL, &ha));
        ASSERT_OK(compute_direct(b, (ph_algorithm_t)algo, NULL, &hb));
        ASSERT_OK(ph_compute_digest(a, (ph_algorithm_t)algo, &da));
        ASSERT_OK(ph_compute_digest(b, (ph_algorithm_t)algo, &db));
        ASSERT_INT_EQ(ph_hamming_distance(ha, hb), ph_hamming_distance_digest(&da, &db));
    }
    ph_free(a);
    ph_free(b);
    PASS("test_uint64_digests_compare_like_hashes");
}

static void test_names(void) {
    static const char *const expected[PH_ALGORITHM_COUNT] = {
        "ahash", "dhash", "phash", "whash", "bmh", "mhash", "radial", "color_hash", "color_moments",
    };
    for (int a = 0; a < PH_ALGORITHM_COUNT; a++) {
        ASSERT_STR_EQ(expected[a], ph_algorithm_name((ph_algorithm_t)a));
        ph_algorithm_t back = PH_ALGO_FORCE_INT32_;
        ASSERT_OK(ph_algorithm_from_name(expected[a], &back));
        ASSERT_INT_EQ(a, (int)back);
    }
    ASSERT_STR_EQ("unknown", ph_algorithm_name((ph_algorithm_t)-1));
    ASSERT_STR_EQ("unknown", ph_algorithm_name((ph_algorithm_t)PH_ALGORITHM_COUNT));

    const char *bad[] = {"", "AHASH", "ahash ", "unknown", "color-hash", "colorhash"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        ph_algorithm_t out = PH_ALGO_RADIAL;
        ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_algorithm_from_name(bad[i], &out));
        ASSERT_INT_EQ(PH_ALGO_RADIAL, (int)out);
    }
    ph_algorithm_t out;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_algorithm_from_name(NULL, &out));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_algorithm_from_name("ahash", NULL));

    /* The flag bit of a uint64_t algorithm is 1 << its value. */
    ASSERT_INT_EQ(PH_HASH_AHASH, 1 << PH_ALGO_AHASH);
    ASSERT_INT_EQ(PH_HASH_DHASH, 1 << PH_ALGO_DHASH);
    ASSERT_INT_EQ(PH_HASH_PHASH, 1 << PH_ALGO_PHASH);
    ASSERT_INT_EQ(PH_HASH_WHASH, 1 << PH_ALGO_WHASH);
    PASS("test_names");
}

int main(void) {
    test_info_matches_every_computed_digest();
    test_info_without_an_image();
    test_dispatch_errors();
    test_uint64_digests_compare_like_hashes();
    test_names();
    printf("test_algorithms: PASSED\n");
    return 0;
}
