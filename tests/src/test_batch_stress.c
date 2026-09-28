/* Stress test for the batch thread pool.
 *
 * The defect class this file targets: ph_hash_files()/ph_hash_buffers() reporting the
 * batch as finished while worker threads are still writing into items[]. On Windows
 * this can happen two ways -- a NULL hole in the handle array making
 * WaitForMultipleObjects() return WAIT_FAILED at once, and more than
 * MAXIMUM_WAIT_OBJECTS (64) handles being passed in a single call. This test pins the
 * observable contract on every platform:
 *
 *   1. results are byte-for-byte identical to a sequential run, for thread counts
 *      below, equal to and far above both the item count and MAXIMUM_WAIT_OBJECTS;
 *   2. no item is left at its pre-set placeholder status when the call returns
 *      successfully (i.e. every item really was processed before we got control back);
 *   3. after the call returns, the items array can be freed immediately -- under ASan
 *      a straggler worker writing into the freed block is reported as a
 *      use-after-write, which is what a late worker write looks like to the caller.
 */

#include "libphash.h"
#include "test_macros.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Not an error code: set before a batch call so that a status the batch never wrote
 * stands out. */
#define STATUS_UNWRITTEN ((ph_error_t)0x5EED)

/* Prime, > 200, and coprime with every thread count used below, so work distribution
 * is uneven and the last chunk of items is always claimed by an arbitrary worker. */
#define STRESS_N 251

#define STRESS_FLAGS (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH)
/* ph_compute_multi() writes only one slot per set flag, densely from index 0; the tail of
 * hashes[] is left untouched, so only the first STRESS_NSET slots are comparable. */
#define STRESS_NSET  4

/* Mixed inputs on purpose: three decodable images of different formats/sizes, a
 * corrupted file and a NULL path. Per-item error statuses must come out of the
 * threaded path exactly as they do out of the sequential one. */
static const char *stress_path(size_t i) {
    switch (i % 7) {
        case 0:
            return TEST_DATA_DIR "/photo.png";
        case 1:
            return TEST_DATA_DIR "/photo.jpeg";
        case 2:
            return TEST_DATA_DIR "/photo.png";
        case 3:
            return TEST_DATA_DIR "/photo_copy.jpeg";
        case 4:
            return TEST_DATA_DIR "/photo.png";
        case 5:
            return TEST_DATA_DIR "/corrupted.jpg";
        default:
            return NULL;
    }
}

static const int g_thread_counts[] = {
    0, /* auto-detect: whatever the machine reports */
    1, /* forced sequential */
    2,
    4, /* fewer threads than items */
    64,
    65,            /* right at and just past MAXIMUM_WAIT_OBJECTS */
    128,           /* twice the Win32 wait limit */
    STRESS_N,      /* one thread per item */
    STRESS_N + 37, /* more threads than items */
};
#define THREAD_COUNTS_N (sizeof(g_thread_counts) / sizeof(g_thread_counts[0]))

static ph_batch_item_t *alloc_items(size_t n) {
    ph_batch_item_t *items = (ph_batch_item_t *)malloc(sizeof(ph_batch_item_t) * n);
    ASSERT_PTR_NOT_NULL(items);
    for (size_t i = 0; i < n; i++) {
        items[i].path = stress_path(i);
        items[i].status = STATUS_UNWRITTEN;
        memset(items[i].hashes, 0xAB, sizeof(items[i].hashes));
    }
    return items;
}

static void check_all_processed(const ph_batch_item_t *items, size_t n, int threads) {
    for (size_t i = 0; i < n; i++) {
        /* PH_ERR_ALLOCATION_FAILED is the placeholder ph_hash_files() pre-sets before
         * dispatching work. Seeing it after a PH_SUCCESS return means the call gave up
         * waiting on a worker that had not reached this item yet. */
        if (items[i].status == PH_ERR_ALLOCATION_FAILED) {
            fprintf(stderr,
                    "[FAIL] test_batch_stress: item %zu still at placeholder status after a "
                    "successful ph_hash_files(threads=%d) -- the batch returned before its "
                    "workers were done\n",
                    i, threads);
            exit(1);
        }
    }
}

/* Sequential reference, computed without the batch API at all. */
static void reference_run(ph_batch_item_t *items, size_t n) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    for (size_t i = 0; i < n; i++) {
        memset(items[i].hashes, 0, sizeof(items[i].hashes));
        if (!items[i].path) {
            items[i].status = PH_ERR_INVALID_ARGUMENT;
            continue;
        }
        ph_error_t err = ph_load_from_file(ctx, items[i].path);
        if (err != PH_SUCCESS) {
            items[i].status = err;
            continue;
        }
        items[i].status = ph_compute_multi(ctx, STRESS_FLAGS, items[i].hashes);
    }
    ph_free(ctx);
}

static void compare_or_die(const ph_batch_item_t *got, const ph_batch_item_t *want, size_t n,
                           int threads, const char *what) {
    for (size_t i = 0; i < n; i++) {
        if (got[i].status != want[i].status) {
            fprintf(stderr,
                    "[FAIL] test_batch_stress (%s, threads=%d): item %zu status %d, "
                    "sequential run gave %d\n",
                    what, threads, i, got[i].status, want[i].status);
            exit(1);
        }
        if (memcmp(got[i].hashes, want[i].hashes, STRESS_NSET * sizeof(uint64_t)) != 0) {
            fprintf(stderr,
                    "[FAIL] test_batch_stress (%s, threads=%d): item %zu hashes differ from the "
                    "sequential run\n",
                    what, threads, i);
            for (int k = 0; k < STRESS_NSET; k++) {
                fprintf(stderr, "  slot %d: got %llu want %llu\n", k,
                        (unsigned long long)got[i].hashes[k],
                        (unsigned long long)want[i].hashes[k]);
            }
            exit(1);
        }
    }
}

/* 251 items x 9 thread counts, every run compared byte-for-byte with the sequential
 * reference. Covers threads < n, threads == n, threads > n and threads > 64. */
static void test_stress_files_match_sequential(void) {
    ph_batch_item_t *reference = alloc_items(STRESS_N);
    reference_run(reference, STRESS_N);

    for (size_t t = 0; t < THREAD_COUNTS_N; t++) {
        const int threads = g_thread_counts[t];
        ph_batch_item_t *items = alloc_items(STRESS_N);

        ASSERT_OK(ph_hash_files(items, STRESS_N, STRESS_FLAGS, threads));

        check_all_processed(items, STRESS_N, threads);
        compare_or_die(items, reference, STRESS_N, threads, "files");
        free(items);
    }

    free(reference);
    PASS("test_stress_files_match_sequential");
}

/* n < nthreads: ph_resolve_thread_count() must clamp, and the extra threads must not
 * make the run diverge (or touch anything past items[n-1]). */
static void test_stress_more_threads_than_items(void) {
    const size_t small_counts[] = {1, 2, 3, 7};
    for (size_t s = 0; s < sizeof(small_counts) / sizeof(small_counts[0]); s++) {
        const size_t n = small_counts[s];
        ph_batch_item_t *reference = alloc_items(n);
        reference_run(reference, n);

        for (size_t t = 0; t < THREAD_COUNTS_N; t++) {
            const int threads = g_thread_counts[t];
            if (threads != 0 && (size_t)threads <= n) {
                continue; /* covered by the big run */
            }
            ph_batch_item_t *items = alloc_items(n);
            ASSERT_OK(ph_hash_files(items, n, STRESS_FLAGS, threads));
            check_all_processed(items, n, threads);
            compare_or_die(items, reference, n, threads, "n < nthreads");
            free(items);
        }
        free(reference);
    }
    PASS("test_stress_more_threads_than_items");
}

/* What a late worker write looks like to the caller: it owns items[] and may free it the
 * instant the call returns. Snapshot the results, release the block right away, then
 * churn the allocator so any late worker write lands in freed/reused memory -- ASan
 * turns that into a use-after-free report, plain builds usually into a wrong-value
 * mismatch in the snapshot compare below. */
static void test_stress_items_freeable_on_return(void) {
    ph_batch_item_t *reference = alloc_items(STRESS_N);
    reference_run(reference, STRESS_N);

    ph_batch_item_t *snapshot = (ph_batch_item_t *)malloc(sizeof(ph_batch_item_t) * STRESS_N);
    ASSERT_PTR_NOT_NULL(snapshot);

    for (int rep = 0; rep < 4; rep++) {
        ph_batch_item_t *items = alloc_items(STRESS_N);
        ASSERT_OK(ph_hash_files(items, STRESS_N, STRESS_FLAGS, 128));
        memcpy(snapshot, items, sizeof(ph_batch_item_t) * STRESS_N);
        free(items);

        /* Force the freed block to be handed out and overwritten again. */
        for (int k = 0; k < 8; k++) {
            void *churn = malloc(sizeof(ph_batch_item_t) * STRESS_N);
            ASSERT_PTR_NOT_NULL(churn);
            memset(churn, 0x5A, sizeof(ph_batch_item_t) * STRESS_N);
            free(churn);
        }

        check_all_processed(snapshot, STRESS_N, 128);
        compare_or_die(snapshot, reference, STRESS_N, 128, "freed right after return");
    }

    free(snapshot);
    free(reference);
    PASS("test_stress_items_freeable_on_return");
}

/* Same coverage for the buffer entry point: one shared read-only buffer per item so the
 * pool is exercised without 251 file opens. */
static uint8_t *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    ASSERT_PTR_NOT_NULL(f);
    ASSERT(fseek(f, 0, SEEK_END) == 0);
    long len = ftell(f);
    ASSERT(len > 0);
    ASSERT(fseek(f, 0, SEEK_SET) == 0);
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    ASSERT_PTR_NOT_NULL(buf);
    ASSERT(fread(buf, 1, (size_t)len, f) == (size_t)len);
    fclose(f);
    *len_out = (size_t)len;
    return buf;
}

static void test_stress_buffers_match_sequential(void) {
    size_t jpeg_len = 0, png_len = 0;
    uint8_t *jpeg = read_file(TEST_DATA_DIR "/photo.jpeg", &jpeg_len);
    uint8_t *png = read_file(TEST_DATA_DIR "/photo.png", &png_len);

    uint64_t want_jpeg[PH_HASH_FLAGS_COUNT] = {0};
    uint64_t want_png[PH_HASH_FLAGS_COUNT] = {0};
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_memory(ctx, jpeg, jpeg_len));
    ASSERT_OK(ph_compute_multi(ctx, STRESS_FLAGS, want_jpeg));
    ASSERT_OK(ph_load_from_memory(ctx, png, png_len));
    ASSERT_OK(ph_compute_multi(ctx, STRESS_FLAGS, want_png));
    ph_free(ctx);

    for (size_t t = 0; t < THREAD_COUNTS_N; t++) {
        const int threads = g_thread_counts[t];
        ph_batch_buffer_item_t *items =
            (ph_batch_buffer_item_t *)malloc(sizeof(ph_batch_buffer_item_t) * STRESS_N);
        ASSERT_PTR_NOT_NULL(items);
        for (size_t i = 0; i < STRESS_N; i++) {
            const int is_png = (i % 3) != 0;
            items[i].buffer = is_png ? png : jpeg;
            items[i].length = is_png ? png_len : jpeg_len;
            items[i].status = STATUS_UNWRITTEN;
            memset(items[i].hashes, 0xAB, sizeof(items[i].hashes));
        }

        ASSERT_OK(ph_hash_buffers(items, STRESS_N, STRESS_FLAGS, threads));

        for (size_t i = 0; i < STRESS_N; i++) {
            const uint64_t *want = ((i % 3) != 0) ? want_png : want_jpeg;
            if (items[i].status != PH_SUCCESS) {
                fprintf(stderr, "[FAIL] test_stress_buffers (threads=%d): item %zu status %d\n",
                        threads, i, items[i].status);
                exit(1);
            }
            if (memcmp(items[i].hashes, want, STRESS_NSET * sizeof(uint64_t)) != 0) {
                fprintf(stderr,
                        "[FAIL] test_stress_buffers (threads=%d): item %zu hashes differ from the "
                        "single-context reference\n",
                        threads, i);
                exit(1);
            }
        }
        free(items);
    }

    free(jpeg);
    free(png);
    PASS("test_stress_buffers_match_sequential");
}

/* Cancellation and progress under real concurrency -- the TSan leg builds this file, so
 * the callbacks' cross-thread use is checked for races too. should_continue says stop
 * after a fixed number of calls, from whichever worker gets there; afterwards:
 *
 *   - the call returns PH_ERR_CANCELLED (a batch this long cannot finish first),
 *   - the started items are a prefix and match the sequential reference,
 *   - every other item is PH_ERR_CANCELLED with zeroed hashes,
 *   - on_progress saw every finished item exactly once, numbered 1..finished. */
#define CANCEL_AFTER_CALLS 40

typedef struct {
    atomic_int calls;
    atomic_size_t progress_calls;
    atomic_uchar seen[STRESS_N + 1];
} cancel_state_t;

static int stop_after_some(void *user_data) {
    cancel_state_t *st = (cancel_state_t *)user_data;
    return atomic_fetch_add(&st->calls, 1) < CANCEL_AFTER_CALLS;
}

static void record_progress(size_t done, size_t total, void *user_data) {
    cancel_state_t *st = (cancel_state_t *)user_data;
    if (total != STRESS_N || done == 0 || done > STRESS_N ||
        atomic_exchange(&st->seen[done], 1) != 0) {
        fprintf(stderr, "[FAIL] on_progress(done=%zu, total=%zu) out of range or repeated\n", done,
                total);
        exit(1);
    }
    atomic_fetch_add(&st->progress_calls, 1);
}

static void test_stress_cancel_midway(void) {
    ph_batch_item_t *reference = alloc_items(STRESS_N);
    reference_run(reference, STRESS_N);

    const int thread_counts[] = {1, 4, 16};
    for (size_t t = 0; t < sizeof(thread_counts) / sizeof(*thread_counts); t++) {
        cancel_state_t *st = (cancel_state_t *)calloc(1, sizeof(*st));
        ASSERT_PTR_NOT_NULL(st);
        ph_batch_item_t *items = alloc_items(STRESS_N);

        ph_batch_options_t options;
        ASSERT_OK(ph_batch_options_init(&options));
        options.threads = thread_counts[t];
        options.should_continue = stop_after_some;
        options.on_progress = record_progress;
        options.user_data = st;
        ASSERT_INT_EQ(PH_ERR_CANCELLED, ph_hash_files_ex(items, STRESS_N, STRESS_FLAGS, &options));

        size_t started = 0;
        while (started < STRESS_N && items[started].status != PH_ERR_CANCELLED) {
            started++;
        }
        ASSERT(started > 0 && started < STRESS_N);
        compare_or_die(items, reference, started, thread_counts[t], "cancelled prefix");
        for (size_t i = started; i < STRESS_N; i++) {
            ASSERT_INT_EQ(PH_ERR_CANCELLED, items[i].status);
            for (int k = 0; k < PH_BATCH_HASHES_CAPACITY; k++) {
                ASSERT(items[i].hashes[k] == 0);
            }
        }
        ASSERT_INT_EQ((int)started, (int)atomic_load(&st->progress_calls));
        for (size_t d = 1; d <= started; d++) {
            ASSERT_INT_EQ(1, (int)atomic_load(&st->seen[d]));
        }

        free(items);
        free(st);
    }

    free(reference);
    PASS("test_stress_cancel_midway");
}

int main(void) {
    test_stress_files_match_sequential();
    test_stress_more_threads_than_items();
    test_stress_items_freeable_on_return();
    test_stress_buffers_match_sequential();
    test_stress_cancel_midway();
    return 0;
}
