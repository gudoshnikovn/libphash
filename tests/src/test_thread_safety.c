/* R35: the header documents a "one context per thread" contract (see the @note on
 * ph_get_last_error_message() and the file-level comment in include/libphash.h) --
 * every function is thread-safe as long as distinct threads operate on distinct
 * ph_context_t instances. Nothing exercised that claim directly: the batch API
 * (tests/src/test_batch.c, test_batch_stress.c) spawns real threads, but they always
 * share one internal pool and each worker still gets its own ph_context_t from
 * ph_create() inside ph_batch_worker_run() -- so "many contexts, many OS threads,
 * driven by application code rather than the library's own pool" had no test at all.
 *
 * This file is that test: N threads, each creating and owning its own context,
 * running concurrently (synchronized to start together, not just interleaved by
 * scheduling luck), each computing the same set of algorithms on the same fixtures a
 * single-threaded reference already computed. Any global or file-scope mutable state
 * accidentally shared between contexts -- the bug class this guards against -- would
 * show up either as a wrong result here or, under the `tsan` CI job (see R25), as a
 * reported data race even if the result happened to still be right.
 *
 * Two contexts sharing state from two threads at once is deliberately not tested:
 * the header's documented contract is the one-context-per-thread rule above, not a
 * shared-context guarantee, and a shared-context test would either have to be a
 * (fragile, meaningless) test for the absence of a race the contract never promised
 * to prevent, or a deliberate race that a TSan-covered CI job would always flag --
 * neither is useful to have as a standing test. If ph_context_t is ever redesigned to
 * make a single instance shareable across threads, the contract statement in
 * include/libphash.h is the place to change, and a real test belongs here.
 */
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PH_ENABLE_THREADS
#include <stdatomic.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

#define NUM_THREADS 8
#define ITERATIONS_PER_THREAD 5

static const char *FIXTURES[] = {
    TEST_DATA_DIR "/photo.jpeg",
    TEST_DATA_DIR "/photo_copy.jpeg",
    TEST_DATA_DIR "/photo_rotated_90.jpeg",
    TEST_DATA_DIR "/photo.png",
    TEST_DATA_DIR "/photo_complex.png",
};
#define NUM_FIXTURES (sizeof(FIXTURES) / sizeof(FIXTURES[0]))

#define ALL_FLAGS_MASK (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH)

typedef struct {
    uint64_t hashes[PH_HASH_FLAGS_COUNT];
    ph_digest_t bmh;
    ph_digest_t mhash;
} fixture_result_t;

static fixture_result_t g_reference[NUM_FIXTURES];

/* Everything one context computes for one fixture, in a fixed order so two calls on
 * two different contexts are directly comparable. */
static void compute_all(ph_context_t *ctx, const char *path, fixture_result_t *out) {
    ASSERT_OK(ph_load_from_file(ctx, path));
    ASSERT_OK(ph_compute_multi(ctx, ALL_FLAGS_MASK, out->hashes));
    ASSERT_OK(ph_compute_bmh(ctx, &out->bmh));
    ASSERT_OK(ph_compute_mhash(ctx, &out->mhash));
}

static void compute_reference(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    for (size_t i = 0; i < NUM_FIXTURES; i++)
        compute_all(ctx, FIXTURES[i], &g_reference[i]);
    ph_free(ctx);
}

static void check_matches_reference(const char *what, const fixture_result_t *got, size_t i) {
    const fixture_result_t *want = &g_reference[i];
    if (memcmp(got->hashes, want->hashes, sizeof(got->hashes)) != 0) {
        fprintf(stderr, "[FAIL] test_thread_safety (%s): fixture %zu uint64 hashes differ\n", what,
                i);
        exit(1);
    }
    if (ph_hamming_distance_digest(&got->bmh, &want->bmh) != 0) {
        fprintf(stderr, "[FAIL] test_thread_safety (%s): fixture %zu BMH differs\n", what, i);
        exit(1);
    }
    if (ph_hamming_distance_digest(&got->mhash, &want->mhash) != 0) {
        fprintf(stderr, "[FAIL] test_thread_safety (%s): fixture %zu mHash differs\n", what, i);
        exit(1);
    }
}

/* Every thread spins on this until every other thread has also arrived, so the
 * decode/hash work below actually overlaps in wall-clock time instead of just being
 * interleaved by whatever the OS scheduler happened to do -- a portable substitute for
 * pthread_barrier_t, which Win32 threads do not have. */
static atomic_int g_ready;
static atomic_int g_go;

static void wait_at_barrier(void) {
    atomic_fetch_add(&g_ready, 1);
    while (atomic_load(&g_ready) < NUM_THREADS) {
        /* spin */
    }
    while (!atomic_load(&g_go)) {
        /* spin */
    }
}

typedef struct {
    int thread_index;
    int failed;
} worker_arg_t;

static void worker_body(worker_arg_t *arg) {
    wait_at_barrier();

    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        arg->failed = 1;
        return;
    }

    char what[64];
    snprintf(what, sizeof(what), "thread %d", arg->thread_index);

    for (int rep = 0; rep < ITERATIONS_PER_THREAD; rep++) {
        for (size_t i = 0; i < NUM_FIXTURES; i++) {
            fixture_result_t got;
            compute_all(ctx, FIXTURES[i], &got);
            check_matches_reference(what, &got, i);
        }
    }

    ph_free(ctx);
}

#if defined(_WIN32)
static DWORD WINAPI worker_win(LPVOID p) {
    worker_body((worker_arg_t *)p);
    return 0;
}
#else
static void *worker_pthread(void *p) {
    worker_body((worker_arg_t *)p);
    return NULL;
}
#endif

static void run_once(void) {
    atomic_store(&g_ready, 0);
    atomic_store(&g_go, 0);

    worker_arg_t args[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_index = i;
        args[i].failed = 0;
    }

#if defined(_WIN32)
    HANDLE handles[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        handles[i] = CreateThread(NULL, 0, worker_win, &args[i], 0, NULL);
        ASSERT_PTR_NOT_NULL(handles[i]);
    }
    /* All threads created; release the barrier once every one of them has checked in
     * (wait_at_barrier() itself blocks each thread until g_ready reaches NUM_THREADS). */
    atomic_store(&g_go, 1);
    WaitForMultipleObjects(NUM_THREADS, handles, TRUE, INFINITE);
    for (int i = 0; i < NUM_THREADS; i++)
        CloseHandle(handles[i]);
#else
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
        ASSERT_INT_EQ(0, pthread_create(&threads[i], NULL, worker_pthread, &args[i]));
    atomic_store(&g_go, 1);
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);
#endif

    for (int i = 0; i < NUM_THREADS; i++) {
        if (args[i].failed) {
            fprintf(stderr, "[FAIL] test_thread_safety: thread %d could not ph_create()\n", i);
            exit(1);
        }
    }
}

/* Run the whole thing three times back to back: a race that only shows up
 * occasionally would still be expected to show up at least once across three runs,
 * and TSan (see .github/workflows/ci.yml, tsan job) is watching every one of them. */
static void test_many_contexts_many_threads(void) {
    compute_reference();
    for (int rep = 0; rep < 3; rep++)
        run_once();
    PASS("test_many_contexts_many_threads");
}

int main(void) {
    test_many_contexts_many_threads();
    return 0;
}

#else /* !PH_ENABLE_THREADS */

int main(void) {
    printf("test_thread_safety: SKIPPED (built without PH_ENABLE_THREADS)\n");
    return 0;
}

#endif
