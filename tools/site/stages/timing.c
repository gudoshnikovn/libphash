/* site_stages: the times on the site's Cost rows and tables (tools/site/measure/timing.py). */

/* clock_gettime() and CLOCK_MONOTONIC are POSIX, not ISO C; the project compiles as strict
 * ISO C, under which glibc declares them only when asked. Darwin uses mach_absolute_time()
 * instead (below), which _POSIX_C_SOURCE would hide. Must precede every #include. */
#if !defined(__APPLE__) && !defined(_WIN32)
#    define _POSIX_C_SOURCE 200809L
#endif

#include "batch.h"

#include "stages.h"

#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#ifdef __APPLE__
#    include <mach/mach_time.h>
#endif

/* --8<-- [start:time] */
/* Seconds on a monotonic clock, as tests/src/bench_hash.c reads it. The calendar clock
 * (timespec_get() with TIME_UTC) would step back when the system corrects its time, and a
 * run spanning the step would come out negative and become the minimum. On Darwin,
 * CLOCK_MONOTONIC counts whole microseconds, 2 % of the cheapest hash; mach_absolute_time()
 * counts nanoseconds. */
static double seconds(void) {
#ifdef __APPLE__
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) {
        mach_timebase_info(&tb);
    }
    return (double)mach_absolute_time() * tb.numer / tb.denom * 1e-9;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

/* One timed case: `run` does the work once on a loaded image and returns nonzero on
 * failure; `arg` selects a variant of it. A case named after an algorithm, or starting
 * with its name and "_", is shown on that algorithm's page. */
typedef struct {
    const char *name;
    int (*run)(ph_context_t *ctx, const char *path, int arg);
    int arg;
} time_case_t;

static int time_decode(ph_context_t *ctx, const char *path, int arg) {
    (void)arg;
    return ph_load_from_file(ctx, path) != PH_SUCCESS;
}

/* The first hash computed on a loaded image, which is what a caller who loads an image
 * and hashes it pays: the grayscale conversion and the shared area grid are dropped
 * before every run, so each run computes them (the dropping is untimed). */
static int time_hash(ph_context_t *ctx, const char *path, int algo) {
    (void)path;
    ph_digest_t d;
    return ph_compute_digest(ctx, (ph_algorithm_t)algo, &d) != PH_SUCCESS;
}

/* The four 64-bit hashes in one ph_compute_multi() call, which computes the grayscale
 * image and the area grid once for all of them. */
static int time_multi(ph_context_t *ctx, const char *path, int arg) {
    (void)path;
    (void)arg;
    uint64_t h[PH_HASH_FLAGS_COUNT];
    return ph_compute_multi(ctx, PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH,
                            h) != PH_SUCCESS;
}

static int time_whash_full(ph_context_t *ctx, const char *path, int arg) {
    (void)path;
    (void)arg;
    uint64_t h;
    int bad = ph_context_set_whash_mode(ctx, PH_WHASH_FULL) != PH_SUCCESS ||
              ph_compute_whash(ctx, &h) != PH_SUCCESS;
    return ph_context_set_whash_mode(ctx, PH_WHASH_FAST) != PH_SUCCESS || bad;
}

static int time_mhash_size(ph_context_t *ctx, const char *path, int size) {
    (void)path;
    ph_digest_t d;
    int bad = ph_context_set_mhash_params(ctx, PH_MH_ALPHA, PH_MH_LEVEL, size) != PH_SUCCESS ||
              ph_compute_mhash(ctx, &d) != PH_SUCCESS;
    return ph_context_set_mhash_params(ctx, PH_MH_ALPHA, PH_MH_LEVEL, PH_MH_IMAGE_SIZE) !=
               PH_SUCCESS ||
           bad;
}

/* pHash with a dct_size other than the default, and the default block. */
static int time_phash_dct(ph_context_t *ctx, const char *path, int size) {
    (void)path;
    uint64_t h;
    int bad = ph_context_set_phash_params(ctx, size, PH_DCT_REDUCTION_SIZE) != PH_SUCCESS ||
              ph_compute_phash(ctx, &h) != PH_SUCCESS;
    return ph_context_set_phash_params(ctx, PH_DCT_SIZE, PH_DCT_REDUCTION_SIZE) != PH_SUCCESS ||
           bad;
}

/* Radial with the settings of radial_variants[variant]. */
static int time_radial(ph_context_t *ctx, const char *path, int variant) {
    (void)path;
    ph_digest_t d;
    int bad = !radial_settings(ctx, &radial_variants[variant]) ||
              ph_compute_radial_hash(ctx, &d) != PH_SUCCESS;
    return !radial_settings(ctx, &radial_variants[RADIAL_DEFAULT]) || bad;
}

/* Loading the image, then, unless `algo` is LOAD_ONLY, computing that algorithm: what a
 * caller pays for a load and a hash. `arg` packs the load -- a ph_decode_scale_t, plus
 * LOAD_GRAY for ph_context_set_load_grayscale() and LOAD_FRESH for a context created and
 * freed around the load, as a caller who creates one per image does -- and the algorithm
 * above it. Without LOAD_FRESH, a context of its own per kind of load, kept across runs,
 * so the cases after it hash the image as it was first loaded. */
enum {
    LOAD_GRAY = 4,
    LOAD_FRESH = 8,
    LOAD_ALGO = 16,
    LOAD_ONLY = -1,
};

static int time_load(ph_context_t *ctx, const char *path, int arg) {
    (void)ctx;
    static ph_context_t *kept[LOAD_FRESH];
    int kind = arg % LOAD_FRESH, fresh = arg & LOAD_FRESH, algo = arg / LOAD_ALGO - 1;
    ph_context_t *c = fresh ? NULL : kept[kind];
    if (!c && ph_create(&c) != PH_SUCCESS) {
        return 1;
    }
    ph_digest_t d;
    int bad = ph_context_set_decode_scale(c, (ph_decode_scale_t)(kind & 3)) != PH_SUCCESS ||
              ph_context_set_load_grayscale(c, (kind & LOAD_GRAY) != 0) != PH_SUCCESS ||
              ph_load_from_file(c, path) != PH_SUCCESS ||
              (algo != LOAD_ONLY && ph_compute_digest(c, (ph_algorithm_t)algo, &d) != PH_SUCCESS);
    if (fresh) {
        ph_free(c);
    } else {
        kept[kind] = c;
    }
    return bad;
}

#define LOADED(name, flags, algo) {name, time_load, (flags) + ((algo) + 1) * LOAD_ALGO}

static const time_case_t time_cases[] = {
    {"decode", time_decode, 0},
    {"ahash", time_hash, PH_ALGO_AHASH},
    {"dhash", time_hash, PH_ALGO_DHASH},
    {"phash", time_hash, PH_ALGO_PHASH},
    {"whash", time_hash, PH_ALGO_WHASH},
    {"whash_full", time_whash_full, 0},
    {"bmh", time_hash, PH_ALGO_BMH},
    {"mhash", time_hash, PH_ALGO_MHASH},
    {"radial", time_hash, PH_ALGO_RADIAL},
    {"color_hash", time_hash, PH_ALGO_COLOR_HASH},
    {"color_moments", time_hash, PH_ALGO_COLOR_MOMENTS},
    {"multi", time_multi, 0},
    {"mhash_size_62", time_mhash_size, 62},
    {"mhash_size_128", time_mhash_size, 128},
    {"mhash_size_256", time_mhash_size, 256},
    {"mhash_size_512", time_mhash_size, 512},
    {"mhash_size_1024", time_mhash_size, 1024},
    {"mhash_size_2048", time_mhash_size, 2048},
    {"mhash_size_4096", time_mhash_size, 4096},
    {"phash_dct_8", time_phash_dct, 8},
    {"phash_dct_16", time_phash_dct, 16},
    {"phash_dct_24", time_phash_dct, 24},
    {"radial_sigma_1", time_radial, RADIAL_SIGMA_1},
    {"radial_sigma_8", time_radial, RADIAL_SIGMA_8},
    {"radial_gamma_2", time_radial, RADIAL_GAMMA_2},
    {"radial_grid_40x32", time_radial, RADIAL_GRID_40X32},
    {"radial_grid_90x64", time_radial, RADIAL_GRID_90X64},
    {"radial_grid_360x256", time_radial, RADIAL_GRID_360X256},
    {"radial_grid_1440x1024", time_radial, RADIAL_GRID_1440X1024},
    {"radial_grid_4096x4096", time_radial, RADIAL_GRID_4096X4096},
    LOADED("scale_half_decode", PH_DECODE_SCALE_HALF, LOAD_ONLY),
    LOADED("scale_quarter_decode", PH_DECODE_SCALE_QUARTER, LOAD_ONLY),
    LOADED("scale_eighth_decode", PH_DECODE_SCALE_EIGHTH, LOAD_ONLY),
    LOADED("scale_full_phash", PH_DECODE_SCALE_FULL, PH_ALGO_PHASH),
    LOADED("scale_half_phash", PH_DECODE_SCALE_HALF, PH_ALGO_PHASH),
    LOADED("scale_quarter_phash", PH_DECODE_SCALE_QUARTER, PH_ALGO_PHASH),
    LOADED("scale_eighth_phash", PH_DECODE_SCALE_EIGHTH, PH_ALGO_PHASH),
    LOADED("scale_full_mhash", PH_DECODE_SCALE_FULL, PH_ALGO_MHASH),
    LOADED("scale_half_mhash", PH_DECODE_SCALE_HALF, PH_ALGO_MHASH),
    LOADED("scale_quarter_mhash", PH_DECODE_SCALE_QUARTER, PH_ALGO_MHASH),
    LOADED("scale_eighth_mhash", PH_DECODE_SCALE_EIGHTH, PH_ALGO_MHASH),
    LOADED("scale_full_radial", PH_DECODE_SCALE_FULL, PH_ALGO_RADIAL),
    LOADED("scale_half_radial", PH_DECODE_SCALE_HALF, PH_ALGO_RADIAL),
    LOADED("scale_quarter_radial", PH_DECODE_SCALE_QUARTER, PH_ALGO_RADIAL),
    LOADED("scale_eighth_radial", PH_DECODE_SCALE_EIGHTH, PH_ALGO_RADIAL),
    LOADED("gray_decode", LOAD_GRAY, LOAD_ONLY),
    LOADED("gray_phash", LOAD_GRAY, PH_ALGO_PHASH),
    LOADED("gray_mhash", LOAD_GRAY, PH_ALGO_MHASH),
    LOADED("gray_radial", LOAD_GRAY, PH_ALGO_RADIAL),
    LOADED("fresh_decode", LOAD_FRESH, LOAD_ONLY),
    LOADED("fresh_phash", LOAD_FRESH, PH_ALGO_PHASH),
};

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* One warm-up run, then at least TIME_MIN_RUNS runs and TIME_MIN_SECONDS, at most
 * TIME_MAX_RUNS; the minimum is what the pages quote. */
enum {
    TIME_MIN_RUNS = 5,
    TIME_MAX_RUNS = 300,
};

#define TIME_MIN_SECONDS 1.0

/* Whether case `name` is among the `n` names of `only`; every case is when `n` is 0. */
static int time_chosen(const char *name, int n, char **only) {
    for (int i = 0; i < n; i++) {
        if (strcmp(name, only[i]) == 0) {
            return 1;
        }
    }
    return n == 0;
}

/* site_stages time <image> [case...]: every case of time_cases[] on the image, or the cases
 * named; the minimum and the median of its runs, in milliseconds, as one JSON object, with
 * the build that ran them. */
int mode_time(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        int known = 0;
        for (size_t c = 0; c < COUNT(time_cases); c++) {
            known |= strcmp(argv[i], time_cases[c].name) == 0;
        }
        if (!known) {
            return fail("no such timed case", argv[i]);
        }
    }
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[0])) {
        return 1;
    }
    json_t j = json_begin(stdout);
    json_int(&j, "width", ctx->image.width);
    json_int(&j, "height", ctx->image.height);
    json_string(&j, "build_info", ph_get_build_info());
#ifdef __VERSION__
    json_string(&j, "compiler", __VERSION__);
#endif
    json_t cases = json_object(&j, "cases");
    static double ms[TIME_MAX_RUNS];
    for (size_t c = 0; c < COUNT(time_cases); c++) {
        const time_case_t *tc = &time_cases[c];
        if (!time_chosen(tc->name, argc - 1, argv + 1)) {
            continue;
        }
        int runs = 0;
        double total = 0.0;
        for (int k = -1; k < TIME_MAX_RUNS; k++) { /* k = -1: the warm-up */
            ph_drop_gray_cache(ctx);
            double t0 = seconds();
            if (tc->run(ctx, argv[0], tc->arg)) {
                ph_free(ctx);
                return fail("timed case failed", tc->name);
            }
            double t = seconds() - t0;
            if (k >= 0) {
                ms[runs++] = t * 1e3;
                total += t;
                if (runs >= TIME_MIN_RUNS && total >= TIME_MIN_SECONDS) {
                    break;
                }
            }
        }
        qsort(ms, (size_t)runs, sizeof(ms[0]), cmp_double);
        json_t o = json_object(&cases, tc->name);
        json_double(&o, "min_ms", ms[0]);
        json_double(&o, "median_ms", ms[runs / 2]);
        json_int(&o, "runs", runs);
        json_close_object(&o);
    }
    json_close_object(&cases);
    json_end(&j);
    ph_free(ctx);
    return 0;
}

/* --8<-- [end:time] */

/* --8<-- [start:scan] */
/* site_stages scan: what one comparison costs in a search that compares a query with every
 * stored hash, a linear scan. Each case compares one query with a stored set of one
 * algorithm's shape: SCAN_HASHES 64-bit hashes, or SCAN_DIGESTS digests in an array of
 * ph_digest_t. The values are pseudo-random from a fixed seed, as these comparisons cost
 * the same whatever the values (Radial's apart, which refuses a digest without variance
 * before comparing, and random bytes always have some). Each case counts the stored values
 * within a fixed distance and adds it to scan_sink, so that the compiler cannot leave a
 * comparison out. */
enum {
    SCAN_HASHES = 1 << 20,
    SCAN_DIGESTS = 1 << 16,
    SCAN_WITHIN_BITS = 10,
};

static volatile size_t scan_sink;

typedef struct {
    uint64_t query;
    const uint64_t *hashes;
    ph_digest_t query_digest;
    const ph_digest_t *digests;
} scan_t;

/* The library's comparison of two 64-bit hashes, called once per stored hash. */
static size_t scan_hamming(const scan_t *s) {
    size_t within = 0;
    for (size_t i = 0; i < SCAN_HASHES; i++) {
        within += ph_hamming_distance(s->query, s->hashes[i]) <= SCAN_WITHIN_BITS;
    }
    return within;
}

#if defined(__GNUC__) || defined(__clang__)
/* The same distance computed in the caller's loop, where the compiler can inline the
 * popcount and vectorize the loop: what a search loop of its own costs. */
static size_t scan_popcount(const scan_t *s) {
    size_t within = 0;
    for (size_t i = 0; i < SCAN_HASHES; i++) {
        within += __builtin_popcountll(s->query ^ s->hashes[i]) <= SCAN_WITHIN_BITS;
    }
    return within;
}
#endif

/* The function a digest's kind calls for, once per stored digest. */
static size_t scan_digests(const scan_t *s) {
    size_t within = 0;
    const ph_digest_t *q = &s->query_digest;
    for (size_t i = 0; i < SCAN_DIGESTS; i++) {
        const ph_digest_t *d = &s->digests[i];
        double v = 0.0;
        switch (q->kind) {
            case PH_DIGEST_KIND_BITS:
                within += ph_hamming_distance_digest(q, d) <= SCAN_WITHIN_BITS;
                break;
            case PH_DIGEST_KIND_COEFFICIENTS:
                within += ph_radial_similarity(q, d, &v) == PH_SUCCESS && v >= 0.9;
                break;
            case PH_DIGEST_KIND_HISTOGRAM:
                within += ph_histogram_intersection(q, d, &v) == PH_SUCCESS && v >= 0.9;
                break;
            default:
                within += ph_l2_distance(q, d) <= 10.0;
                break;
        }
    }
    return within;
}

/* The cases: a name, the scan, how many values it compares, and for a digest the
 * algorithm whose shape (ph_digest_info()) the stored digests take. */
static const struct {
    const char *name;
    size_t (*scan)(const scan_t *s);
    size_t count;
    ph_algorithm_t algo;
} scan_cases[] = {
    {"hamming", scan_hamming, SCAN_HASHES, PH_ALGO_PHASH},
#if defined(__GNUC__) || defined(__clang__)
    {"popcount", scan_popcount, SCAN_HASHES, PH_ALGO_PHASH},
#endif
    {"phash", scan_digests, SCAN_DIGESTS, PH_ALGO_PHASH},
    {"bmh", scan_digests, SCAN_DIGESTS, PH_ALGO_BMH},
    {"mhash", scan_digests, SCAN_DIGESTS, PH_ALGO_MHASH},
    {"radial", scan_digests, SCAN_DIGESTS, PH_ALGO_RADIAL},
    {"color_hash", scan_digests, SCAN_DIGESTS, PH_ALGO_COLOR_HASH},
    {"color_moments", scan_digests, SCAN_DIGESTS, PH_ALGO_COLOR_MOMENTS},
};

/* xorshift64: a fixed sequence of pseudo-random values. */
static uint64_t scan_random(void) {
    static uint64_t x = 0x9E3779B97F4A7C15u;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
}

/* A digest of the shape `algo` gives with the default settings, its bytes random. */
static int random_digest(ph_algorithm_t algo, ph_digest_t *d) {
    size_t size;
    ph_digest_kind_t kind;
    if (ph_digest_info(NULL, algo, &size, &kind) != PH_SUCCESS) {
        return 1;
    }
    memset(d, 0, sizeof(*d));
    d->size = (uint8_t)size;
    d->kind = (uint8_t)kind;
    for (size_t i = 0; i < size; i++) {
        d->data[i] = (uint8_t)scan_random();
    }
    return 0;
}

/* site_stages scan: every case of scan_cases[], timed as the cases of `time` are; the
 * minimum and the median of its runs, in nanoseconds per comparison, as one JSON object. */
int mode_scan(int argc, char **argv) {
    (void)argc;
    (void)argv;
    uint64_t *hashes = malloc(SCAN_HASHES * sizeof(*hashes));
    ph_digest_t *digests = malloc(SCAN_DIGESTS * sizeof(*digests));
    if (!hashes || !digests) {
        free(hashes);
        free(digests);
        return fail("out of memory", NULL);
    }
    scan_t s = {scan_random(), hashes, {{0}, 0, 0, {0}}, digests};
    for (size_t i = 0; i < SCAN_HASHES; i++) {
        hashes[i] = scan_random();
    }
    json_t j = json_begin(stdout);
    json_int(&j, "hashes", SCAN_HASHES);
    json_int(&j, "digests", SCAN_DIGESTS);
    json_t cases = json_object(&j, "cases");
    static double ns[TIME_MAX_RUNS];
    int status = 0;
    for (size_t c = 0; c < COUNT(scan_cases) && !status; c++) {
        for (size_t i = 0; i < SCAN_DIGESTS && !status; i++) {
            status = random_digest(scan_cases[c].algo, &digests[i]);
        }
        if (status || random_digest(scan_cases[c].algo, &s.query_digest)) {
            status = fail("no digest shape for", scan_cases[c].name);
            break;
        }
        int runs = 0;
        double total = 0.0;
        for (int k = -1; k < TIME_MAX_RUNS; k++) { /* k = -1: the warm-up */
            double t0 = seconds();
            scan_sink += scan_cases[c].scan(&s);
            double t = seconds() - t0;
            if (k >= 0) {
                ns[runs++] = t * 1e9 / (double)scan_cases[c].count;
                total += t;
                if (runs >= TIME_MIN_RUNS && total >= TIME_MIN_SECONDS) {
                    break;
                }
            }
        }
        qsort(ns, (size_t)runs, sizeof(ns[0]), cmp_double);
        json_t o = json_object(&cases, scan_cases[c].name);
        json_double(&o, "min_ns", ns[0]);
        json_double(&o, "median_ns", ns[runs / 2]);
        json_int(&o, "runs", runs);
        json_int(&o, "size", scan_cases[c].scan == scan_digests ? s.query_digest.size : 8);
        json_close_object(&o);
    }
    json_close_object(&cases);
    json_end(&j);
    free(hashes);
    free(digests);
    return status;
}

/* --8<-- [end:scan] */

/* --8<-- [start:batch] */
/* The highest resident memory of this process so far, in bytes: a high-water mark that
 * never goes down, which is why every thread count of `batch` runs as a process of its
 * own. */
static long long peak_resident_bytes(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) {
        return -1;
    }
#ifdef __APPLE__
    return (long long)ru.ru_maxrss; /* bytes on Darwin */
#else
    return (long long)ru.ru_maxrss * 1024; /* kilobytes on Linux */
#endif
}

/* site_stages batch <image> <threads> <items> <runs>: ph_hash_files() on `items` entries
 * that all name the image, with the four 64-bit hashes, once to warm up and then `runs`
 * times. Prints the shortest run in seconds, the workers it ran, the CPUs `threads = 0`
 * would use, and the peak resident memory of the process before the first batch and after
 * the last, with the build that ran them. Every item must succeed. */
int mode_batch(int argc, char **argv) {
    (void)argc;
    int threads = atoi(argv[1]), runs = atoi(argv[3]);
    long count = atol(argv[2]);
    if (threads < 1 || count < 1 || runs < 1) {
        return fail("threads, items and runs must be positive", argv[1]);
    }
    size_t n = (size_t)count;
    ph_batch_item_t *items = calloc(n, sizeof(*items));
    if (!items) {
        return fail("out of memory for", argv[2]);
    }
    for (size_t i = 0; i < n; i++) {
        items[i].path = argv[0];
    }
    long long before = peak_resident_bytes();
    double best = 0.0;
    for (int r = -1; r < runs; r++) { /* r = -1: the warm-up */
        double t0 = seconds();
        ph_error_t err = ph_hash_files(items, n, PH_HASH_FLAGS_ALL, threads);
        double t = seconds() - t0;
        for (size_t i = 0; i < n && err == PH_SUCCESS; i++) {
            err = items[i].status;
        }
        if (err != PH_SUCCESS) {
            free(items);
            return fail("batch failed on", argv[0]);
        }
        if (r == 0 || (r > 0 && t < best)) {
            best = t;
        }
    }
    free(items);
    json_t j = json_begin(stdout);
    json_string(&j, "build_info", ph_get_build_info());
    json_int(&j, "cpus", ph_available_cpus()); /* what threads = 0 would start */
    json_int(&j, "items", (long long)n);
    json_int(&j, "workers", (long long)((size_t)threads < n ? (size_t)threads : n));
    json_double(&j, "seconds", best);
    json_int(&j, "peak_before", before);
    json_int(&j, "peak_after", peak_resident_bytes());
    json_end(&j);
    return 0;
}

/* --8<-- [end:batch] */
