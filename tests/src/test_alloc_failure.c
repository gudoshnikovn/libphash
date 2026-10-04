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

#define PNG_PATH  TEST_DATA_DIR "/photo_complex.png"
#define JPEG_PATH TEST_DATA_DIR "/photo.jpeg"
#define WEBP_PATH TEST_DATA_DIR "/photo.webp"

/* ---- soft failure reporting -------------------------------------------
 * Failures are collected rather than fatal: one aborted run would hide every
 * other failure point, and the interesting output is the full list. */

static int g_failures = 0;
static const char *g_scenario = "";
static long g_fail_at = 0;

static void defect(const char *fmt, ...) PH_TEST_PRINTF_FORMAT(1, 2);

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

#define ALLOW_ALLOC   0x1 /* PH_ERR_ALLOCATION_FAILED */
#define ALLOW_DECODE  0x2 /* PH_ERR_DECODER_UNAVAILABLE: e.g. WebP with no decoder built in */
#define ALLOW_EMPTY   0x4 /* PH_ERR_EMPTY_IMAGE: the load before the hash failed */
/* PH_ERR_CORRUPT_DATA, but ONLY for the decode checks below that opt into this
 * flag -- NOT a blanket allowance. libwebp reports one specific decode-time
 * allocation failure as VP8_STATUS_BITSTREAM_ERROR, the status WebPDecode() also
 * uses for real corruption, so this wrapper cannot tell the two apart. That is a
 * narrow limitation of libwebp's own error reporting, not a libphash bug, and only
 * the WebP load in scen_batch() needs this flag. The other decoders report an
 * allocation failure precisely -- libpng through ph_png_message_is_oom() in
 * src/loaders/png_libpng.c, libjpeg as JERR_OUT_OF_MEMORY -- so the PNG and JPEG
 * loads do not take it, and a misclassified OOM there fails loudly. */
#define ALLOW_CORRUPT 0x8

static int check(const char *tag, ph_error_t err, int allowed) {
    if (err == PH_SUCCESS) {
        return 1;
    }
    if (err == PH_ERR_ALLOCATION_FAILED && (allowed & ALLOW_ALLOC)) {
        return 0;
    }
    if (err == PH_ERR_DECODER_UNAVAILABLE && (allowed & ALLOW_DECODE)) {
        return 0;
    }
    if (err == PH_ERR_EMPTY_IMAGE && (allowed & ALLOW_EMPTY)) {
        return 0;
    }
    if (err == PH_ERR_CORRUPT_DATA && (allowed & ALLOW_CORRUPT)) {
        return 0;
    }
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
    int allowed = ALLOW_ALLOC | ALLOW_EMPTY;

#define U64_HASH(call, field, tag)                                                     \
    do {                                                                               \
        guard_init(&u, sizeof(u));                                                     \
        u.v = 0;                                                                       \
        int ok = check(tag, call(ctx, &u.v), allowed);                                 \
        guard_check(u.front, u.back, tag);                                             \
        if (ok && out)                                                                 \
            out->field = u.v;                                                          \
        if (ok && ref && ref->valid && u.v != ref->field)                              \
            defect("%s reported success but returned %016llx instead of %016llx", tag, \
                   (unsigned long long)u.v, (unsigned long long)ref->field);           \
    } while (0)

#define DIGEST_HASH(call, field, tag)                                                          \
    do {                                                                                       \
        guard_init(&g, sizeof(g));                                                             \
        memset(&g.d, 0, sizeof(g.d));                                                          \
        int ok = check(tag, call(ctx, &g.d), allowed);                                         \
        guard_check(g.front, g.back, tag);                                                     \
        if (ok && out)                                                                         \
            out->field = g.d;                                                                  \
        if (ok && ref && ref->valid &&                                                         \
            (g.d.size != ref->field.size || memcmp(g.d.data, ref->field.data, g.d.size) != 0)) \
            defect("%s reported success but returned a digest differing from the "             \
                   "reference",                                                                \
                   tag);                                                                       \
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

    if (out) {
        out->valid = 1;
    }
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

    if (!g_golden.valid) {
        return;
    }
#define CMP_U64(f)                                                            \
    if (now.f != g_golden.f)                                                  \
    defect("recovery: %s differs after the failure (%016llx vs %016llx)", #f, \
           (unsigned long long)now.f, (unsigned long long)g_golden.f)
    CMP_U64(ahash);
    CMP_U64(dhash);
    CMP_U64(phash);
    CMP_U64(whash_fast);
    CMP_U64(whash_full);
#undef CMP_U64
#define CMP_DIGEST(f)                                                                          \
    if (now.f.size != g_golden.f.size || memcmp(now.f.data, g_golden.f.data, now.f.size) != 0) \
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
        if (ctx != NULL) {
            defect("ph_create failed but still handed back a context pointer");
        }
        return;
    }
    ASSERT_PTR_NOT_NULL(ctx);
    ph_free(ctx);
}

static void scen_load_file(int recording) {
    (void)recording;
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return;
    }
    ph_error_t err = ph_load_from_file(ctx, PNG_PATH);
    check("ph_load_from_file", err, ALLOW_ALLOC | ALLOW_DECODE);
    if (err != PH_SUCCESS && ph_is_loaded(ctx)) {
        defect("ph_load_from_file failed but the context reports an image is loaded");
    }
    ph_free(ctx);
}

static void scen_load_memory(int recording) {
    (void)recording;
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return;
    }
    ph_error_t err = ph_load_from_memory(ctx, g_jpeg.data, g_jpeg.size);
    check("ph_load_from_memory", err, ALLOW_ALLOC | ALLOW_DECODE);
    if (err != PH_SUCCESS && ph_is_loaded(ctx)) {
        defect("ph_load_from_memory failed but the context reports an image is loaded");
    }
    ph_free(ctx);
}

static void scen_hash_all(int recording) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return;
    }
    ph_error_t err = ph_load_from_memory(ctx, g_png.data, g_png.size);
    check("ph_load_from_memory", err, ALLOW_ALLOC | ALLOW_DECODE);

    hash_battery(ctx, recording ? &g_golden : NULL, recording ? NULL : &g_golden);
    if (!recording) {
        check_recovery(ctx);
    }

    ph_free(ctx);
}

/* Context reuse: several images through one context, which is what exercises
 * arena growth and the grayscale cache being dropped and rebuilt. */
static void scen_batch(int recording) {
    (void)recording;
    static const char *paths[] = {PNG_PATH, JPEG_PATH, WEBP_PATH};
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return;
    }

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        /* A build without a WebP decoder reports PH_ERR_DECODER_UNAVAILABLE here,
         * which ALLOW_DECODE already covers. */
        ph_error_t err = ph_load_from_file(ctx, paths[i]);
        check("ph_load_from_file(batch)", err, ALLOW_ALLOC | ALLOW_DECODE | ALLOW_CORRUPT);

        guarded_u64_t u;
        guarded_digest_t g;
        guard_init(&u, sizeof(u));
        check("ph_compute_ahash(batch)", ph_compute_ahash(ctx, &u.v), ALLOW_ALLOC | ALLOW_EMPTY);
        guard_check(u.front, u.back, "ph_compute_ahash(batch)");
        guard_init(&u, sizeof(u));
        check("ph_compute_phash(batch)", ph_compute_phash(ctx, &u.v), ALLOW_ALLOC | ALLOW_EMPTY);
        guard_check(u.front, u.back, "ph_compute_phash(batch)");
        guard_init(&g, sizeof(g));
        check("ph_compute_bmh(batch)", ph_compute_bmh(ctx, &g.d), ALLOW_ALLOC | ALLOW_EMPTY);
        guard_check(g.front, g.back, "ph_compute_bmh(batch)");
        guard_init(&g, sizeof(g));
        check("ph_compute_radial_hash(batch)", ph_compute_radial_hash(ctx, &g.d),
              ALLOW_ALLOC | ALLOW_EMPTY);
        guard_check(g.front, g.back, "ph_compute_radial_hash(batch)");
    }

    ph_free(ctx);
}

/* A photo stored in sensor orientation with an EXIF tag saying "rotate 90°" (6), loaded
 * with auto-orientation on (the default). The rotation takes a second full-size buffer;
 * if that allocation fails, the load must fail, not succeed with an unrotated image whose
 * every hash describes an orientation the caller never asked for. Rule: a load that
 * reports success hashes exactly like the un-injected one. */
static blob_t g_oriented;
static uint64_t g_oriented_ahash, g_oriented_phash;

static void build_oriented_jpeg(void) {
    /* APP1 "Exif", little-endian TIFF, IFD0 with one SHORT Orientation entry = 6. */
    static const uint8_t app1[] = {
        0xFF, 0xE1, 0x00, 0x22, 'E',  'x',  'i',  'f',  0x00, 0x00, 'I',  'I',
        0x2A, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x12, 0x01, 0x03, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    g_oriented.size = g_jpeg.size + sizeof(app1);
    g_oriented.data = (uint8_t *)malloc(g_oriented.size);
    ASSERT_PTR_NOT_NULL(g_oriented.data);
    memcpy(g_oriented.data, g_jpeg.data, 2); /* SOI */
    memcpy(g_oriented.data + 2, app1, sizeof(app1));
    memcpy(g_oriented.data + 2 + sizeof(app1), g_jpeg.data + 2, g_jpeg.size - 2);
}

static void scen_load_oriented(int recording) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return;
    }
    ph_error_t err = ph_load_from_memory(ctx, g_oriented.data, g_oriented.size);
    check("ph_load_from_memory(oriented)", err, ALLOW_ALLOC | ALLOW_DECODE);
    if (err != PH_SUCCESS) {
        if (ph_is_loaded(ctx)) {
            defect("the oriented load failed but the context reports an image is loaded");
        }
        ph_free(ctx);
        return;
    }

    uint64_t ahash = 0, phash = 0;
    ph_shim_disarm(); /* the load is what is under test; the hashes must not fail */
    ph_error_t ea = ph_compute_ahash(ctx, &ahash);
    ph_error_t ep = ph_compute_phash(ctx, &phash);
    if (ea != PH_SUCCESS || ep != PH_SUCCESS) {
        defect("hashing the oriented image failed with the shim disarmed (%d, %d)", (int)ea,
               (int)ep);
    } else if (recording) {
        g_oriented_ahash = ahash;
        g_oriented_phash = phash;
    } else if (ahash != g_oriented_ahash || phash != g_oriented_phash) {
        defect("the oriented load reported success but hashes differently from the reference "
               "(aHash %016llx vs %016llx) -- the orientation was not applied",
               (unsigned long long)ahash, (unsigned long long)g_oriented_ahash);
    }
    ph_free(ctx);
}

/* ---- one algorithm on a fresh context -----------------------------------
 * The battery above runs every algorithm on one context, so the grey cache that
 * ph_get_gray() builds lazily is allocated by the first algorithm only, and the
 * allocation-failure branch after ph_get_gray() in every later one is unreachable from
 * it. Here each algorithm gets its own context, the load runs with the shim disarmed,
 * and the sweep covers the allocations of that one call. */

typedef struct {
    const char *name;
    ph_error_t (*u64)(ph_context_t *, uint64_t *);
    ph_error_t (*digest)(ph_context_t *, ph_digest_t *);
    ph_whash_mode_t whash_mode;
    int allocates; /* 0: works on the decoded pixels in place, nothing to sweep */
} algo_t;

static const algo_t ALGOS[] = {
    {"aHash", ph_compute_ahash, NULL, PH_WHASH_FAST, 1},
    {"dHash", ph_compute_dhash, NULL, PH_WHASH_FAST, 1},
    {"pHash", ph_compute_phash, NULL, PH_WHASH_FAST, 1},
    {"wHash fast", ph_compute_whash, NULL, PH_WHASH_FAST, 1},
    {"wHash full", ph_compute_whash, NULL, PH_WHASH_FULL, 1},
    {"BMH", NULL, ph_compute_bmh, PH_WHASH_FAST, 1},
    {"mHash", NULL, ph_compute_mhash, PH_WHASH_FAST, 1},
    {"Radial", NULL, ph_compute_radial_hash, PH_WHASH_FAST, 1},
    {"ColorHash", NULL, ph_compute_color_hash, PH_WHASH_FAST, 0},
    {"ColorMoments", NULL, ph_compute_color_moments_hash, PH_WHASH_FAST, 0},
};
#define NUM_ALGOS (sizeof(ALGOS) / sizeof(ALGOS[0]))

static const algo_t *g_algo;
static uint64_t g_algo_ref_u64[NUM_ALGOS];
static ph_digest_t g_algo_ref_digest[NUM_ALGOS];

static int run_algo(ph_context_t *ctx, const algo_t *a, uint64_t *u, ph_digest_t *d) {
    guarded_u64_t gu;
    guarded_digest_t gd;
    ph_error_t err;
    if (a->u64) {
        guard_init(&gu, sizeof(gu));
        err = a->u64(ctx, &gu.v);
        guard_check(gu.front, gu.back, a->name);
        *u = gu.v;
    } else {
        guard_init(&gd, sizeof(gd));
        memset(&gd.d, 0, sizeof(gd.d));
        err = a->digest(ctx, &gd.d);
        guard_check(gd.front, gd.back, a->name);
        *d = gd.d;
    }
    return check(a->name, err, ALLOW_ALLOC);
}

static int same_as_reference(size_t i, uint64_t u, const ph_digest_t *d) {
    if (ALGOS[i].u64) {
        return u == g_algo_ref_u64[i];
    }
    return d->size == g_algo_ref_digest[i].size &&
           memcmp(d->data, g_algo_ref_digest[i].data, d->size) == 0;
}

static void scen_one_hash(int recording) {
    const size_t i = (size_t)(g_algo - ALGOS);
    ph_context_t *ctx = NULL;

    /* Set-up outside the sweep: re-arming resets the counter, so only the hash call's
     * allocations are counted and failed. */
    ph_shim_disarm();
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_context_set_whash_mode(ctx, g_algo->whash_mode));
    ASSERT_OK(ph_load_from_memory(ctx, g_png.data, g_png.size));
    ph_shim_arm(g_fail_at);

    uint64_t u = 0;
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    int ok = run_algo(ctx, g_algo, &u, &d);
    if (recording) {
        if (!ok) {
            defect("%s failed with nothing injected", g_algo->name);
        }
        g_algo_ref_u64[i] = u;
        g_algo_ref_digest[i] = d;
    } else if (ok && !same_as_reference(i, u, &d)) {
        defect("%s reported success but differs from the un-injected result", g_algo->name);
    }

    /* The context survives: the same call, nothing injected, gives the reference. */
    ph_shim_disarm();
    if (!recording) {
        ok = run_algo(ctx, g_algo, &u, &d);
        if (!ok || !same_as_reference(i, u, &d)) {
            defect("%s: the same context does not give the reference after the failure",
                   g_algo->name);
        }
    }
    ph_free(ctx);
}

/* ---- other loads -------------------------------------------------------- */

static uint64_t g_pixels_ahash, g_scaled_ahash;

/* ph_load_from_pixels() is the one load that does not go through the decoder. */
static void scen_load_pixels(int recording) {
    static uint8_t rgb[48 * 32 * 3];
    for (size_t i = 0; i < sizeof(rgb); i++) {
        rgb[i] = (uint8_t)((i * 7u) ^ (i >> 5));
    }
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return;
    }
    ph_error_t err = ph_load_from_pixels(ctx, rgb, 48, 32, 3, 48 * 3);
    check("ph_load_from_pixels", err, ALLOW_ALLOC);
    if (err == PH_SUCCESS) {
        uint64_t ahash = 0;
        ph_shim_disarm();
        ASSERT_OK(ph_compute_ahash(ctx, &ahash));
        if (recording) {
            g_pixels_ahash = ahash;
        } else if (ahash != g_pixels_ahash) {
            defect("ph_load_from_pixels reported success but the image hashes differently");
        }
    } else if (ph_is_loaded(ctx)) {
        defect("ph_load_from_pixels failed but the context reports an image is loaded");
    }
    ph_free(ctx);
}

/* A grey PNG loaded for colour: the decoder output is expanded from one channel to three,
 * which in the stb_image path takes a second buffer. */
static void scen_load_gray_as_colour(int recording) {
    (void)recording;
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return;
    }
    ASSERT_OK(ph_context_set_load_grayscale(ctx, 0));
    ph_error_t err = ph_load_from_file(ctx, TEST_DATA_DIR "/png/gray8.png");
    check("ph_load_from_file(grey PNG as colour)", err, ALLOW_ALLOC);
    if (err != PH_SUCCESS && ph_is_loaded(ctx)) {
        defect("the grey-as-colour load failed but the context reports an image is loaded");
    }
    ph_free(ctx);
}

/* The same expansion for a grey PGM, which stb_image decodes in every build. */
static void scen_load_pgm_as_colour(int recording) {
    (void)recording;
    static const uint8_t pgm[] = "P5\n4 3\n255\n\x10\x20\x30\x40\x50\x60\x70\x80\x90\xa0\xb0\xc0";
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return;
    }
    ASSERT_OK(ph_context_set_load_grayscale(ctx, 0));
    ph_error_t err = ph_load_from_memory(ctx, pgm, sizeof(pgm) - 1);
    check("ph_load_from_memory(grey PGM as colour)", err, ALLOW_ALLOC);
    if (err != PH_SUCCESS && ph_is_loaded(ctx)) {
        defect("the PGM load failed but the context reports an image is loaded");
    }
    ph_free(ctx);
}

/* Decoding at reduced resolution takes its own allocation path in the JPEG backend. */
static void scen_load_scaled(int recording) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return;
    }
    ASSERT_OK(ph_context_set_decode_scale(ctx, PH_DECODE_SCALE_HALF));
    ph_error_t err = ph_load_from_memory(ctx, g_jpeg.data, g_jpeg.size);
    check("ph_load_from_memory(decode scale 1/2)", err, ALLOW_ALLOC | ALLOW_DECODE);
    if (err == PH_SUCCESS) {
        uint64_t ahash = 0;
        ph_shim_disarm();
        ASSERT_OK(ph_compute_ahash(ctx, &ahash));
        if (recording) {
            g_scaled_ahash = ahash;
        } else if (ahash != g_scaled_ahash) {
            defect("the scaled load reported success but the image hashes differently");
        }
    } else if (ph_is_loaded(ctx)) {
        defect("the scaled load failed but the context reports an image is loaded");
    }
    ph_free(ctx);
}

/* ---- the batch API -----------------------------------------------------
 * Sequential (threads = 1): the shim's counter is not thread-safe, and with one
 * thread the allocation order is deterministic. The threaded path's own allocation is
 * covered by test_threaded_batch_thread_array_oom() below. */

#define BATCH_FLAGS (PH_HASH_AHASH | PH_HASH_PHASH)
static uint64_t g_batch_ref[2][2];

static void scen_batch_api(int recording) {
    ph_batch_buffer_item_t items[2];
    memset(items, 0, sizeof(items));
    items[0].buffer = g_png.data;
    items[0].length = g_png.size;
    items[1].buffer = g_jpeg.data;
    items[1].length = g_jpeg.size;

    ph_error_t err = ph_hash_buffers(items, 2, BATCH_FLAGS, 1);
    check("ph_hash_buffers", err, ALLOW_ALLOC);
    for (int i = 0; i < 2; i++) {
        if (err != PH_SUCCESS) {
            if (items[i].status != PH_ERR_ALLOCATION_FAILED) {
                defect("ph_hash_buffers failed but item %d has status %d", i, items[i].status);
            }
            continue;
        }
        if (!check("ph_hash_buffers item", items[i].status, ALLOW_ALLOC | ALLOW_DECODE) ||
            items[i].status != PH_SUCCESS) {
            continue;
        }
        if (recording) {
            g_batch_ref[i][0] = items[i].hashes[0];
            g_batch_ref[i][1] = items[i].hashes[1];
        } else if (items[i].hashes[0] != g_batch_ref[i][0] ||
                   items[i].hashes[1] != g_batch_ref[i][1]) {
            defect("batch item %d reported success but its hashes differ", i);
        }
    }
}

/* The first allocation of a threaded batch is the thread array, made on the calling
 * thread before any worker exists -- so it can be failed deterministically. The call
 * fails and every item says so. */
static void test_threaded_batch_thread_array_oom(void) {
#if defined(PH_ENABLE_THREADS)
    g_scenario = "threaded batch, thread array";
    g_fail_at = 1;
    ph_batch_buffer_item_t items[2];
    memset(items, 0, sizeof(items));
    items[0].buffer = items[1].buffer = g_png.data;
    items[0].length = items[1].length = g_png.size;

    ph_shim_arm(1);
    ph_error_t err = ph_hash_buffers(items, 2, BATCH_FLAGS, 2);
    ph_shim_disarm();
    if (ph_shim_live() != 0) {
        defect("%ld allocation(s) leaked", ph_shim_live());
    }
    ph_shim_reset();
    if (err != PH_ERR_ALLOCATION_FAILED) {
        defect("ph_hash_buffers(threads=2) returned %d with its first allocation failed", (int)err);
    }
    for (int i = 0; i < 2; i++) {
        if (items[i].status != PH_ERR_ALLOCATION_FAILED) {
            defect("item %d has status %d after the batch failed", i, items[i].status);
        }
    }
    printf("  %-24s checked\n", "threaded batch, OOM");
#endif
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
    {"load, EXIF orientation", scen_load_oriented},
    {"load_from_pixels", scen_load_pixels},
    {"load, grey PNG as colour", scen_load_gray_as_colour},
    {"load, grey PGM as colour", scen_load_pgm_as_colour},
    {"load, decode scale 1/2", scen_load_scaled},
    {"batch API, sequential", scen_batch_api},
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
    if (ph_shim_overflowed()) {
        defect("allocation tracker overflowed; raise PH_SHIM_SLOTS");
    }
    if (leaked != 0) {
        defect("%ld allocation(s) leaked", leaked);
    }
    if (fail_at > 0 && ph_shim_injected() == 0 && fail_at <= count) {
        defect("expected to inject a failure at #%ld but never reached it", fail_at);
    }
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
    if (err == PH_SUCCESS) {
        ph_free(ctx);
    }
    ph_shim_disarm();
    ph_shim_reset();
    return seen > 0;
#endif
}

/* ---- stb "outofmem" pinning --------------------------------------------
 *
 * The scenarios above cover stb_image's out-of-memory path end to end: src/loader.c maps
 * stb_image's "outofmem" failure reason to PH_ERR_ALLOCATION_FAILED, so every one of
 * their failure points that lands inside stb_image's decode is accepted by check()'s
 * ALLOW_ALLOC.
 *
 * That end-to-end result is not proof that the "outofmem" mapping itself works: a
 * differently-worded stb reason falling through to the PH_ERR_CORRUPT_DATA branch
 * would look identical from here if some *other* allocation on the same call path
 * happened to fail with PH_ERR_ALLOCATION_FAILED instead -- the scenario would still
 * pass, for the wrong reason, and a real regression in ph_stb_reason_is_oom() would
 * go unnoticed. This test checks the diagnostic message itself:
 * it sweeps every allocation ordinal a clean JPEG decode makes and requires that at
 * least one of the resulting failures leaves the literal reason "outofmem" behind
 * (not merely a non-empty message), pinned via ph_get_last_error_message(). If a
 * vendored stb_image update rewords that reason, ph_stb_reason_is_oom() in
 * src/loader.c stops recognizing it and this assertion fails -- which is the point:
 * see also test_stb_failure_classification() in tests/src/test_loader.c, which pins
 * the sibling "unknown image type" mapping the same way. */
static void test_stb_oom_reason_pinned(void) {
    g_scenario = "stb outofmem reason";

    /* This pins ph_stb_reason_is_oom(), which only matters on the stb_image decode
     * path -- but JPEG only takes that path when no native JPEG backend is compiled
     * in. On a libjpeg-turbo build (ph_can_use_jpeg() == 1) g_jpeg decodes through
     * ph_decode_jpeg_mem() instead, which never produces stb's "outofmem" reason at
     * all -- every injected allocation failure there surfaces libjpeg's own
     * "Insufficient memory (case N)" message with PH_ERR_ALLOCATION_FAILED,
     * so the sweep below would legitimately never see "outofmem" and this assertion
     * would fail for a reason that has nothing to do with ph_stb_reason_is_oom()
     * being stale. Skip rather than assert something this build cannot exercise. */
    if (ph_can_use_jpeg()) {
        printf("  %-24s SKIPPED (libjpeg-turbo compiled in -- JPEG doesn't take the stb "
               "decode path this pins)\n",
               "stb oom pinning");
        return;
    }

    ph_shim_arm(0);
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        defect("could not create a context for the baseline pass");
        ph_shim_disarm();
        ph_shim_reset();
        return;
    }
    ph_error_t baseline = ph_load_from_memory(ctx, g_jpeg.data, g_jpeg.size);
    ph_free(ctx);
    ph_shim_disarm();
    long n = ph_shim_count();
    ph_shim_reset();

    if (baseline != PH_SUCCESS || n <= 0) {
        defect("could not establish a clean baseline load to sweep allocations over");
        return;
    }

    int saw_outofmem = 0;
    for (long k = 1; k <= n; k++) {
        g_fail_at = k;
        ph_shim_arm(k);
        ctx = NULL;
        if (ph_create(&ctx) != PH_SUCCESS) {
            ph_shim_disarm();
            ph_shim_reset();
            continue;
        }
        ph_error_t err = ph_load_from_memory(ctx, g_jpeg.data, g_jpeg.size);
        if (err == PH_ERR_ALLOCATION_FAILED &&
            strcmp(ph_get_last_error_message(ctx), "outofmem") == 0) {
            saw_outofmem = 1;
        }
        ph_free(ctx);
        ph_shim_disarm();
        ph_shim_reset();
    }

    if (!saw_outofmem) {
        defect("no injected allocation failure reproduced stb_image's literal "
               "\"outofmem\" reason over %ld failure point(s) -- ph_stb_reason_is_oom() "
               "may be stale against the vendored stb_image.h",
               n);
    } else {
        printf("  %-24s stb \"outofmem\" reason reproduced and mapped to "
               "PH_ERR_ALLOCATION_FAILED\n",
               "stb oom pinning");
    }
}

/* Pass 1: no injection -- counts the allocations and records the reference results.
 * Pass 2: fail allocation #k, for every k. Returns the number of failure points. */
static long sweep(const scenario_t *s) {
    long n = run_scenario(s, 0, 1);
    if (n <= 0) {
        fprintf(stderr, "[FAIL] scenario '%s' made no allocations at all\n", s->name);
        g_failures++;
        return 0;
    }
    for (long k = 1; k <= n; k++) {
        (void)run_scenario(s, k, 0);
    }
    printf("  %-24s %3ld allocation(s), %3ld failure point(s) exercised\n", s->name, n, n);
    return n;
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
    build_oriented_jpeg();

    test_stb_oom_reason_pinned();

    test_threaded_batch_thread_array_oom();

    long total_points = 0;
    for (size_t i = 0; i < sizeof(SCENARIOS) / sizeof(SCENARIOS[0]); i++) {
        total_points += sweep(&SCENARIOS[i]);
    }
    for (size_t i = 0; i < NUM_ALGOS; i++) {
        char name[48];
        snprintf(name, sizeof(name), "%s alone", ALGOS[i].name);
        const scenario_t s = {name, scen_one_hash};
        g_algo = &ALGOS[i];
        if (ALGOS[i].allocates) {
            total_points += sweep(&s);
        } else if (run_scenario(&s, 0, 1) != 0) {
            /* Pinned so that an allocation added later is swept, not silently skipped. */
            fprintf(stderr, "[FAIL] %s allocates; set `allocates` in ALGOS\n", name);
            g_failures++;
        }
    }

    free(g_png.data);
    free(g_jpeg.data);
    free(g_oriented.data);

    if (g_failures != 0) {
        fprintf(stderr, "\n%d problem(s) found across %ld failure points\n", g_failures,
                total_points);
        return 1;
    }
    printf("PASSED: %ld allocation failure points, no crash and no leak\n", total_points);
    return 0;
}
