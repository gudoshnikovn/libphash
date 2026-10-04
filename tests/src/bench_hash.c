/* clock_gettime()/CLOCK_MONOTONIC (below) and opendir()/readdir() (the non-MSVC
 * branch further down) are POSIX, not ISO C. The project compiles as strict ISO
 * (-std=c17, not -std=gnu17), so the compiler defines __STRICT_ANSI__ and glibc
 * hides every non-ISO declaration behind it -- this translation unit genuinely needs
 * POSIX and therefore asks for it explicitly, rather than
 * the whole project switching to a GNU dialect. Darwin declares these regardless
 * and additionally needs mach_absolute_time(), which _POSIX_C_SOURCE would hide,
 * so the request is scoped to the libcs that require it.
 *
 * Must precede every #include: feature test macros are read when the first system
 * header is parsed. */
#if !defined(__APPLE__) && !defined(_WIN32)
#    define _POSIX_C_SOURCE 200809L
#endif

#include "libphash.h"

/* Internal: ph_drop_gray_cache(), see benchmark_hashing(). */
#include "image/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
#    include <mach/mach_time.h>
#endif

/* MSVC has no <dirent.h>; this benchmark_directory() only ever needs each entry's
 * name, so a minimal opendir/readdir/closedir built on FindFirstFile/FindNextFile
 * covers it without pulling in a full POSIX dirent shim. */
#ifdef _MSC_VER
#    include <windows.h>

typedef struct {
    HANDLE handle;
    WIN32_FIND_DATAA find_data;
    int first_call;
} DIR;

struct dirent {
    char d_name[MAX_PATH];
};

static DIR *opendir(const char *path) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    DIR *d = malloc(sizeof(DIR));
    if (!d) {
        return NULL;
    }
    d->handle = FindFirstFileA(pattern, &d->find_data);
    if (d->handle == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    d->first_call = 1;
    return d;
}

static struct dirent *readdir(DIR *d) {
    static struct dirent ent;
    if (!d->first_call) {
        if (!FindNextFileA(d->handle, &d->find_data)) {
            return NULL;
        }
    }
    d->first_call = 0;
    snprintf(ent.d_name, sizeof(ent.d_name), "%s", d->find_data.cFileName);
    return &ent;
}

static void closedir(DIR *d) {
    FindClose(d->handle);
    free(d);
}
#else
#    include <dirent.h>
#endif

/* Version of what the JSON metrics measure; see main(). 2: every hash row includes the
 * grayscale conversion and the area-sum grid, recomputed for each iteration. */
#define PH_BENCH_SCHEMA 2

/* --- Global State --- */
int g_json_output = 0;

/* --- Timing Utilities --- */
double get_time_sec() {
#ifdef __APPLE__
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) {
        mach_timebase_info(&tb);
    }
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
#elif defined(_MSC_VER)
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

/* --- Per-iteration sampling ---
 *
 * A single mean over the whole loop is not a usable benchmark number: one
 * scheduler preemption or page fault inside the loop shifts it by tens of
 * percent, and the caller can't tell that it happened: compared against
 * itself through scripts/bench_regression_gate.sh on an idle machine, a
 * mean-based number shows false 40-45% "regressions". So: warm up first, then time every iteration
 * separately and report robust statistics (min/median/p90) alongside the mean.
 *
 * min_ms is the number to compare across builds -- it's the closest estimate
 * of "how fast this code can run" with OS noise removed; median_ms shows the
 * typical case, p90_ms shows how noisy the environment was. */

/* Iterations discarded before measuring: warms the page cache for the file
 * being decoded, faults in the arena and the code paths, and lets the CPU
 * settle at a steady clock. */
#define PH_BENCH_WARMUP(iters) ((iters) / 10 > 3 ? (iters) / 10 : 3)

typedef struct {
    double *ms; /* per-iteration wall time, milliseconds */
    int n;
    int cap;
} ph_bench_samples;

static int ph_bench_samples_init(ph_bench_samples *s, int cap) {
    s->ms = (double *)malloc((size_t)cap * sizeof(double));
    s->n = 0;
    s->cap = s->ms ? cap : 0;
    return s->ms != NULL;
}

static void ph_bench_samples_free(ph_bench_samples *s) {
    free(s->ms);
    s->ms = NULL;
    s->n = s->cap = 0;
}

static void ph_bench_add(ph_bench_samples *s, double seconds) {
    if (s->n < s->cap) {
        s->ms[s->n++] = seconds * 1000.0;
    }
}

static int ph_bench_cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

typedef struct {
    double min_ms, median_ms, p90_ms, avg_ms, total_s;
    int iterations;
} ph_bench_stats;

/* Sorts s->ms in place. */
static ph_bench_stats ph_bench_summarize(ph_bench_samples *s) {
    ph_bench_stats st = {0, 0, 0, 0, 0, 0};
    double sum = 0.0;
    int i;

    if (s->n <= 0) {
        return st;
    }

    qsort(s->ms, (size_t)s->n, sizeof(double), ph_bench_cmp);
    for (i = 0; i < s->n; i++) {
        sum += s->ms[i];
    }

    st.iterations = s->n;
    st.min_ms = s->ms[0];
    st.median_ms = s->ms[(s->n - 1) / 2];
    st.p90_ms = s->ms[(int)((double)(s->n - 1) * 0.9)];
    st.avg_ms = sum / s->n;
    st.total_s = sum / 1000.0;
    return st;
}

/* avg_ms is kept in the JSON for schema compatibility with older gate runs,
 * but min_ms is what scripts/bench_regression_gate.sh compares. */
static void ph_bench_print_json_stats(const ph_bench_stats *st) {
    printf("\"iterations\": %d, \"total_s\": %.6f, \"min_ms\": %.6f, "
           "\"median_ms\": %.6f, \"p90_ms\": %.6f, \"avg_ms\": %.6f",
           st->iterations, st->total_s, st->min_ms, st->median_ms, st->p90_ms, st->avg_ms);
}

static void ph_bench_print_row(const char *label, const ph_bench_stats *st, const char *suffix) {
    printf("%-15s | %10.4fs | %10.4fms | %10.4fms | %10.4fms%s\n", label, st->total_s, st->min_ms,
           st->median_ms, st->p90_ms, suffix);
}

/* --- Benchmark Data Types --- */
typedef ph_error_t (*ph_bench_hash_fn)(ph_context_t *);

struct ph_hash_algo {
    const char *label;
    ph_bench_hash_fn func;
    int iter_divisor; /* digest algorithms an order of magnitude slower run 1/10 of the loop */
};

static ph_error_t bench_ahash(ph_context_t *ctx) {
    uint64_t h;
    return ph_compute_ahash(ctx, &h);
}

static ph_error_t bench_dhash(ph_context_t *ctx) {
    uint64_t h;
    return ph_compute_dhash(ctx, &h);
}

static ph_error_t bench_phash(ph_context_t *ctx) {
    uint64_t h;
    return ph_compute_phash(ctx, &h);
}

static ph_error_t bench_whash_fast(ph_context_t *ctx) {
    uint64_t h;
    ph_context_set_whash_mode(ctx, PH_WHASH_FAST);
    return ph_compute_whash(ctx, &h);
}

static ph_error_t bench_whash_full(ph_context_t *ctx) {
    uint64_t h;
    ph_context_set_whash_mode(ctx, PH_WHASH_FULL);
    return ph_compute_whash(ctx, &h);
}

static ph_error_t bench_bmh(ph_context_t *ctx) {
    ph_digest_t d;
    return ph_compute_bmh(ctx, &d);
}

static ph_error_t bench_color_moments(ph_context_t *ctx) {
    ph_digest_t d;
    return ph_compute_color_moments_hash(ctx, &d);
}

static ph_error_t bench_color_hash(ph_context_t *ctx) {
    ph_digest_t d;
    return ph_compute_color_hash(ctx, &d);
}

static ph_error_t bench_mhash(ph_context_t *ctx) {
    ph_digest_t d;
    return ph_compute_mhash(ctx, &d);
}

static ph_error_t bench_radial(ph_context_t *ctx) {
    ph_digest_t d;
    return ph_compute_radial_hash(ctx, &d);
}

/* --- Benchmark Functions --- */

/* Each hashing iteration measures the first hash computed on a loaded image, which is
 * what a caller who loads an image and hashes it pays: the grayscale conversion and the
 * shared area-sum grid are part of the cost. The context caches both until the image
 * changes, so they are dropped before every iteration, outside the timed region --
 * otherwise every iteration after the first would time only the work left once they
 * exist (for aHash, assembling 8x8 from a cached 32x32 grid: under a microsecond). */
void benchmark_hashing(ph_context_t *ctx, int iterations) {
    static const struct ph_hash_algo algos[] = {
        {"aHash", bench_ahash, 1},
        {"dHash", bench_dhash, 1},
        {"pHash", bench_phash, 1},
        {"wHash (Fast)", bench_whash_fast, 1},
        {"wHash (Full)", bench_whash_full, 1},
        {"BMH", bench_bmh, 1},
        {"ColorHash", bench_color_hash, 1},
        {"ColorMoments", bench_color_moments, 1},
        {"mHash", bench_mhash, 10},
        {"Radial", bench_radial, 10},
    };
    /* Taken from the full count for every algorithm: a cheap warmup on the slowest ones
     * is the wrong place to save time. */
    int warmup = PH_BENCH_WARMUP(iterations);
    ph_bench_samples samples;

    if (!ph_bench_samples_init(&samples, iterations)) {
        fprintf(stderr, "benchmark: out of memory for %d samples\n", iterations);
        return;
    }

    if (!g_json_output) {
        printf("\n--- Hashing: first hash on a loaded image, caches dropped each time "
               "(%d iterations, %d warmup) ---\n",
               iterations, warmup);
        printf("%-15s | %-12s | %-12s | %-12s | %-12s\n", "Algorithm", "Total Time", "Min (ms/op)",
               "Median", "p90");
        printf("----------------|--------------|--------------|--------------|--------------\n");
    } else {
        printf("\"hashing\": [");
    }

    for (size_t i = 0; i < sizeof(algos) / sizeof(algos[0]); i++) {
        int iters = iterations / algos[i].iter_divisor;
        ph_bench_stats st;

        if (iters < 1) {
            iters = 1;
        }

        for (int j = 0; j < warmup; j++) {
            ph_drop_gray_cache(ctx);
            if (algos[i].func(ctx) != PH_SUCCESS) {
                /* ignore for benchmark */
            }
        }

        samples.n = 0;
        for (int j = 0; j < iters; j++) {
            double start;
            ph_drop_gray_cache(ctx);
            start = get_time_sec();
            if (algos[i].func(ctx) != PH_SUCCESS) {
                /* ignore for benchmark */
            }
            ph_bench_add(&samples, get_time_sec() - start);
        }
        st = ph_bench_summarize(&samples);

        if (!g_json_output) {
            ph_bench_print_row(algos[i].label, &st,
                               algos[i].iter_divisor > 1 ? " (1/10 iter)" : "");
        } else {
            printf("%s{\"name\": \"%s\", ", i == 0 ? "" : ", ", algos[i].label);
            ph_bench_print_json_stats(&st);
            printf("}");
        }
    }
    if (g_json_output) {
        printf("]");
    }

    ph_bench_samples_free(&samples);
}

void benchmark_directory(const char *path, int grayscale) {
    DIR *dir = opendir(path);
    if (!dir) {
        if (!g_json_output) {
            fprintf(stderr, "Error: Could not open directory: %s\n", path);
        }
        return;
    }

    ph_context_t *ctx;
    if (ph_create(&ctx) != PH_SUCCESS) {
        closedir(dir);
        return;
    }
    ph_context_set_load_grayscale(ctx, grayscale);

    struct dirent *ent;
    int count = 0;
    double start = get_time_sec();

    if (!g_json_output) {
        printf("\n--- Directory Loading Performance (%s) ---\n", path);
        printf("Mode: %s\n", grayscale ? "Grayscale (Fast)" : "RGB (Full)");
    }

    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, ".jpg") || strstr(ent->d_name, ".jpeg") ||
            strstr(ent->d_name, ".png")) {
            char full_path[512];
            int len = snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);
            if (len < 0 || (size_t)len >= sizeof(full_path)) {
                continue; /* a path this long would be truncated into a different file */
            }
            if (ph_load_from_file(ctx, full_path) == PH_SUCCESS) {
                count++;
                if (!g_json_output && count % 100 == 0) {
                    printf(".");
                }
            }
        }
    }
    if (!g_json_output) {
        printf("\n");
    }

    double end = get_time_sec();
    double total = end - start;

    if (!g_json_output) {
        if (count > 0) {
            printf("Loaded %d images in %.4fs (Avg: %.4fms/image)\n", count, total,
                   (total / count) * 1000.0);
            printf("Throughput: %.2f images/sec\n", count / total);
        } else {
            printf("No valid images found in %s\n", path);
        }
    } else {
        printf("\"directory\": {\"path\": \"%s\", \"mode\": \"%s\", \"count\": %d, \"total_s\": "
               "%.6f, \"avg_ms\": %.6f}",
               path, grayscale ? "grayscale" : "rgb", count, total,
               count > 0 ? (total / count) * 1000.0 : 0.0);
    }

    ph_free(ctx);
    closedir(dir);
}

/* `metric` names the JSON object: "loading_<metric>". `scale` is the JPEG decode scale
 * (ph_context_set_decode_scale()); other formats ignore it. */
void benchmark_loading(const char *metric, const char *img, int iterations, int grayscale,
                       ph_decode_scale_t scale) {
    if (!g_json_output) {
        printf("\n--- Loading Performance (%s, %d iterations) ---\n", img, iterations);
        printf("Mode: %s\n", grayscale ? "Grayscale (Fast)" : "RGB (Full)");
    }

    int warmup = PH_BENCH_WARMUP(iterations);
    ph_bench_samples samples;
    ph_bench_stats st;

    if (!ph_bench_samples_init(&samples, iterations)) {
        fprintf(stderr, "benchmark: out of memory for %d samples\n", iterations);
        return;
    }

    /* The warmup pass matters most here: it pulls the file into the page cache,
     * so what's measured afterwards is decode cost and not disk. That's the
     * intent -- loading is decode-bound, and disk latency would only add
     * variance. */
    for (int i = 0; i < warmup + iterations; i++) {
        double start = get_time_sec();
        ph_context_t *ctx;
        if (ph_create(&ctx) == PH_SUCCESS) {
            ph_context_set_load_grayscale(ctx, grayscale);
            ph_context_set_decode_scale(ctx, scale);
            if (ph_load_from_file(ctx, img) == PH_SUCCESS) {
                /* ignore for benchmark */
            }
            ph_free(ctx);
        }
        if (i >= warmup) {
            ph_bench_add(&samples, get_time_sec() - start);
        }
    }
    st = ph_bench_summarize(&samples);

    if (!g_json_output) {
        printf("Warmup: %d iterations\n", warmup);
        printf("Total: %.4fs, Min: %.4fms, Median: %.4fms, p90: %.4fms per load\n", st.total_s,
               st.min_ms, st.median_ms, st.p90_ms);
    } else {
        printf("\"loading_%s\": {\"image\": \"%s\", ", metric, img);
        ph_bench_print_json_stats(&st);
        printf("}");
    }

    ph_bench_samples_free(&samples);
}

/* --- Main --- */

/* Ends the JSON document. Every exit path goes through here, so a failed run still prints
 * a valid document: `after_metrics` says whether a metric precedes the closing field. */
static void ph_bench_json_close(int after_metrics) {
    if (g_json_output) {
        printf("%s\"schema\": %d}\n", after_metrics ? ", " : "", PH_BENCH_SCHEMA);
    }
}

void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] [command] [args]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --json                Output results in JSON format\n");
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  hash [file] [iters]   Benchmark hashing algorithms for a single image\n");
    fprintf(stderr, "  dir  [path]           Benchmark loading performance for a directory\n");
    fprintf(stderr, "  full [file] [iters]   Benchmark both loading and hashing\n");
    fprintf(stderr, "  load [file] [iters] [scale]\n"
                    "                        Benchmark loading an image; scale 0-3 is the\n"
                    "                        JPEG decode scale (full, 1/2, 1/4, 1/8)\n");
    fprintf(stderr, "  smoke                 Run a standard set of benchmarks for CI\n");
}

int main(int argc, char **argv) {
    int arg_idx = 1;
    if (argc > 1 && strcmp(argv[1], "--json") == 0) {
        g_json_output = 1;
        arg_idx++;
    }

    const char *cmd = (arg_idx < argc) ? argv[arg_idx] : "hash";
    const char *img = TEST_DATA_DIR "/photo.jpeg";
    int iters = 100;

    if (g_json_output) {
        printf("{");
    } else if (arg_idx >= argc) {
        printf("No command provided. Running default smoke test (hash %s %d)...\n", img, iters);
    }

    if (strcmp(cmd, "hash") == 0) {
        img = (arg_idx + 1 < argc) ? argv[arg_idx + 1] : TEST_DATA_DIR "/photo.jpeg";
        iters = (arg_idx + 2 < argc) ? atoi(argv[arg_idx + 2]) : 100;

        ph_context_t *ctx;
        if (ph_create(&ctx) != PH_SUCCESS) {
            ph_bench_json_close(0);
            return 1;
        }
        if (ph_load_from_file(ctx, img) != PH_SUCCESS) {
            fprintf(stderr, "Failed to load %s\n", img);
            ph_free(ctx);
            ph_bench_json_close(0);
            return 1;
        }
        benchmark_hashing(ctx, iters);
        ph_free(ctx);

    } else if (strcmp(cmd, "dir") == 0) {
        const char *path = (arg_idx + 1 < argc) ? argv[arg_idx + 1] : TEST_DATA_DIR;
        benchmark_directory(path, 1);
        if (g_json_output) {
            printf(", ");
        }
        benchmark_directory(path, 0);

    } else if (strcmp(cmd, "full") == 0) {
        img = (arg_idx + 1 < argc) ? argv[arg_idx + 1] : TEST_DATA_DIR "/photo.jpeg";
        iters = (arg_idx + 2 < argc) ? atoi(argv[arg_idx + 2]) : 100;

        if (!g_json_output) {
            printf("--- Full Pipeline Benchmark ---\n");
        }

        ph_context_t *ctx;
        if (ph_create(&ctx) != PH_SUCCESS) {
            ph_bench_json_close(0);
            return 1;
        }

        int warmup = PH_BENCH_WARMUP(iters);
        ph_bench_samples samples;
        ph_bench_stats st;

        if (!ph_bench_samples_init(&samples, iters)) {
            fprintf(stderr, "benchmark: out of memory for %d samples\n", iters);
            ph_free(ctx);
            ph_bench_json_close(0);
            return 1;
        }

        for (int i = 0; i < warmup + iters; i++) {
            double start = get_time_sec();
            if (ph_load_from_file(ctx, img) == PH_SUCCESS) {
                uint64_t hash;
                if (ph_compute_phash(ctx, &hash) == PH_SUCCESS) {
                    /* ignore for benchmark */
                }
            }
            if (i >= warmup) {
                ph_bench_add(&samples, get_time_sec() - start);
            }
        }
        st = ph_bench_summarize(&samples);

        if (!g_json_output) {
            printf("Load + pHash (iters=%d, warmup=%d): Total %.4fs, Min %.4fms, Median %.4fms, "
                   "p90 %.4fms\n",
                   iters, warmup, st.total_s, st.min_ms, st.median_ms, st.p90_ms);
        } else {
            printf("\"full_pipeline\": {\"image\": \"%s\", ", img);
            ph_bench_print_json_stats(&st);
            printf("}");
        }

        ph_bench_samples_free(&samples);

        ph_free(ctx);
    } else if (strcmp(cmd, "load") == 0) {
        img = (arg_idx + 1 < argc) ? argv[arg_idx + 1] : TEST_DATA_DIR "/photo.jpeg";
        iters = (arg_idx + 2 < argc) ? atoi(argv[arg_idx + 2]) : 100;
        ph_decode_scale_t scale = (arg_idx + 3 < argc) ? (ph_decode_scale_t)atoi(argv[arg_idx + 3])
                                                       : PH_DECODE_SCALE_FULL;
        benchmark_loading("grayscale", img, iters, 1, scale);
        if (g_json_output) {
            printf(", ");
        }
        benchmark_loading("rgb", img, iters, 0, scale);
    } else if (strcmp(cmd, "smoke") == 0) {
        /* Standard CI smoke test. 200 iterations, not 50: at 50 the whole
         * measurement window for a load metric is ~35ms, short enough that a
         * single OS stall dominates it. docs/development.md has the noise
         * floor measured with it.
         *
         * One decode metric per format the build decodes natively or through
         * stb_image (JPEG twice: the grayscale request takes a different decoder
         * path), then every hash on a loaded JPEG. */
        img = TEST_DATA_DIR "/photo.jpeg";
        iters = 200;

        ph_context_t *ctx;
        if (ph_create(&ctx) != PH_SUCCESS) {
            fprintf(stderr, "smoke: cannot create a context\n");
            ph_bench_json_close(0);
            return 1;
        }
        if (ph_load_from_file(ctx, img) != PH_SUCCESS) {
            fprintf(stderr, "smoke: cannot load %s\n", img);
            ph_free(ctx);
            ph_bench_json_close(0);
            return 1;
        }

        benchmark_loading("grayscale", img, iters, 1, PH_DECODE_SCALE_FULL);
        if (g_json_output) {
            printf(", ");
        }
        benchmark_loading("rgb", img, iters, 0, PH_DECODE_SCALE_FULL);
        if (g_json_output) {
            printf(", ");
        }
        benchmark_loading("png_rgb", TEST_DATA_DIR "/photo_complex.png", iters, 0,
                          PH_DECODE_SCALE_FULL);
        if (g_json_output) {
            printf(", ");
        }
        /* stb_image has no WebP decoder: without libwebp there is nothing to time. */
        if (ph_can_use_webp()) {
            benchmark_loading("webp_rgb", TEST_DATA_DIR "/photo.webp", iters, 0,
                              PH_DECODE_SCALE_FULL);
            if (g_json_output) {
                printf(", ");
            }
        }

        benchmark_hashing(ctx, iters);
        ph_free(ctx);
    } else {
        print_usage(argv[0]);
        ph_bench_json_close(0);
        return 1;
    }

    ph_bench_json_close(1);
    return 0;
}
