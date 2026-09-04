#include "internal.h"
#include <stdatomic.h>
#include <stdlib.h>

#if defined(PH_ENABLE_THREADS)
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif
#endif

#define PH_HASH_FLAGS_ALL                                                                          \
    (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH | PH_HASH_COLOR_HASH)

static int ph_flags_are_valid(uint32_t flags) {
    return flags != 0 && (flags & ~(uint32_t)PH_HASH_FLAGS_ALL) == 0;
}

static void clear_hashes(uint64_t hashes[PH_HASH_FLAGS_COUNT]) {
    for (int i = 0; i < PH_HASH_FLAGS_COUNT; i++) {
        hashes[i] = 0;
    }
}

static void process_file_item(ph_context_t *ctx, ph_batch_item_t *item, uint32_t flags) {
    if (!item->path) {
        clear_hashes(item->hashes);
        item->status = PH_ERR_INVALID_ARGUMENT;
        return;
    }
    ph_error_t err = ph_load_from_file(ctx, item->path);
    if (err != PH_SUCCESS) {
        clear_hashes(item->hashes);
        item->status = err;
        return;
    }
    item->status = ph_compute_multi(ctx, flags, item->hashes);
}

static void process_buffer_item(ph_context_t *ctx, ph_batch_buffer_item_t *item, uint32_t flags) {
    if (!item->buffer || item->length == 0) {
        clear_hashes(item->hashes);
        item->status = PH_ERR_INVALID_ARGUMENT;
        return;
    }
    ph_error_t err = ph_load_from_memory(ctx, item->buffer, item->length);
    if (err != PH_SUCCESS) {
        clear_hashes(item->hashes);
        item->status = err;
        return;
    }
    item->status = ph_compute_multi(ctx, flags, item->hashes);
}

typedef void (*ph_batch_process_fn)(ph_context_t *ctx, void *item, uint32_t flags);

static void process_file_item_v(ph_context_t *ctx, void *item, uint32_t flags) {
    process_file_item(ctx, (ph_batch_item_t *)item, flags);
}

static void process_buffer_item_v(ph_context_t *ctx, void *item, uint32_t flags) {
    process_buffer_item(ctx, (ph_batch_buffer_item_t *)item, flags);
}

#if defined(PH_ENABLE_THREADS)

typedef struct {
    uint8_t *items_base;
    size_t item_stride;
    size_t n;
    uint32_t flags;
    ph_batch_process_fn process;
    atomic_size_t next;
    /* Number of workers that got a context and therefore actually drained the index.
     * Zero means no item was looked at at all -- see the return contract below. */
    atomic_int workers_ready;
} ph_batch_shared_t;

static void ph_batch_worker_run(ph_batch_shared_t *shared) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        /* Leave this thread's would-be share unclaimed; other workers (if any)
         * still drain the shared index and will pick it up. Items are pre-set
         * to PH_ERR_ALLOCATION_FAILED, so nothing is left uninitialized even
         * if every worker fails to allocate a context. */
        return;
    }
    atomic_fetch_add(&shared->workers_ready, 1);

    for (;;) {
        size_t idx = atomic_fetch_add(&shared->next, 1);
        if (idx >= shared->n)
            break;
        shared->process(ctx, shared->items_base + idx * shared->item_stride, shared->flags);
    }

    ph_free(ctx);
}

#if defined(_WIN32)
static DWORD WINAPI ph_batch_worker_win(LPVOID arg) {
    ph_batch_worker_run((ph_batch_shared_t *)arg);
    return 0;
}
#else
static void *ph_batch_worker_pthread(void *arg) {
    ph_batch_worker_run((ph_batch_shared_t *)arg);
    return NULL;
}
#endif

/* Known and deliberate platform split, documented on ph_hash_files() (R47).
 *
 * The Windows branch reports only the processors of the *current processor group*, which
 * the OS caps at 64. So `threads = 0` on a machine with more than 64 logical processors
 * spawns at most 64 workers here, while the POSIX branch spawns one per online CPU.
 *
 * Swapping in GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) would be a one-line change and
 * would make things worse, not better: a thread inherits the processor group of its creator,
 * so workers past the 64th would contend for the same 64 logical processors -- more threads,
 * more context switching, no extra parallelism. A correct fix has to place workers into
 * groups explicitly (SetThreadGroupAffinity, or InitializeProcThreadAttributeList with
 * PROC_THREAD_ATTRIBUTE_GROUP_AFFINITY), which is a design change that cannot be validated
 * anywhere in this project's current test matrix. Until such a machine is available for
 * testing, the limitation stays documented rather than half-fixed.
 *
 * Note this is not the cause of the MAXIMUM_WAIT_OBJECTS defect fixed in R05: the wait there
 * is now batched, so it is correct for any worker count. The 64-processor cap merely kept
 * `threads = 0` from ever reaching that limit, which is why the defect went unnoticed. */
static int ph_detect_num_cores(void) {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

static ph_error_t ph_batch_run_threaded(void *items_base, size_t item_stride, size_t n,
                                        uint32_t flags, ph_batch_process_fn process, int nthreads) {
    ph_batch_shared_t shared = {
        .items_base = (uint8_t *)items_base,
        .item_stride = item_stride,
        .n = n,
        .flags = flags,
        .process = process,
    };
    atomic_init(&shared.next, 0);
    atomic_init(&shared.workers_ready, 0);

#if defined(_WIN32)
    /* nthreads is clamped to n by ph_resolve_thread_count(), so on a 64-bit size_t this
     * product cannot wrap -- but on a 32-bit size_t with a huge n it can. Refuse instead
     * of allocating a wrapped-around, too-small handle array. */
    /* Spelled out rather than via a helper: the macro previously used here was a
     * tautology wherever SIZE_MAX == ULLONG_MAX, i.e. it checked nothing on every
     * 64-bit build (removed in R03). */
    if ((size_t)nthreads > SIZE_MAX / sizeof(HANDLE))
        return PH_ERR_ALLOCATION_FAILED;
    HANDLE *handles = malloc(sizeof(HANDLE) * (size_t)nthreads);
    if (!handles)
        return PH_ERR_ALLOCATION_FAILED;
    /* Store only handles that were actually created, packed with no gaps.
     * WaitForMultipleObjects() fails immediately with WAIT_FAILED if *any* slot in
     * the range it is given is NULL, so a single CreateThread() failure in the middle
     * of the array used to make us stop waiting while other workers were still
     * writing into items[] -- a data race and a use-after-free for the caller. */
    int spawned = 0;
    for (int i = 0; i < nthreads; i++) {
        HANDLE h = CreateThread(NULL, 0, ph_batch_worker_win, &shared, 0, NULL);
        if (h)
            handles[spawned++] = h;
    }

    /* WaitForMultipleObjects() accepts at most MAXIMUM_WAIT_OBJECTS (64) handles, so
     * wait in chunks of that size instead of clamping nthreads to 64: clamping would
     * silently cap parallelism on machines with more than 64 logical processors
     * (routine on CI runners and servers), which is exactly the configuration
     * `threads = 0` is meant to exploit. Waiting on chunks one after another is safe
     * because the workers are independent: they only drain a shared atomic index and
     * never wait on each other or on us, so by the time the last chunk returns every
     * worker has terminated. */
    for (int i = 0; i < spawned;) {
        DWORD chunk = (DWORD)(spawned - i);
        if (chunk > MAXIMUM_WAIT_OBJECTS)
            chunk = MAXIMUM_WAIT_OBJECTS;
        if (WaitForMultipleObjects(chunk, handles + i, TRUE, INFINITE) == WAIT_FAILED) {
            /* Should not happen (all handles are valid), but returning here would
             * hand the caller an array that live workers are still writing to.
             * Fall back to joining this chunk one handle at a time. */
            for (DWORD k = 0; k < chunk; k++) {
                WaitForSingleObject(handles[i + (int)k], INFINITE);
            }
        }
        i += (int)chunk;
    }

    for (int i = 0; i < spawned; i++) {
        CloseHandle(handles[i]);
    }
    free(handles);
#else
    /* Same overflow guard as the Windows branch above. */
    /* Spelled out rather than via a helper: the macro previously used here was a
     * tautology wherever SIZE_MAX == ULLONG_MAX, i.e. it checked nothing on every
     * 64-bit build (removed in R03). */
    if ((size_t)nthreads > SIZE_MAX / sizeof(pthread_t))
        return PH_ERR_ALLOCATION_FAILED;
    pthread_t *threads_arr = malloc(sizeof(pthread_t) * (size_t)nthreads);
    if (!threads_arr)
        return PH_ERR_ALLOCATION_FAILED;
    int spawned = 0;
    for (int i = 0; i < nthreads; i++) {
        if (pthread_create(&threads_arr[spawned], NULL, ph_batch_worker_pthread, &shared) == 0)
            spawned++;
    }
    for (int i = 0; i < spawned; i++) {
        pthread_join(threads_arr[i], NULL);
    }
    free(threads_arr);
#endif

    /* Nothing touched items[]: either not a single thread was created (spawned == 0), or
     * threads were created but every one of them bailed out on a failed ph_create(), so
     * none ever drained the shared index. In both cases every item is still at its pre-set
     * PH_ERR_ALLOCATION_FAILED and reporting PH_SUCCESS would tell the caller the batch is
     * done when no work was performed at all.
     *
     * Partial degradation is deliberately *not* an error: as long as one worker got a
     * context it drains the whole index by itself, so the batch still completes and the
     * per-item statuses are the full story. */
    if (spawned == 0 || atomic_load(&shared.workers_ready) == 0)
        return PH_ERR_ALLOCATION_FAILED;

    return PH_SUCCESS;
}

#endif /* PH_ENABLE_THREADS */

static int ph_resolve_thread_count(int threads, size_t n) {
#if defined(PH_ENABLE_THREADS)
    int count = (threads == 0) ? ph_detect_num_cores() : threads;
    if (count < 1)
        count = 1;
#else
    (void)threads;
    int count = 1;
#endif
    if ((size_t)count > n)
        count = (int)n;
    if (count < 1)
        count = 1;
    return count;
}

static ph_error_t ph_hash_batch(void *items_base, size_t item_stride, size_t n, uint32_t flags,
                                int threads, ph_batch_process_fn process,
                                void (*init_defaults)(void *item)) {
    /* Validation runs before the `n == 0` shortcut: an empty batch must not swallow a
     * malformed call. `flags` and `threads` are checked unconditionally; `items_base` is
     * only required when there is something to dereference, so a (NULL, 0) pair -- the
     * natural spelling of an empty array -- stays a no-op success. */
    if (threads < 0 || !ph_flags_are_valid(flags))
        return PH_ERR_INVALID_ARGUMENT;
    if (!items_base && n > 0)
        return PH_ERR_INVALID_ARGUMENT;
    if (n == 0)
        return PH_SUCCESS;

    for (size_t i = 0; i < n; i++) {
        init_defaults((uint8_t *)items_base + i * item_stride);
    }

    int nthreads = ph_resolve_thread_count(threads, n);

    if (nthreads <= 1) {
        ph_context_t *ctx = NULL;
        if (ph_create(&ctx) != PH_SUCCESS)
            return PH_ERR_ALLOCATION_FAILED;
        for (size_t i = 0; i < n; i++) {
            process(ctx, (uint8_t *)items_base + i * item_stride, flags);
        }
        ph_free(ctx);
        return PH_SUCCESS;
    }

#if defined(PH_ENABLE_THREADS)
    return ph_batch_run_threaded(items_base, item_stride, n, flags, process, nthreads);
#else
    return PH_ERR_NOT_IMPLEMENTED; /* unreachable: ph_resolve_thread_count clamps to 1 */
#endif
}

static void init_file_item_defaults(void *item) {
    ph_batch_item_t *i = (ph_batch_item_t *)item;
    clear_hashes(i->hashes);
    i->status = PH_ERR_ALLOCATION_FAILED;
}

static void init_buffer_item_defaults(void *item) {
    ph_batch_buffer_item_t *i = (ph_batch_buffer_item_t *)item;
    clear_hashes(i->hashes);
    i->status = PH_ERR_ALLOCATION_FAILED;
}

PH_API ph_error_t ph_hash_files(ph_batch_item_t *items, size_t n, uint32_t flags, int threads) {
    return ph_hash_batch(items, sizeof(ph_batch_item_t), n, flags, threads, process_file_item_v,
                         init_file_item_defaults);
}

PH_API ph_error_t ph_hash_buffers(ph_batch_buffer_item_t *items, size_t n, uint32_t flags,
                                  int threads) {
    return ph_hash_batch(items, sizeof(ph_batch_buffer_item_t), n, flags, threads,
                         process_buffer_item_v, init_buffer_item_defaults);
}
