#ifndef PH_TEST_ALLOC_SHIM_H
#define PH_TEST_ALLOC_SHIM_H

/*
 * Deterministic allocation-failure injection + leak accounting for tests.
 *
 * Mechanism: the shim defines malloc/calloc/realloc/free/posix_memalign in the
 * test executable itself. Because libphash is linked as a static archive, the
 * link editor resolves those calls to the definitions in the executable rather
 * than to libc. This is the only interception technique that works uniformly on
 * macOS and Linux: `-Wl,--wrap` is a GNU ld feature that Apple's linker does not
 * have, and DYLD_INSERT_LIBRARIES needs a separate shared object plus an
 * environment variable, which nothing in this repo's build or CI would set.
 *
 * The forwarding target must not be the symbols we just replaced, so each
 * platform gets a private door to the real allocator:
 *   - macOS: the malloc zone API (malloc_zone_malloc & friends). libSystem
 *     itself keeps using its internal allocator, so libc's own bookkeeping never
 *     shows up in our counters.
 *   - glibc: the __libc_* aliases. Here libc *does* route through the shim, so
 *     failures are only injected while the shim is explicitly armed, and only
 *     allocations made while armed are tracked for leaks.
 * Anywhere else the shim compiles to a no-op and reports itself unavailable;
 * the test then skips instead of pretending to have verified something.
 *
 * A shared-library build defeats the technique (a dylib/so binds its own
 * malloc), so callers must probe at runtime -- arm the shim, call into the
 * library, and check that the counter moved -- rather than trust
 * PH_SHIM_SUPPORTED alone.
 *
 * AddressSanitizer is a third case that disables the shim outright (see the
 * PH_SHIM_SUPPORTED definition below for why): ASan's own startup calls free()
 * before its shadow memory exists, and that call lands in this file's
 * ASan-instrumented free() override, segfaulting on the shadow check with no
 * ASan report to show for it.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* AddressSanitizer's own startup sequence is incompatible with a statically-linked
 * malloc/free override: on Linux, __asan_init() -> AsanInitInternal() ->
 * InitializeAsanInterceptors() resolves real libc symbols via glibc's dlsym/dlvsym
 * machinery, which calls free() on its internal dlerror buffer (dl_error_free())
 * *before* ASan has finished mapping its own shadow memory or installing its signal
 * handlers. Since this file's free() is a strong global symbol, that early call
 * lands in ph_shim_untrack() -- itself compiled with ASan instrumentation, so its
 * access to the (shadow-backed) ph_shim.slots[] array segfaults reading shadow
 * memory that doesn't exist yet, with no ASan report at all (its fault handler
 * isn't installed yet either). This isn't a bug in the shim's own logic, just a
 * fundamental ordering conflict with ASan's static-link interception model, so the
 * override is simply not attempted under ASan; PH_SHIM_SUPPORTED's existing
 * runtime-probe/graceful-skip contract (see shim_is_effective() in the tests) covers
 * this the same way it covers a shared-library build. */
#if defined(__SANITIZE_ADDRESS__)
#define PH_SHIM_SUPPORTED 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PH_SHIM_SUPPORTED 0
#endif
#endif

#ifndef PH_SHIM_SUPPORTED
#if defined(__APPLE__)
#define PH_SHIM_SUPPORTED 1
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#define PH_SHIM_SUPPORTED 1
extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void __libc_free(void *);
extern void *__libc_memalign(size_t, size_t);
#else
#define PH_SHIM_SUPPORTED 0
#endif
#endif

/* ---- real allocator access -------------------------------------------- */

#if PH_SHIM_SUPPORTED
#if defined(__APPLE__)
static void *ph_real_malloc(size_t n) { return malloc_zone_malloc(malloc_default_zone(), n); }
static void *ph_real_calloc(size_t c, size_t n) {
    return malloc_zone_calloc(malloc_default_zone(), c, n);
}
static void *ph_real_realloc(void *p, size_t n) {
    return malloc_zone_realloc(malloc_default_zone(), p, n);
}
static void ph_real_free(void *p) {
    if (!p)
        return;
    /* The pointer may come from a zone other than the default one (or, for a
     * pointer libSystem never registered, from none at all); freeing it into
     * the wrong zone would corrupt the heap. */
    malloc_zone_t *z = malloc_zone_from_ptr(p);
    if (z)
        malloc_zone_free(z, p);
}
static void *ph_real_memalign(size_t align, size_t n) {
    return malloc_zone_memalign(malloc_default_zone(), align, n);
}
#else
static void *ph_real_malloc(size_t n) { return __libc_malloc(n); }
static void *ph_real_calloc(size_t c, size_t n) { return __libc_calloc(c, n); }
static void *ph_real_realloc(void *p, size_t n) { return __libc_realloc(p, n); }
static void ph_real_free(void *p) { __libc_free(p); }
static void *ph_real_memalign(size_t align, size_t n) { return __libc_memalign(align, n); }
#endif
#endif /* PH_SHIM_SUPPORTED */

/* ---- shim state -------------------------------------------------------- */

#define PH_SHIM_SLOTS 65536u /* power of two; open-addressed live-pointer set */

typedef struct {
    void *ptr;
    size_t size;
} ph_shim_slot_t;

typedef struct {
    int armed;     /* count and (optionally) inject only while armed */
    long counter;  /* allocations seen since the last reset */
    long fail_at;  /* 1-based ordinal to fail; 0 = never fail */
    long live;     /* tracked allocations not yet freed */
    long injected; /* how many failures were actually injected */
    int overflow;  /* the live-pointer set ran out of slots */
    ph_shim_slot_t slots[PH_SHIM_SLOTS];
} ph_shim_state_t;

static ph_shim_state_t ph_shim; /* zero-initialised, never heap-allocated */

#if PH_SHIM_SUPPORTED

static size_t ph_shim_hash(void *p) {
    uintptr_t v = (uintptr_t)p;
    v ^= v >> 33;
    v *= (uintptr_t)0xff51afd7ed558ccdULL;
    v ^= v >> 29;
    return (size_t)(v & (PH_SHIM_SLOTS - 1u));
}

static void ph_shim_track(void *p, size_t size) {
    size_t i = ph_shim_hash(p);
    for (size_t probe = 0; probe < PH_SHIM_SLOTS; probe++) {
        size_t k = (i + probe) & (PH_SHIM_SLOTS - 1u);
        if (ph_shim.slots[k].ptr == NULL) {
            ph_shim.slots[k].ptr = p;
            ph_shim.slots[k].size = size;
            ph_shim.live++;
            return;
        }
    }
    ph_shim.overflow = 1;
}

/* Returns 1 if the pointer was tracked (and drops it), 0 if it is foreign. */
static int ph_shim_untrack(void *p) {
    size_t i = ph_shim_hash(p);
    for (size_t probe = 0; probe < PH_SHIM_SLOTS; probe++) {
        size_t k = (i + probe) & (PH_SHIM_SLOTS - 1u);
        if (ph_shim.slots[k].ptr == p) {
            ph_shim.slots[k].ptr = NULL;
            ph_shim.slots[k].size = 0;
            ph_shim.live--;
            /* Open addressing: rehash the rest of the cluster so the probe
             * sequence for other keys stays unbroken. */
            for (size_t j = probe + 1; j < PH_SHIM_SLOTS; j++) {
                size_t m = (i + j) & (PH_SHIM_SLOTS - 1u);
                ph_shim_slot_t moved = ph_shim.slots[m];
                if (moved.ptr == NULL)
                    break;
                ph_shim.slots[m].ptr = NULL;
                ph_shim.slots[m].size = 0;
                ph_shim.live--;
                ph_shim_track(moved.ptr, moved.size);
            }
            return 1;
        }
        if (ph_shim.slots[k].ptr == NULL)
            return 0;
    }
    return 0;
}

/* Returns 1 when this allocation must fail. */
static int ph_shim_should_fail(void) {
    if (!ph_shim.armed)
        return 0;
    ph_shim.counter++;
    if (ph_shim.fail_at != 0 && ph_shim.counter == ph_shim.fail_at) {
        ph_shim.injected++;
        return 1;
    }
    return 0;
}

void *malloc(size_t n) {
    if (ph_shim_should_fail())
        return NULL;
    void *p = ph_real_malloc(n);
    if (p && ph_shim.armed)
        ph_shim_track(p, n);
    return p;
}

void *calloc(size_t count, size_t n) {
    if (ph_shim_should_fail())
        return NULL;
    void *p = ph_real_calloc(count, n);
    if (p && ph_shim.armed)
        ph_shim_track(p, count * n);
    return p;
}

void *realloc(void *old, size_t n) {
    if (ph_shim_should_fail())
        return NULL; /* on failure the original block must stay valid */
    int tracked = old ? ph_shim_untrack(old) : 0;
    void *p = ph_real_realloc(old, n);
    if (!p) {
        if (tracked)
            ph_shim_track(old, n); /* nothing moved; keep owning the old block */
        return NULL;
    }
    if (tracked || ph_shim.armed)
        ph_shim_track(p, n);
    return p;
}

void free(void *p) {
    if (!p)
        return;
    ph_shim_untrack(p);
    ph_real_free(p);
}

int posix_memalign(void **out, size_t align, size_t n) {
    if (!out)
        return EINVAL;
    if (ph_shim_should_fail())
        return ENOMEM;
    void *p = ph_real_memalign(align, n);
    if (!p)
        return ENOMEM;
    if (ph_shim.armed)
        ph_shim_track(p, n);
    *out = p;
    return 0;
}

#endif /* PH_SHIM_SUPPORTED */

/* ---- control API ------------------------------------------------------- */

/* Forget all accounting. Any pointer allocated before this call becomes
 * foreign, so it is never reported as a leak. */
static void ph_shim_reset(void) {
    memset(ph_shim.slots, 0, sizeof(ph_shim.slots));
    ph_shim.counter = 0;
    ph_shim.fail_at = 0;
    ph_shim.live = 0;
    ph_shim.injected = 0;
    ph_shim.overflow = 0;
    ph_shim.armed = 0;
}

/* fail_at == 0 counts allocations without failing any. */
static void ph_shim_arm(long fail_at) {
    ph_shim_reset();
    ph_shim.fail_at = fail_at;
    ph_shim.armed = 1;
}

static void ph_shim_disarm(void) { ph_shim.armed = 0; }

static long ph_shim_count(void) { return ph_shim.counter; }
static long ph_shim_live(void) { return ph_shim.live; }
static long ph_shim_injected(void) { return ph_shim.injected; }
static int ph_shim_overflowed(void) { return ph_shim.overflow; }

#endif /* PH_TEST_ALLOC_SHIM_H */
