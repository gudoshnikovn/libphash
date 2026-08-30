#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <string.h>

static uint64_t reference_hash(const char *path, uint32_t flag) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, path));

    uint64_t h = 0;
    ph_error_t err = PH_ERR_INVALID_ARGUMENT;
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
        default:
            fprintf(stderr, "reference_hash: unexpected flag %u\n", flag);
            exit(1);
    }
    ASSERT_OK(err);
    ph_free(ctx);
    return h;
}

/* ph_hash_files must match individual ph_compute_* calls bit-for-bit, regardless of
 * how many worker threads are requested (0=auto, 1=sequential, or an explicit count). */
static void test_hash_files_matches_individual_calls() {
    const char *paths[] = {
        TEST_DATA_DIR "/photo.jpeg",
        TEST_DATA_DIR "/photo_complex.png",
        TEST_DATA_DIR "/photo_copy.jpeg",
        TEST_DATA_DIR "/photo_rotated_90.jpeg",
    };
    const size_t n = sizeof(paths) / sizeof(paths[0]);
    int thread_counts[] = {1, 0, 4};

    for (size_t tc = 0; tc < sizeof(thread_counts) / sizeof(thread_counts[0]); tc++) {
        ph_batch_item_t items[4];
        for (size_t i = 0; i < n; i++) {
            items[i].path = paths[i];
            items[i].hash = 0;
            items[i].status = PH_ERR_NOT_IMPLEMENTED;
        }

        ASSERT_OK(ph_hash_files(items, n, PH_HASH_PHASH, thread_counts[tc]));

        for (size_t i = 0; i < n; i++) {
            ASSERT_INT_EQ(PH_SUCCESS, items[i].status);
            uint64_t expected = reference_hash(paths[i], PH_HASH_PHASH);
            if (items[i].hash != expected) {
                fprintf(stderr,
                        "[FAIL] test_hash_files_matches_individual_calls: threads=%d path=%s "
                        "expected=%llu got=%llu\n",
                        thread_counts[tc], paths[i], (unsigned long long)expected,
                        (unsigned long long)items[i].hash);
                exit(1);
            }
        }
    }

    PASS("test_hash_files_matches_individual_calls");
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

    uint64_t expected0 = reference_hash(TEST_DATA_DIR "/photo.jpeg", PH_HASH_DHASH);
    uint64_t expected2 = reference_hash(TEST_DATA_DIR "/photo_complex.png", PH_HASH_DHASH);
    ASSERT_UINT64_EQ(expected0, items[0].hash);
    ASSERT_UINT64_EQ(expected2, items[2].hash);

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

    ph_batch_buffer_item_t items[1] = {
        {.buffer = buf, .length = (size_t)size},
    };

    ASSERT_OK(ph_hash_buffers(items, 1, PH_HASH_AHASH, 0));
    ASSERT_INT_EQ(PH_SUCCESS, items[0].status);

    uint64_t expected = reference_hash(path, PH_HASH_AHASH);
    ASSERT_UINT64_EQ(expected, items[0].hash);

    free(buf);
    PASS("test_hash_buffers_matches_files");
}

static void test_batch_invalid_args() {
    ph_batch_item_t items[1] = {{.path = TEST_DATA_DIR "/photo.jpeg"}};

    /* n == 0 is a no-op success regardless of items. */
    ASSERT_OK(ph_hash_files(NULL, 0, PH_HASH_AHASH, 0));

    /* NULL items with n > 0. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_files(NULL, 1, PH_HASH_AHASH, 0));

    /* flags must be exactly one bit. */
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                  ph_hash_files(items, 1, PH_HASH_AHASH | PH_HASH_DHASH, 0));
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

int main() {
    test_hash_files_matches_individual_calls();
    test_hash_files_partial_failure();
    test_hash_buffers_matches_files();
    test_batch_invalid_args();
    return 0;
}
