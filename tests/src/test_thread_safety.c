/* The header documents a "one context per thread" contract (see the @note on
 * ph_get_last_error_message() and the file-level comment in include/libphash.h) --
 * every function is thread-safe as long as distinct threads operate on distinct
 * ph_context_t instances. The batch API's workers come from the library's own pool
 * (tests/src/test_batch.c, test_batch_stress.c); this file tests application-driven
 * threads, each owning its own context: N threads, each creating and owning its own context,
 * running concurrently (synchronized to start together, not just interleaved by
 * scheduling luck), each computing all nine algorithms on every fixture format. Any
 * global or file-scope mutable state accidentally shared between contexts -- the bug
 * class this guards against -- would show up either as a wrong result here or, under
 * the `tsan` CI job, as a reported data race even if the result happened to still be
 * right.
 *
 * Nothing is decoded or hashed before the first threads start. One-time lazy
 * initialisation -- the library's own tables or a decoder's CPU dispatch -- happens on
 * first use, and a sequential warm-up would run it before any thread exists and hide a
 * race in it. The threads therefore check each other, and the single-threaded reference
 * is computed after them.
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
#    include <stdatomic.h>
#    if defined(_WIN32)
#        include <windows.h>
#    else
#        include <pthread.h>
#    endif

#    define NUM_THREADS           8
#    define ITERATIONS_PER_THREAD 5

/* PNG first: right after the barrier every thread is in the same decoder at once, which
 * is where a race in lazily initialised decoder state shows. */
static const char *FIXTURES[] = {
    TEST_DATA_DIR "/photo.png",
    TEST_DATA_DIR "/photo.webp",
    TEST_DATA_DIR "/photo.jpeg",
    TEST_DATA_DIR "/photo_complex.png",
    TEST_DATA_DIR "/photo_copy.jpeg",
    TEST_DATA_DIR "/photo_complex.webp",
    TEST_DATA_DIR "/photo_rotated_90.jpeg",
};
#    define NUM_FIXTURES (sizeof(FIXTURES) / sizeof(FIXTURES[0]))

#    define ALL_FLAGS_MASK (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH)

enum {
    D_BMH,
    D_MHASH,
    D_RADIAL,
    D_COLOR,
    D_MOMENTS,
    NUM_DIGESTS,
};

static const char *const DIGEST_NAMES[NUM_DIGESTS] = {"BMH", "mHash", "Radial", "ColorHash",
                                                      "ColorMoments"};

typedef struct {
    ph_error_t load;
    uint64_t hashes[PH_HASH_FLAGS_COUNT];
    ph_digest_t digests[NUM_DIGESTS];
} fixture_result_t;

/* Each thread's results from its first pass, then the sequential reference. */
static fixture_result_t g_first[NUM_THREADS][NUM_FIXTURES];
static fixture_result_t g_reference[NUM_FIXTURES];

/* A WebP fixture is undecodable in a build without the WebP backend; everything else
 * decodes everywhere. */
static ph_error_t expected_load(const char *path) {
    size_t n = strlen(path);
    if (n > 5 && strcmp(path + n - 5, ".webp") == 0 && !ph_can_use_webp()) {
        return PH_ERR_DECODER_UNAVAILABLE;
    }
    return PH_SUCCESS;
}

/* Everything one context computes for one fixture, in a fixed order so two calls on
 * two different contexts are directly comparable. */
static void compute_all(ph_context_t *ctx, const char *path, fixture_result_t *out) {
    memset(out, 0, sizeof(*out));
    out->load = ph_load_from_file(ctx, path);
    ASSERT_MSG(out->load == expected_load(path), "%s: load returned %d, expected %d", path,
               out->load, expected_load(path));
    if (out->load != PH_SUCCESS) {
        return;
    }
    ASSERT_OK(ph_compute_multi(ctx, ALL_FLAGS_MASK, out->hashes));
    ASSERT_OK(ph_compute_bmh(ctx, &out->digests[D_BMH]));
    ASSERT_OK(ph_compute_mhash(ctx, &out->digests[D_MHASH]));
    ASSERT_OK(ph_compute_radial_hash(ctx, &out->digests[D_RADIAL]));
    ASSERT_OK(ph_compute_color_hash(ctx, &out->digests[D_COLOR]));
    ASSERT_OK(ph_compute_color_moments_hash(ctx, &out->digests[D_MOMENTS]));
}

static int digest_equal(const ph_digest_t *a, const ph_digest_t *b) {
    return a->size == b->size && a->kind == b->kind && memcmp(a->data, b->data, a->size) == 0;
}

static void check_same(const char *what, const fixture_result_t *got, const fixture_result_t *want,
                       size_t i) {
    ASSERT_MSG(got->load == want->load, "%s: %s: load status %d, expected %d", what, FIXTURES[i],
               got->load, want->load);
    ASSERT_MSG(memcmp(got->hashes, want->hashes, sizeof(got->hashes)) == 0,
               "%s: %s: uint64 hashes differ", what, FIXTURES[i]);
    for (int d = 0; d < NUM_DIGESTS; d++) {
        ASSERT_MSG(digest_equal(&got->digests[d], &want->digests[d]), "%s: %s: %s differs", what,
                   FIXTURES[i], DIGEST_NAMES[d]);
    }
}

static void compute_reference(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    for (size_t i = 0; i < NUM_FIXTURES; i++) {
        compute_all(ctx, FIXTURES[i], &g_reference[i]);
    }
    ph_free(ctx);
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

    fixture_result_t *first = g_first[arg->thread_index];
    for (size_t i = 0; i < NUM_FIXTURES; i++) {
        compute_all(ctx, FIXTURES[i], &first[i]);
    }
    for (int rep = 1; rep < ITERATIONS_PER_THREAD; rep++) {
        for (size_t i = 0; i < NUM_FIXTURES; i++) {
            fixture_result_t got;
            compute_all(ctx, FIXTURES[i], &got);
            check_same(what, &got, &first[i], i);
        }
    }

    ph_free(ctx);
}

#    if defined(_WIN32)
static DWORD WINAPI worker_win(LPVOID p) {
    worker_body((worker_arg_t *)p);
    return 0;
}
#    else
static void *worker_pthread(void *p) {
    worker_body((worker_arg_t *)p);
    return NULL;
}
#    endif

static void run_once(void) {
    atomic_store(&g_ready, 0);
    atomic_store(&g_go, 0);

    worker_arg_t args[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_index = i;
        args[i].failed = 0;
    }

#    if defined(_WIN32)
    HANDLE handles[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        handles[i] = CreateThread(NULL, 0, worker_win, &args[i], 0, NULL);
        ASSERT_PTR_NOT_NULL(handles[i]);
    }
    /* All threads created; release the barrier once every one of them has checked in
     * (wait_at_barrier() itself blocks each thread until g_ready reaches NUM_THREADS). */
    atomic_store(&g_go, 1);
    WaitForMultipleObjects(NUM_THREADS, handles, TRUE, INFINITE);
    for (int i = 0; i < NUM_THREADS; i++) {
        CloseHandle(handles[i]);
    }
#    else
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT_INT_EQ(0, pthread_create(&threads[i], NULL, worker_pthread, &args[i]));
    }
    atomic_store(&g_go, 1);
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
#    endif

    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT_MSG(!args[i].failed, "thread %d could not ph_create()", i);
    }
    for (int t = 1; t < NUM_THREADS; t++) {
        char what[64];
        snprintf(what, sizeof(what), "thread %d vs thread 0", t);
        for (size_t i = 0; i < NUM_FIXTURES; i++) {
            check_same(what, &g_first[t][i], &g_first[0][i], i);
        }
    }
}

/* Run the whole thing three times back to back: a race that only shows up
 * occasionally would still be expected to show up at least once across three runs,
 * and TSan (see .github/workflows/ci.yml, tsan job) is watching every one of them. The
 * first run starts cold; the reference comes after it. */
static void test_many_contexts_many_threads(void) {
    for (int rep = 0; rep < 3; rep++) {
        run_once();
        if (rep == 0) {
            compute_reference();
        }
        for (size_t i = 0; i < NUM_FIXTURES; i++) {
            check_same("threads vs sequential", &g_first[0][i], &g_reference[i], i);
        }
    }
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
