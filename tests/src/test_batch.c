#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <string.h>

/* Every combination of the 6 ph_hash_flags_t bits, including the empty and full sets. */
#define ALL_FLAGS_MASK                                                                            \
    (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH | PH_HASH_MHASH |               \
     PH_HASH_COLOR_HASH)

static void reference_multi(const char *path, uint32_t flags, uint64_t out[PH_HASH_FLAGS_COUNT]) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, path));
    ASSERT_OK(ph_compute_multi(ctx, flags, out));
    ph_free(ctx);
}

/* ph_hash_files must match ph_compute_multi() bit-for-bit for every non-empty flag
 * combination, regardless of how many worker threads are requested. */
static void test_hash_files_matches_compute_multi() {
    const char *paths[] = {
        TEST_DATA_DIR "/photo.jpeg",
        TEST_DATA_DIR "/photo_complex.png",
        TEST_DATA_DIR "/photo_copy.jpeg",
        TEST_DATA_DIR "/photo_rotated_90.jpeg",
    };
    const size_t n = sizeof(paths) / sizeof(paths[0]);
    int thread_counts[] = {1, 0, 4};

    for (uint32_t flags = 1; flags <= ALL_FLAGS_MASK; flags++) {
        if (flags & ~(uint32_t)ALL_FLAGS_MASK)
            continue;
        int nset = __builtin_popcount(flags);

        for (size_t tc = 0; tc < sizeof(thread_counts) / sizeof(thread_counts[0]); tc++) {
            ph_batch_item_t items[4];
            for (size_t i = 0; i < n; i++) {
                items[i].path = paths[i];
                items[i].status = PH_ERR_NOT_IMPLEMENTED;
            }

            ASSERT_OK(ph_hash_files(items, n, flags, thread_counts[tc]));

            for (size_t i = 0; i < n; i++) {
                ASSERT_INT_EQ(PH_SUCCESS, items[i].status);
                uint64_t expected[PH_HASH_FLAGS_COUNT] = {0};
                reference_multi(paths[i], flags, expected);
                for (int k = 0; k < nset; k++) {
                    if (items[i].hashes[k] != expected[k]) {
                        fprintf(stderr,
                                "[FAIL] test_hash_files_matches_compute_multi: flags=0x%x "
                                "threads=%d path=%s slot=%d expected=%llu got=%llu\n",
                                flags, thread_counts[tc], paths[i], k,
                                (unsigned long long)expected[k],
                                (unsigned long long)items[i].hashes[k]);
                        exit(1);
                    }
                }
            }
        }
    }

    PASS("test_hash_files_matches_compute_multi");
}

/* A bad item (missing file) records its own error in `status` and must not stop the
 * rest of the batch from being hashed successfully. */
static void test_hash_files_partial_failure() {
    ph_batch_item_t items[3] = {
        {.path = TEST_DATA_DIR "/photo.jpeg"},
        {.path = TEST_DATA_DIR "/does_not_exist.jpeg"},
        {.path = TEST_DATA_DIR "/photo_complex.png"},
    };

    ASSERT_OK(ph_hash_files(items, 3, PH_HASH_DHASH, 0));

    ASSERT_INT_EQ(PH_SUCCESS, items[0].status);
    ASSERT(items[1].status != PH_SUCCESS);
    ASSERT_INT_EQ(PH_SUCCESS, items[2].status);

    uint64_t expected0[PH_HASH_FLAGS_COUNT] = {0};
    uint64_t expected2[PH_HASH_FLAGS_COUNT] = {0};
    reference_multi(TEST_DATA_DIR "/photo.jpeg", PH_HASH_DHASH, expected0);
    reference_multi(TEST_DATA_DIR "/photo_complex.png", PH_HASH_DHASH, expected2);
    ASSERT_UINT64_EQ(expected0[0], items[0].hashes[0]);
    ASSERT_UINT64_EQ(expected2[0], items[2].hashes[0]);

    PASS("test_hash_files_partial_failure");
}

static void test_hash_buffers_matches_files() {
    const char *path = TEST_DATA_DIR "/photo.jpeg";
    FILE *f = fopen(path, "rb");
    ASSERT_PTR_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ASSERT(size > 0);

    uint8_t *buf = malloc((size_t)size);
    ASSERT_PTR_NOT_NULL(buf);
    ASSERT_INT_EQ((int)size, (int)fread(buf, 1, (size_t)size, f));
    fclose(f);

    uint32_t flags = PH_HASH_AHASH | PH_HASH_PHASH;
    ph_batch_buffer_item_t items[1] = {
        {.buffer = buf, .length = (size_t)size},
    };

    ASSERT_OK(ph_hash_buffers(items, 1, flags, 0));
    ASSERT_INT_EQ(PH_SUCCESS, items[0].status);

    uint64_t expected[PH_HASH_FLAGS_COUNT] = {0};
    reference_multi(path, flags, expected);
    ASSERT_UINT64_EQ(expected[0], items[0].hashes[0]);
    ASSERT_UINT64_EQ(expected[1], items[0].hashes[1]);

    free(buf);
    PASS("test_hash_buffers_matches_files");
}

static void test_batch_invalid_args() {
    ph_batch_item_t items[1] = {{.path = TEST_DATA_DIR "/photo.jpeg"}};

    /* n == 0 is a no-op success regardless of items. */
    ASSERT_OK(ph_hash_files(NULL, 0, PH_HASH_AHASH, 0));

    /* NULL items with n > 0. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_files(NULL, 1, PH_HASH_AHASH, 0));

    /* Multiple flags are allowed; only an empty or unknown mask is rejected. */
    ASSERT_OK(ph_hash_files(items, 1, PH_HASH_AHASH | PH_HASH_DHASH, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_files(items, 1, 0, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_files(items, 1, 1u << 31, 0));

    /* Negative thread count. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_files(items, 1, PH_HASH_AHASH, -1));

    /* Item with a NULL path is reported per-item, not as a hard failure. */
    ph_batch_item_t null_item[1] = {{.path = NULL}};
    ASSERT_OK(ph_hash_files(null_item, 1, PH_HASH_AHASH, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, null_item[0].status);

    PASS("test_batch_invalid_args");
}

/* Argument validation must run *before* the `n == 0` shortcut: an empty batch is not a
 * licence to swallow a malformed call. `flags` and `threads` are validated unconditionally;
 * `items == NULL` is only an error when there is something to dereference (`n > 0`). */
static void test_batch_validation_precedes_empty_shortcut() {
    ph_batch_item_t items[1] = {{.path = TEST_DATA_DIR "/photo.jpeg"}};
    ph_batch_buffer_item_t bitems[1] = {{.buffer = (const uint8_t *)"x", .length = 1}};

    /* The exact call from the review: every argument is junk, n == 0. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_files(NULL, 0, 0, -5));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_buffers(NULL, 0, 0, -5));

    /* Negative thread count is rejected for any n, including 0. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_files(items, 0, PH_HASH_AHASH, -1));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_files(NULL, 0, PH_HASH_AHASH, -1));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_buffers(bitems, 0, PH_HASH_AHASH, -1));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_buffers(NULL, 0, PH_HASH_AHASH, -1));

    /* Empty and unknown-bit flag masks are rejected for any n, including 0. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_files(items, 0, 0, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_files(items, 0, 1u << 31, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_files(items, 0, PH_HASH_AHASH | (1u << 6), 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_buffers(bitems, 0, 0, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_buffers(bitems, 0, 1u << 31, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                  ph_hash_buffers(bitems, 0, PH_HASH_AHASH | (1u << 6), 0));

    /* n == 0 with valid arguments stays a no-op success, with or without an array. */
    ASSERT_OK(ph_hash_files(items, 0, PH_HASH_AHASH, 0));
    ASSERT_OK(ph_hash_files(NULL, 0, PH_HASH_AHASH, 0));
    ASSERT_OK(ph_hash_files(NULL, 0, ALL_FLAGS_MASK, 4));
    ASSERT_OK(ph_hash_buffers(bitems, 0, PH_HASH_AHASH, 0));
    ASSERT_OK(ph_hash_buffers(NULL, 0, PH_HASH_AHASH, 0));
    ASSERT_OK(ph_hash_buffers(NULL, 0, ALL_FLAGS_MASK, 4));

    /* A rejected call must not have touched the array. */
    ASSERT_PTR_NOT_NULL((void *)items[0].path);

    PASS("test_batch_validation_precedes_empty_shortcut");
}

/* ph_hash_buffers() must reject the same malformed calls as ph_hash_files() with n > 0. */
static void test_hash_buffers_invalid_args() {
    ph_batch_buffer_item_t items[1] = {{.buffer = (const uint8_t *)"x", .length = 1}};

    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_buffers(NULL, 1, PH_HASH_AHASH, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_buffers(items, 1, 0, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_buffers(items, 1, 1u << 31, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_buffers(items, 1, PH_HASH_AHASH, -1));

    /* An item with a NULL/empty buffer is a per-item failure, not a hard one. */
    ph_batch_buffer_item_t bad[2] = {
        {.buffer = NULL, .length = 4},
        {.buffer = (const uint8_t *)"garbage", .length = 0},
    };
    ASSERT_OK(ph_hash_buffers(bad, 2, PH_HASH_AHASH, 0));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, bad[0].status);
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, bad[1].status);

    PASS("test_hash_buffers_invalid_args");
}

int main() {
    test_hash_files_matches_compute_multi();
    test_hash_files_partial_failure();
    test_hash_buffers_matches_files();
    test_batch_invalid_args();
    test_batch_validation_precedes_empty_shortcut();
    test_hash_buffers_invalid_args();
    return 0;
}
