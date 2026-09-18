#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_bmh_logic() {
    ph_context_t *ctx1 = NULL;
    ph_context_t *ctx2 = NULL;

    // Allocate digests on stack (No malloc/free needed!)
    ph_digest_t digest1;
    ph_digest_t digest2;

    ASSERT_OK(ph_create(&ctx1));
    ASSERT_OK(ph_create(&ctx2));

    ASSERT_OK(ph_load_from_file(ctx1, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_OK(ph_load_from_file(ctx2, TEST_DATA_DIR "/photo_copy.jpeg"));

    ASSERT_OK(ph_compute_bmh(ctx1, &digest1));
    ASSERT_OK(ph_compute_bmh(ctx2, &digest2));

    int dist = ph_hamming_distance_digest(&digest1, &digest2);
    printf("[BMH] Distance: %d bits\n", dist);

    if (dist > 20) {
        fprintf(stderr, "BMH distance too high: %d\n", dist);
        exit(1);
    }

    // No ph_digest_free calls needed
    ph_free(ctx1);
    ph_free(ctx2);

    printf("test_bmh_logic: PASSED\n");
}

/* The digest width is a function of block_size and nothing else: one bit per block,
 * rounded up to a byte. It is what a caller has to store, so it is part of the contract
 * and not an implementation detail. */
static void test_bmh_digest_width_follows_block_size() {
    const int sizes[] = {2,  8, 16, 22,
                         31, 32}; /* 2 is the setter's lower bound,
                                      32 is its upper bound */

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        int n = sizes[i];
        ph_digest_t d;
        memset(&d, 0xAA, sizeof(d)); /* must be fully overwritten, not merged into */

        ASSERT_OK(ph_context_set_block_params(ctx, n));
        ASSERT_OK(ph_compute_bmh(ctx, &d));

        ASSERT_INT_EQ((n * n + 7) / 8, d.size);
        ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_BITS, d.kind);

        /* Everything past `size`, including the padding, must be zero -- a caller that
         * hashes or serialises sizeof(ph_digest_t) bytes would otherwise pick up
         * whatever the struct happened to be sitting on. */
        for (int b = d.size; b < PH_DIGEST_MAX_BYTES; b++)
            ASSERT_UINT8_EQ(0, d.data[b]);
        for (size_t r = 0; r < sizeof(d.reserved); r++)
            ASSERT_UINT8_EQ(0, d.reserved[r]);
    }

    /* 32x32 = 1024 bits is exactly a full digest; that is why the bound is 32. */
    ASSERT_OK(ph_context_set_block_params(ctx, 32));
    ph_digest_t full;
    ASSERT_OK(ph_compute_bmh(ctx, &full));
    ASSERT_INT_EQ(PH_DIGEST_MAX_BYTES, full.size);

    ph_free(ctx);
    PASS("test_bmh_digest_width_follows_block_size");
}

/* The setter is what keeps bmh.c's clipping branch unreachable, so its bound is the
 * thing worth pinning -- and a rejected value must leave the previous one in place,
 * which is observable here as the digest width not moving. */
static void test_bmh_block_size_bounds() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    ASSERT_OK(ph_context_set_block_params(ctx, 16));
    ph_digest_t before;
    ASSERT_OK(ph_compute_bmh(ctx, &before));

    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_block_params(ctx, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_block_params(ctx, -1));
    /* 1 used to be accepted but collapsed every image to the same digest (0x01). */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_block_params(ctx, 1));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_block_params(ctx, 33));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_block_params(ctx, 46341));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_context_set_block_params(NULL, 16));

    ph_digest_t after;
    ASSERT_OK(ph_compute_bmh(ctx, &after));
    ASSERT_INT_EQ(before.size, after.size);
    ASSERT_INT_EQ(0, memcmp(before.data, after.data, before.size));

    ph_free(ctx);
    PASS("test_bmh_block_size_bounds");
}

/* bmh.c's defence-in-depth branch: a block_size that did not come through the setter and
 * asks for more bits than a ph_digest_t holds. The size is clipped to the capacity, and
 * the hash is still computed over the full grid -- so the digest covers only the first
 * PH_DIGEST_MAX_BYTES*8 blocks. That is a silently partial result, which is why the
 * setter bound exists; this test documents the branch rather than endorsing it.
 *
 * Also the guard on the other side: a non-positive block_size is an error, not a
 * zero-length digest. */
static void test_bmh_out_of_range_block_size_direct() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    ph_digest_t d;
    memset(&d, 0xAA, sizeof(d));

    ctx->config.block_size = 40; /* 1600 bits = 200 bytes, past the 128-byte capacity */
    ASSERT_OK(ph_compute_bmh(ctx, &d));
    ASSERT_INT_EQ(PH_DIGEST_MAX_BYTES, d.size);
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_BITS, d.kind);

    /* One block_size past the setter's bound: still clipped, since 33*33 = 1089 bits
     * needs 137 bytes. */
    ctx->config.block_size = 33;
    ASSERT_OK(ph_compute_bmh(ctx, &d));
    ASSERT_INT_EQ(PH_DIGEST_MAX_BYTES, d.size);

    ctx->config.block_size = 0;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_bmh(ctx, &d));
    ctx->config.block_size = -4;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_bmh(ctx, &d));

    ph_free(ctx);
    PASS("test_bmh_out_of_range_block_size_direct");
}

static void test_bmh_invalid_args() {
    ph_context_t *ctx = NULL;
    ph_digest_t d;
    ASSERT_OK(ph_create(&ctx));

    /* No image loaded yet. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_bmh(ctx, &d));

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_bmh(NULL, &d));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_compute_bmh(ctx, NULL));

    ph_free(ctx);
    PASS("test_bmh_invalid_args");
}

/* The digest the algorithm's source relies on: thresholding at the median gives as many
 * ones as zeroes, whatever the image. Ties on flat images are the one thing that can tip
 * it, so this is checked on photographs, where equal block means are rare. */
static void test_bmh_bits_are_balanced() {
    const char *paths[] = {
        TEST_DATA_DIR "/photo.jpeg",
        TEST_DATA_DIR "/photo_complex.png",
        TEST_DATA_DIR "/photo_rotated_90.jpeg",
    };

    for (size_t p = 0; p < sizeof(paths) / sizeof(paths[0]); p++) {
        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_load_from_file(ctx, paths[p]));
        ASSERT_OK(ph_context_set_block_params(ctx, 16));

        ph_digest_t d;
        ASSERT_OK(ph_compute_bmh(ctx, &d));

        int ones = 0;
        for (int i = 0; i < d.size; i++)
            ones += __builtin_popcount(d.data[i]);

        int total = d.size * 8;
        if (ones * 2 < total - total / 8 || ones * 2 > total + total / 8) {
            fprintf(stderr, "[FAIL] test_bmh_bits_are_balanced: %s has %d ones of %d bits\n",
                    paths[p], ones, total);
            exit(1);
        }
        ph_free(ctx);
    }

    PASS("test_bmh_bits_are_balanced");
}

int main() {
    test_bmh_logic();
    test_bmh_digest_width_follows_block_size();
    test_bmh_block_size_bounds();
    test_bmh_out_of_range_block_size_direct();
    test_bmh_invalid_args();
    test_bmh_bits_are_balanced();
    return 0;
}
