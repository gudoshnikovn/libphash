/* sched_getaffinity() and CPU_COUNT() are GNU extensions; the rest of this file is ISO C
 * plus POSIX threads. Defined before any header, as feature-test macros must be. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "internal.h"

/* MSVC only ships <stdatomic.h> under /std:c11 or later (VS 17.5+); the CMake
 * build gets that flag from CMAKE_C_STANDARD (see CMakeLists.txt), but a build invoking
 * cl.exe directly without it fails inside the header with a confusing
 * "cannot open source file" -- fail here instead, with a message that names
 * the actual requirement. */
#if defined(_MSC_VER) && !defined(__STDC_VERSION__)
#error "src/batch.c requires <stdatomic.h>: build MSVC with /std:c11 or later"
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#if defined(PH_ENABLE_THREADS)
#include <pthread.h>
#endif
#endif
#if defined(__linux__)
#include <sched.h>
#endif

static int ph_flags_are_valid(uint32_t flags) {
    return flags != 0 && (flags & ~(uint32_t)PH_HASH_FLAGS_ALL) == 0;
}

_Static_assert(PH_HASH_FLAGS_COUNT <= PH_BATCH_HASHES_CAPACITY,
               "a batch item must have a slot for every uint64_t algorithm");

static void clear_hashes(uint64_t hashes[PH_BATCH_HASHES_CAPACITY]) {
    for (int i = 0; i < PH_BATCH_HASHES_CAPACITY; i++) {
        hashes[i] = 0;
    }
}

/* --- How many CPUs this process may use --------------------------------------------
 *
 * Not how many the machine has: a container limited with --cpuset-cpus or --cpus, a
 * `taskset`, or a Kubernetes CPU limit leaves the machine count unchanged, and one worker
 * per host CPU there means several times the memory for no throughput (each worker holds
 * a decoded image) and throttled, ragged latency under a CFS quota. */

int ph_cpu_quota_limit(const char *cpu_max) {
    if (!cpu_max)
        return 0;
    long long quota = 0, period = 0;
    /* cgroup v2 cpu.max: "<quota> <period>" or "max <period>"; v1 is read into the same
     * form by the caller. */
    if (sscanf(cpu_max, "%lld %lld", &quota, &period) != 2 || quota <= 0 || period <= 0)
        return 0;
    long long cpus = (quota + period - 1) / period;
    return cpus > INT_MAX ? INT_MAX : (int)cpus;
}

#if defined(__linux__)
/* Reads at most `cap - 1` bytes of a small text file; 1 on success. */
static int ph_read_small_file(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n > 0;
}

/* The CPU quota of the cgroup the process sees at the conventional mount point -- inside
 * a container that is the container's own. 0 when there is none or it cannot be read. */
static int ph_cgroup_cpu_limit(void) {
    char buf[128];
    if (ph_read_small_file("/sys/fs/cgroup/cpu.max", buf, sizeof(buf)))
        return ph_cpu_quota_limit(buf); /* cgroup v2 */
    char quota[64], period[64];
    if (ph_read_small_file("/sys/fs/cgroup/cpu/cpu.cfs_quota_us", quota, sizeof(quota)) &&
        ph_read_small_file("/sys/fs/cgroup/cpu/cpu.cfs_period_us", period, sizeof(period))) {
        snprintf(buf, sizeof(buf), "%lld %lld", atoll(quota), atoll(period));
        return ph_cpu_quota_limit(buf); /* cgroup v1: quota -1 means none */
    }
    return 0;
}
#endif

int ph_available_cpus(void) {
    int n = 0;
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    n = si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 1;
    /* The process affinity mask, within the current processor group (see the note on the
     * 64-processor limit below). */
    DWORD_PTR process_mask = 0, system_mask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) &&
        process_mask != 0) {
        int allowed = 0;
        for (DWORD_PTR m = process_mask; m; m &= m - 1)
            allowed++;
        if (allowed < n)
            n = allowed;
    }
#else
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    n = online > 0 ? (online > INT_MAX ? INT_MAX : (int)online) : 1;
#if defined(__linux__)
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        int allowed = CPU_COUNT(&set);
        if (allowed > 0 && allowed < n)
            n = allowed;
    }
    int quota = ph_cgroup_cpu_limit();
    if (quota > 0 && quota < n)
        n = quota;
#endif
#endif
    return n > 0 ? n : 1;
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

/* Everything about one batch call beyond its items: the configuration every worker
 * context starts from (NULL = ph_create()'s defaults) and the caller's callbacks. */
typedef struct {
    const struct ph_context_config *config;
    ph_batch_continue_fn should_continue;
    ph_batch_progress_fn on_progress;
    void *user_data;
} ph_batch_hooks_t;

static ph_error_t ph_batch_create_context(const ph_batch_hooks_t *hooks, ph_context_t **out) {
    ph_error_t err = ph_create(out);
    if (err == PH_SUCCESS && hooks->config)
        (*out)->config = *hooks->config;
    return err;
}

static int ph_batch_should_stop(const ph_batch_hooks_t *hooks) {
    return hooks->should_continue && !hooks->should_continue(hooks->user_data);
}

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
    const ph_batch_hooks_t *hooks;
    /* Items are claimed in index order, so the claimed ones are always the prefix
     * [0, min(next, n)): after the join, everything from there on was never started. */
    atomic_size_t next;
    /* Items finished, for the progress callback's `done`. */
    atomic_size_t done;
    /* Set by the first worker whose should_continue returned 0; the others stop claiming. */
    atomic_int stop;
    /* Number of workers that got a context and therefore actually drained the index.
     * Zero means no item was looked at at all -- see the return contract below. */
    atomic_int workers_ready;
} ph_batch_shared_t;

static void ph_batch_worker_run(ph_batch_shared_t *shared) {
    ph_context_t *ctx = NULL;
    if (ph_batch_create_context(shared->hooks, &ctx) != PH_SUCCESS) {
        /* Leave this thread's would-be share unclaimed; other workers (if any)
         * still drain the shared index and will pick it up. Items are pre-set
         * to PH_ERR_ALLOCATION_FAILED, so nothing is left uninitialized even
         * if every worker fails to allocate a context. */
        return;
    }
    atomic_fetch_add(&shared->workers_ready, 1);

    const ph_batch_hooks_t *hooks = shared->hooks;
    for (;;) {
        if (atomic_load(&shared->stop))
            break;
        if (ph_batch_should_stop(hooks)) {
            atomic_store(&shared->stop, 1);
            break;
        }
        size_t idx = atomic_fetch_add(&shared->next, 1);
        if (idx >= shared->n)
            break;
        shared->process(ctx, shared->items_base + idx * shared->item_stride, shared->flags);
        if (hooks->on_progress)
            hooks->on_progress(atomic_fetch_add(&shared->done, 1) + 1, shared->n, hooks->user_data);
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

/* Known and deliberate platform split, documented on ph_hash_files().
 *
 * The Windows branch reports only the processors of the *current processor group*, which
 * the OS caps at 64. So `threads = 0` on a machine with more than 64 logical processors
 * spawns at most 64 workers here, while the POSIX branch spawns one per CPU the process
 * may use (the online count, narrowed by affinity and the cgroup quota on Linux).
 *
 * Swapping in GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) would be a one-line change and
 * would make things worse, not better: a thread inherits the processor group of its creator,
 * so workers past the 64th would contend for the same 64 logical processors -- more threads,
 * more context switching, no extra parallelism. A correct fix has to place workers into
 * groups explicitly (SetThreadGroupAffinity, or InitializeProcThreadAttributeList with
 * PROC_THREAD_ATTRIBUTE_GROUP_AFFINITY), a design change that needs a >64-processor
 * Windows machine to validate; the limitation is documented instead.
 *
 * Independent of this cap, the wait below is chunked by MAXIMUM_WAIT_OBJECTS, so it is
 * correct for any worker count. */
static int ph_detect_num_cores(void) { return ph_available_cpus(); }
/* Runs the batch on `nthreads` workers. On return, *out_started is the number of items
 * that were started -- the prefix [0, *out_started) -- which is n unless the batch was
 * cancelled or no worker ever ran. */
static ph_error_t ph_batch_run_threaded(void *items_base, size_t item_stride, size_t n,
                                        uint32_t flags, ph_batch_process_fn process, int nthreads,
                                        const ph_batch_hooks_t *hooks, size_t *out_started) {
    *out_started = 0;
    ph_batch_shared_t shared = {
        .items_base = (uint8_t *)items_base,
        .item_stride = item_stride,
        .n = n,
        .flags = flags,
        .process = process,
        .hooks = hooks,
    };
    atomic_init(&shared.next, 0);
    atomic_init(&shared.done, 0);
    atomic_init(&shared.stop, 0);
    atomic_init(&shared.workers_ready, 0);

#if defined(_WIN32)
    /* nthreads is clamped to n by ph_resolve_thread_count(), so on a 64-bit size_t this
     * product cannot wrap -- but on a 32-bit size_t with a huge n it can. Refuse instead
     * of allocating a wrapped-around, too-small handle array. */
    /* Spelled out rather than via a SIZE_MAX-vs-ULLONG_MAX helper: such a helper is a
     * tautology wherever the two are equal, i.e. on every 64-bit build. */
    if ((size_t)nthreads > SIZE_MAX / sizeof(HANDLE))
        return PH_ERR_ALLOCATION_FAILED;
    HANDLE *handles = malloc(sizeof(HANDLE) * (size_t)nthreads);
    if (!handles)
        return PH_ERR_ALLOCATION_FAILED;
    /* Store only handles that were actually created, packed with no gaps.
     * WaitForMultipleObjects() fails immediately with WAIT_FAILED if *any* slot in
     * the range it is given is NULL, so a NULL slot from a failed CreateThread() would
     * end the wait while other workers are still writing into items[] -- a data race
     * and a use-after-free for the caller. */
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
    /* Spelled out rather than via a SIZE_MAX-vs-ULLONG_MAX helper: such a helper is a
     * tautology wherever the two are equal, i.e. on every 64-bit build. */
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

    size_t claimed = atomic_load(&shared.next);
    *out_started = claimed < n ? claimed : n;
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

/* The single-threaded path: the calling thread, one context, items in order. Same
 * *out_started contract as ph_batch_run_threaded(). */
static ph_error_t ph_batch_run_sequential(void *items_base, size_t item_stride, size_t n,
                                          uint32_t flags, ph_batch_process_fn process,
                                          const ph_batch_hooks_t *hooks, size_t *out_started) {
    *out_started = 0;
    ph_context_t *ctx = NULL;
    if (ph_batch_create_context(hooks, &ctx) != PH_SUCCESS)
        return PH_ERR_ALLOCATION_FAILED;
    size_t started = 0;
    while (started < n && !ph_batch_should_stop(hooks)) {
        process(ctx, (uint8_t *)items_base + started * item_stride, flags);
        started++;
        if (hooks->on_progress)
            hooks->on_progress(started, n, hooks->user_data);
    }
    ph_free(ctx);
    *out_started = started;
    return PH_SUCCESS;
}

/* Sets an item's status and zeroes its hashes: the pre-set value every item starts from,
 * and the final value of every item a cancellation kept from being started. */
typedef void (*ph_batch_reset_fn)(void *item, ph_error_t status);

static ph_error_t ph_hash_batch(void *items_base, size_t item_stride, size_t n, uint32_t flags,
                                const ph_batch_options_t *options, ph_batch_process_fn process,
                                ph_batch_reset_fn reset) {
    ph_batch_options_t defaults;
    ph_batch_options_init(&defaults);
    if (!options)
        options = &defaults;

    /* Validation runs before the `n == 0` shortcut: an empty batch must not swallow a
     * malformed call. `flags` and the options are checked unconditionally; `items_base` is
     * only required when there is something to dereference, so a (NULL, 0) pair -- the
     * natural spelling of an empty array -- stays a no-op success. Only the first version
     * of the options struct exists, so anything shorter is a caller error. */
    if (options->struct_size < sizeof(ph_batch_options_t) || options->threads < 0 ||
        !ph_flags_are_valid(flags))
        return PH_ERR_INVALID_ARGUMENT;
    if (!items_base && n > 0)
        return PH_ERR_INVALID_ARGUMENT;
    if (n == 0)
        return PH_SUCCESS;

    /* The template's configuration is copied here, on the calling thread, so no worker
     * ever reads the caller's context. */
    struct ph_context_config config;
    ph_batch_hooks_t hooks = {
        .config = NULL,
        .should_continue = options->should_continue,
        .on_progress = options->on_progress,
        .user_data = options->user_data,
    };
    if (options->config) {
        config = options->config->config;
        hooks.config = &config;
    }

    for (size_t i = 0; i < n; i++) {
        reset((uint8_t *)items_base + i * item_stride, PH_ERR_ALLOCATION_FAILED);
    }

    int nthreads = ph_resolve_thread_count(options->threads, n);
    (void)nthreads; /* always 1 without threads: ph_resolve_thread_count() clamps */
    size_t started = 0;
    ph_error_t err;
#if defined(PH_ENABLE_THREADS)
    if (nthreads > 1)
        err = ph_batch_run_threaded(items_base, item_stride, n, flags, process, nthreads, &hooks,
                                    &started);
    else
#endif
        err = ph_batch_run_sequential(items_base, item_stride, n, flags, process, &hooks, &started);
    if (err != PH_SUCCESS)
        return err;
    if (started == n)
        return PH_SUCCESS;
    for (size_t i = started; i < n; i++) {
        reset((uint8_t *)items_base + i * item_stride, PH_ERR_CANCELLED);
    }
    return PH_ERR_CANCELLED;
}

static void reset_file_item(void *item, ph_error_t status) {
    ph_batch_item_t *i = (ph_batch_item_t *)item;
    clear_hashes(i->hashes);
    i->status = status;
}

static void reset_buffer_item(void *item, ph_error_t status) {
    ph_batch_buffer_item_t *i = (ph_batch_buffer_item_t *)item;
    clear_hashes(i->hashes);
    i->status = status;
}

PH_API ph_error_t ph_batch_options_init(ph_batch_options_t *options) {
    if (!options)
        return PH_ERR_INVALID_ARGUMENT;
    memset(options, 0, sizeof(*options));
    options->struct_size = sizeof(*options);
    return PH_SUCCESS;
}

PH_API ph_error_t ph_hash_files_ex(ph_batch_item_t *items, size_t n, uint32_t flags,
                                   const ph_batch_options_t *options) {
    return ph_hash_batch(items, sizeof(ph_batch_item_t), n, flags, options, process_file_item_v,
                         reset_file_item);
}

PH_API ph_error_t ph_hash_buffers_ex(ph_batch_buffer_item_t *items, size_t n, uint32_t flags,
                                     const ph_batch_options_t *options) {
    return ph_hash_batch(items, sizeof(ph_batch_buffer_item_t), n, flags, options,
                         process_buffer_item_v, reset_buffer_item);
}

PH_API ph_error_t ph_hash_files(ph_batch_item_t *items, size_t n, uint32_t flags, int threads) {
    ph_batch_options_t options;
    ph_batch_options_init(&options);
    options.threads = threads;
    return ph_hash_files_ex(items, n, flags, &options);
}

PH_API ph_error_t ph_hash_buffers(ph_batch_buffer_item_t *items, size_t n, uint32_t flags,
                                  int threads) {
    ph_batch_options_t options;
    ph_batch_options_init(&options);
    options.threads = threads;
    return ph_hash_buffers_ex(items, n, flags, &options);
}
