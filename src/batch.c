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

#define PH_HASH_FLAGS_ALL                                                                        \
    (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH | PH_HASH_MHASH |              \
     PH_HASH_COLOR_HASH)

static ph_error_t ph_compute_by_flag(ph_context_t *ctx, uint32_t flag, uint64_t *out) {
    switch (flag) {
        case PH_HASH_AHASH:
            return ph_compute_ahash(ctx, out);
        case PH_HASH_DHASH:
            return ph_compute_dhash(ctx, out);
        case PH_HASH_PHASH:
            return ph_compute_phash(ctx, out);
        case PH_HASH_WHASH:
            return ph_compute_whash(ctx, out);
        case PH_HASH_MHASH:
            return ph_compute_mhash(ctx, out);
        case PH_HASH_COLOR_HASH:
            return ph_compute_color_hash(ctx, out);
        default:
            return PH_ERR_INVALID_ARGUMENT;
    }
}

static int ph_flag_is_single_valid_bit(uint32_t flag) {
    return flag != 0 && (flag & (flag - 1)) == 0 && (flag & ~(uint32_t)PH_HASH_FLAGS_ALL) == 0;
}

static void process_file_item(ph_context_t *ctx, ph_batch_item_t *item, uint32_t flag) {
    if (!item->path) {
        item->hash = 0;
        item->status = PH_ERR_INVALID_ARGUMENT;
        return;
    }
    ph_error_t err = ph_load_from_file(ctx, item->path);
    if (err != PH_SUCCESS) {
        item->hash = 0;
        item->status = err;
        return;
    }
    item->status = ph_compute_by_flag(ctx, flag, &item->hash);
}

static void process_buffer_item(ph_context_t *ctx, ph_batch_buffer_item_t *item, uint32_t flag) {
    if (!item->buffer || item->length == 0) {
        item->hash = 0;
        item->status = PH_ERR_INVALID_ARGUMENT;
        return;
    }
    ph_error_t err = ph_load_from_memory(ctx, item->buffer, item->length);
    if (err != PH_SUCCESS) {
        item->hash = 0;
        item->status = err;
        return;
    }
    item->status = ph_compute_by_flag(ctx, flag, &item->hash);
}

typedef void (*ph_batch_process_fn)(ph_context_t *ctx, void *item, uint32_t flag);

static void process_file_item_v(ph_context_t *ctx, void *item, uint32_t flag) {
    process_file_item(ctx, (ph_batch_item_t *)item, flag);
}

static void process_buffer_item_v(ph_context_t *ctx, void *item, uint32_t flag) {
    process_buffer_item(ctx, (ph_batch_buffer_item_t *)item, flag);
}

#if defined(PH_ENABLE_THREADS)

typedef struct {
    uint8_t *items_base;
    size_t item_stride;
    size_t n;
    uint32_t flag;
    ph_batch_process_fn process;
    atomic_size_t next;
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

    for (;;) {
        size_t idx = atomic_fetch_add(&shared->next, 1);
        if (idx >= shared->n)
            break;
        shared->process(ctx, shared->items_base + idx * shared->item_stride, shared->flag);
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
                                        uint32_t flag, ph_batch_process_fn process,
                                        int nthreads) {
    ph_batch_shared_t shared = {
        .items_base = (uint8_t *)items_base,
        .item_stride = item_stride,
        .n = n,
        .flag = flag,
        .process = process,
    };
    atomic_init(&shared.next, 0);

#if defined(_WIN32)
    HANDLE *handles = malloc(sizeof(HANDLE) * (size_t)nthreads);
    if (!handles)
        return PH_ERR_ALLOCATION_FAILED;
    int spawned = 0;
    for (int i = 0; i < nthreads; i++) {
        handles[i] = CreateThread(NULL, 0, ph_batch_worker_win, &shared, 0, NULL);
        if (handles[i])
            spawned++;
    }
    if (spawned > 0)
        WaitForMultipleObjects((DWORD)spawned, handles, TRUE, INFINITE);
    for (int i = 0; i < nthreads; i++) {
        if (handles[i])
            CloseHandle(handles[i]);
    }
    free(handles);
#else
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

static ph_error_t ph_hash_batch(void *items_base, size_t item_stride, size_t n, uint32_t flag,
                                int threads, ph_batch_process_fn process,
                                void (*init_defaults)(void *item)) {
    if (n == 0)
        return PH_SUCCESS;
    if (!items_base || threads < 0 || !ph_flag_is_single_valid_bit(flag))
        return PH_ERR_INVALID_ARGUMENT;

    for (size_t i = 0; i < n; i++) {
        init_defaults((uint8_t *)items_base + i * item_stride);
    }

    int nthreads = ph_resolve_thread_count(threads, n);

    if (nthreads <= 1) {
        ph_context_t *ctx = NULL;
        if (ph_create(&ctx) != PH_SUCCESS)
            return PH_ERR_ALLOCATION_FAILED;
        for (size_t i = 0; i < n; i++) {
            process(ctx, (uint8_t *)items_base + i * item_stride, flag);
        }
        ph_free(ctx);
        return PH_SUCCESS;
    }

#if defined(PH_ENABLE_THREADS)
    return ph_batch_run_threaded(items_base, item_stride, n, flag, process, nthreads);
#else
    return PH_ERR_NOT_IMPLEMENTED; /* unreachable: ph_resolve_thread_count clamps to 1 */
#endif
}

static void init_file_item_defaults(void *item) {
    ph_batch_item_t *i = (ph_batch_item_t *)item;
    i->hash = 0;
    i->status = PH_ERR_ALLOCATION_FAILED;
}

static void init_buffer_item_defaults(void *item) {
    ph_batch_buffer_item_t *i = (ph_batch_buffer_item_t *)item;
    i->hash = 0;
    i->status = PH_ERR_ALLOCATION_FAILED;
}

PH_API ph_error_t ph_hash_files(ph_batch_item_t *items, size_t n, uint32_t flag, int threads) {
    return ph_hash_batch(items, sizeof(ph_batch_item_t), n, flag, threads, process_file_item_v,
                         init_file_item_defaults);
}

PH_API ph_error_t ph_hash_buffers(ph_batch_buffer_item_t *items, size_t n, uint32_t flag,
                                  int threads) {
    return ph_hash_batch(items, sizeof(ph_batch_buffer_item_t), n, flag, threads,
                         process_buffer_item_v, init_buffer_item_defaults);
}
