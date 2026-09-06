/*
 * Out-of-memory robustness: every allocation the library makes on a public code
 * path is failed in turn, and the run is checked for three things:
 *   1. the call reports an error instead of crashing or returning success with
 *      a half-built result;
 *   2. nothing allocated during the run is leaked;
 *   3. the context survives the failure -- after a failed call the same context
 *      still produces the same hashes as an untouched one.
 *
 * The allocation-failure injection itself lives in alloc_shim.h.
 */

#include "alloc_shim.h"

#include "libphash.h"
#include "test_macros.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PNG_PATH TEST_DATA_DIR "/photo_complex.png"
#define JPEG_PATH TEST_DATA_DIR "/photo.jpeg"
#define WEBP_PATH TEST_DATA_DIR "/photo.webp"

/* ---- soft failure reporting -------------------------------------------
 * Failures are collected rather than fatal: one aborted run would hide every
 * other failure point, and the interesting output is the full list. */

static int g_failures = 0;
static const char *g_scenario = "";
static long g_fail_at = 0;

static void defect(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void defect(const char *fmt, ...) {
    va_list ap;
    g_failures++;
    fprintf(stderr, "[FAIL] scenario '%s', failing allocation #%ld: ", g_scenario, g_fail_at);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---- guarded outputs ---------------------------------------------------
 * The library writes into caller-owned structs even on the error paths (radial
 * memsets the digest before it allocates). Padding around them catches a write
 * that runs past the declared output. */

#define GUARD_BYTE 0x5A

typedef struct {
    uint8_t front[32];
    ph_digest_t d;
    uint8_t back[32];
} guarded_digest_t;

typedef struct {
    uint8_t front[32];
    uint64_t v;
    uint8_t back[32];
} guarded_u64_t;

static void guard_init(void *p, size_t n) { memset(p, GUARD_BYTE, n); }

static void guard_check(const uint8_t *front, const uint8_t *back, const char *tag) {
    for (size_t i = 0; i < 32; i++) {
        if (front[i] != GUARD_BYTE || back[i] != GUARD_BYTE) {
            defect("%s: wrote outside its output struct", tag);
            return;
        }
    }
}

/* ---- result checking --------------------------------------------------- */

#define ALLOW_ALLOC 0x1   /* PH_ERR_ALLOCATION_FAILED */
#define ALLOW_DECODE 0x2  /* PH_ERR_DECODER_UNAVAILABLE: e.g. WebP with no decoder built in */
#define ALLOW_INVALID 0x4 /* PH_ERR_INVALID_ARGUMENT: e.g. no image loaded */

static int check(const char *tag, ph_error_t err, int allowed) {
    if (err == PH_SUCCESS)
        return 1;
    if (err == PH_ERR_ALLOCATION_FAILED && (allowed & ALLOW_ALLOC))
        return 0;
    if (err == PH_ERR_DECODER_UNAVAILABLE && (allowed & ALLOW_DECODE))
        return 0;
    if (err == PH_ERR_INVALID_ARGUMENT && (allowed & ALLOW_INVALID))
        return 0;
    defect("%s returned unexpected error %d (%s)", tag, (int)err, ph_get_error_string(err));
    return 0;
}

/* ---- fixtures ---------------------------------------------------------- */

typedef struct {
    uint8_t *data;
    size_t size;
} blob_t;

static blob_t g_png, g_jpeg;

static blob_t read_file(const char *path) {
    blob_t b = {NULL, 0};
    FILE *f = fopen(path, "rb");
    ASSERT_PTR_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    ASSERT(n > 0);
    b.data = (uint8_t *)malloc((size_t)n);
    ASSERT_PTR_NOT_NULL(b.data);
    ASSERT(fread(b.data, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    b.size = (size_t)n;
    return b;
}

/* ---- the hash battery -------------------------------------------------- */

typedef struct {
    uint64_t ahash, dhash, phash, whash_fast, whash_full;
    ph_digest_t bmh, mhash, color, moments, radial;
    int valid;
} golden_t;

static golden_t g_golden;

/* Runs every hash on ctx.
 *   out != NULL -- record the results (used by the un-injected reference pass).
 *   ref != NULL -- a hash that reports success while a failure is being injected
 *                  must still produce the reference value; anything else is a
 *                  silently degraded result, which is worse than an error. */
static void hash_battery(ph_context_t *ctx, golden_t *out, const golden_t *ref) {
    guarded_u64_t u;
    guarded_digest_t g;
    int allowed = ALLOW_ALLOC | ALLOW_INVALID;

#define U64_HASH(call, field, tag)                                                                 \
    do {                                                                                           \
        guard_init(&u, sizeof(u));                                                                 \
        u.v = 0;                                                                                   \
        int ok = check(tag, call(ctx, &u.v), allowed);                                             \
        guard_check(u.front, u.back, tag);                                                         \
        if (ok && out)                                                                             \
            out->field = u.v;                                                                      \
        if (ok && ref && ref->valid && u.v != ref->field)                                          \
            defect("%s reported success but returned %016llx instead of %016llx", tag,             \
                   (unsigned long long)u.v, (unsigned long long)ref->field);                       \
    } while (0)

#define DIGEST_HASH(call, field, tag)                                                              \
    do {                                                                                           \
        guard_init(&g, sizeof(g));                                                                 \
        memset(&g.d, 0, sizeof(g.d));                                                              \
        int ok = check(tag, call(ctx, &g.d), allowed);                                             \
        guard_check(g.front, g.back, tag);                                                         \
        if (ok && out)                                                                             \
            out->field = g.d;                                                                      \
        if (ok && ref && ref->valid &&                                                             \
            (g.d.size != ref->field.size || memcmp(g.d.data, ref->field.data, g.d.size) != 0))     \
            defect("%s reported success but returned a digest differing from the "                 \
                   "reference",                                                                    \
                   tag);                                                                           \
    } while (0)

    U64_HASH(ph_compute_ahash, ahash, "ph_compute_ahash");
    U64_HASH(ph_compute_dhash, dhash, "ph_compute_dhash");
    U64_HASH(ph_compute_phash, phash, "ph_compute_phash");

    ph_context_set_whash_mode(ctx, PH_WHASH_FAST);
    U64_HASH(ph_compute_whash, whash_fast, "ph_compute_whash(fast)");
    ph_context_set_whash_mode(ctx, PH_WHASH_FULL);
    U64_HASH(ph_compute_whash, whash_full, "ph_compute_whash(full)");
    ph_context_set_whash_mode(ctx, PH_WHASH_FAST);

    DIGEST_HASH(ph_compute_bmh, bmh, "ph_compute_bmh");
    DIGEST_HASH(ph_compute_mhash, mhash, "ph_compute_mhash");
    DIGEST_HASH(ph_compute_color_hash, color, "ph_compute_color_hash");
    DIGEST_HASH(ph_compute_color_moments_hash, moments, "ph_compute_color_moments_hash");
    DIGEST_HASH(ph_compute_radial_hash, radial, "ph_compute_radial_hash");

#undef U64_HASH
#undef DIGEST_HASH

    if (out)
        out->valid = 1;
}

/* Compares a clean run against the recorded reference. Any difference means an
 * injected failure left state behind (a stale cache, a leaked arena offset). */
static void check_recovery(ph_context_t *ctx) {
    golden_t now;
    memset(&now, 0, sizeof(now));

    ph_shim_disarm();
    if (!ph_is_loaded(ctx)) {
        if (ph_load_from_memory(ctx, g_png.data, g_png.size) != PH_SUCCESS) {
            defect("recovery: reload of the fixture failed with the shim disarmed");
            return;
        }
    }
    hash_battery(ctx, &now, NULL);

    if (!g_golden.valid)
        return;
#define CMP_U64(f)                                                                                 \
    if (now.f != g_golden.f)                                                                       \
    defect("recovery: %s differs after the failure (%016llx vs %016llx)", #f,                      \
           (unsigned long long)now.f, (unsigned long long)g_golden.f)
    CMP_U64(ahash);
    CMP_U64(dhash);
    CMP_U64(phash);
    CMP_U64(whash_fast);
    CMP_U64(whash_full);
#undef CMP_U64
#define CMP_DIGEST(f)                                                                              \
    if (now.f.size != g_golden.f.size || memcmp(now.f.data, g_golden.f.data, now.f.size) != 0)     \
    defect("recovery: %s digest differs after the failure", #f)
    CMP_DIGEST(bmh);
    CMP_DIGEST(mhash);
    CMP_DIGEST(color);
    CMP_DIGEST(moments);
    CMP_DIGEST(radial);
#undef CMP_DIGEST
}

/* ---- scenarios ---------------------------------------------------------
 * `recording` is set on the single un-injected pass used to count allocations
 * and to capture the reference hashes. */

static void scen_create(int recording) {
    (void)recording;
    ph_context_t *ctx = NULL;
    ph_error_t err = ph_create(&ctx);
    if (err != PH_SUCCESS) {
        check("ph_create", err, ALLOW_ALLOC);
        if (ctx != NULL)
            defect("ph_create failed but still handed back a context pointer");
        return;
    }
    ASSERT_PTR_NOT_NULL(ctx);
    ph_free(ctx);
}

static void scen_load_file(int recording) {
    (void)recording;
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS)
        return;
    ph_error_t err = ph_load_from_file(ctx, PNG_PATH);
    check("ph_load_from_file", err, ALLOW_ALLOC | ALLOW_DECODE);
    if (err != PH_SUCCESS && ph_is_loaded(ctx))
        defect("ph_load_from_file failed but the context reports an image is loaded");
    ph_free(ctx);
}

static void scen_load_memory(int recording) {
    (void)recording;
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS)
        return;
    ph_error_t err = ph_load_from_memory(ctx, g_jpeg.data, g_jpeg.size);
    check("ph_load_from_memory", err, ALLOW_ALLOC | ALLOW_DECODE);
    if (err != PH_SUCCESS && ph_is_loaded(ctx))
        defect("ph_load_from_memory failed but the context reports an image is loaded");
    ph_free(ctx);
}

static void scen_hash_all(int recording) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS)
        return;
    ph_error_t err = ph_load_from_memory(ctx, g_png.data, g_png.size);
    check("ph_load_from_memory", err, ALLOW_ALLOC | ALLOW_DECODE);

    hash_battery(ctx, recording ? &g_golden : NULL, recording ? NULL : &g_golden);
    if (!recording)
        check_recovery(ctx);

    ph_free(ctx);
}

/* Context reuse: several images through one context, which is what exercises
 * arena growth and the grayscale cache being dropped and rebuilt. */
static void scen_batch(int recording) {
    (void)recording;
    static const char *paths[] = {PNG_PATH, JPEG_PATH, WEBP_PATH};
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS)
        return;

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        /* A build without a WebP decoder reports PH_ERR_DECODER_UNAVAILABLE here,
         * which ALLOW_DECODE already covers. */
        ph_error_t err = ph_load_from_file(ctx, paths[i]);
        check("ph_load_from_file(batch)", err, ALLOW_ALLOC | ALLOW_DECODE);

        guarded_u64_t u;
        guarded_digest_t g;
        guard_init(&u, sizeof(u));
        check("ph_compute_ahash(batch)", ph_compute_ahash(ctx, &u.v), ALLOW_ALLOC | ALLOW_INVALID);
        guard_check(u.front, u.back, "ph_compute_ahash(batch)");
        guard_init(&u, sizeof(u));
        check("ph_compute_phash(batch)", ph_compute_phash(ctx, &u.v), ALLOW_ALLOC | ALLOW_INVALID);
        guard_check(u.front, u.back, "ph_compute_phash(batch)");
        guard_init(&g, sizeof(g));
        check("ph_compute_bmh(batch)", ph_compute_bmh(ctx, &g.d), ALLOW_ALLOC | ALLOW_INVALID);
        guard_check(g.front, g.back, "ph_compute_bmh(batch)");
        guard_init(&g, sizeof(g));
        check("ph_compute_radial_hash(batch)", ph_compute_radial_hash(ctx, &g.d),
              ALLOW_ALLOC | ALLOW_INVALID);
        guard_check(g.front, g.back, "ph_compute_radial_hash(batch)");
    }

    ph_free(ctx);
}

typedef void (*scenario_fn)(int recording);

typedef struct {
    const char *name;
    scenario_fn fn;
} scenario_t;

static const scenario_t SCENARIOS[] = {
    {"ph_create", scen_create},
    {"load_from_file", scen_load_file},
    {"load_from_memory", scen_load_memory},
    {"load + every hash", scen_hash_all},
    {"batch over one context", scen_batch},
};

/* ---- driver ------------------------------------------------------------ */

static long run_scenario(const scenario_t *s, long fail_at, int recording) {
    g_scenario = s->name;
    g_fail_at = fail_at;

    ph_shim_arm(fail_at);
    s->fn(recording);
    ph_shim_disarm();

    long leaked = ph_shim_live();
    long count = ph_shim_count();
    if (ph_shim_overflowed())
        defect("allocation tracker overflowed; raise PH_SHIM_SLOTS");
    if (leaked != 0)
        defect("%ld allocation(s) leaked", leaked);
    if (fail_at > 0 && ph_shim_injected() == 0 && fail_at <= count)
        defect("expected to inject a failure at #%ld but never reached it", fail_at);
    ph_shim_reset();
    return count;
}

/* The shim replaces allocator symbols at link time, which only reaches the
 * library when it is statically linked into this binary. */
static int shim_is_effective(void) {
#if !PH_SHIM_SUPPORTED
    return 0;
#else
    ph_shim_arm(0);
    ph_context_t *ctx = NULL;
    ph_error_t err = ph_create(&ctx);
    long seen = ph_shim_count();
    if (err == PH_SUCCESS)
        ph_free(ctx);
    ph_shim_disarm();
    ph_shim_reset();
    return seen > 0;
#endif
}

int main(void) {
    printf("Running allocation-failure tests...\n");

    if (!shim_is_effective()) {
        printf("[SKIP] the allocator shim does not intercept this build "
               "(unsupported libc, or libphash linked as a shared library)\n");
        return 0;
    }

    g_png = read_file(PNG_PATH);
    g_jpeg = read_file(JPEG_PATH);

    long total_points = 0;
    for (size_t i = 0; i < sizeof(SCENARIOS) / sizeof(SCENARIOS[0]); i++) {
        const scenario_t *s = &SCENARIOS[i];

        /* Pass 1: no injection. Counts the allocations and, for the hash
         * scenario, records the reference hashes. */
        long n = run_scenario(s, 0, 1);
        if (n <= 0) {
            fprintf(stderr, "[FAIL] scenario '%s' made no allocations at all\n", s->name);
            g_failures++;
            continue;
        }

        /* Pass 2: fail allocation #k, for every k. */
        for (long k = 1; k <= n; k++)
            (void)run_scenario(s, k, 0);

        total_points += n;
        printf("  %-24s %3ld allocation(s), %3ld failure point(s) exercised\n", s->name, n, n);
    }

    free(g_png.data);
    free(g_jpeg.data);

    if (g_failures != 0) {
        fprintf(stderr, "\n%d problem(s) found across %ld failure points\n", g_failures,
                total_points);
        return 1;
    }
    printf("PASSED: %ld allocation failure points, no crash and no leak\n", total_points);
    return 0;
}
