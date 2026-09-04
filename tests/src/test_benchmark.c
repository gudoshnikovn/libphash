#include "libphash.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

/* --- Global State --- */
int g_json_output = 0;

/* --- Timing Utilities --- */
double get_time_sec() {
#ifdef __APPLE__
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0)
        mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
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
 * percent, and the caller can't tell that it happened. Measured on an idle
 * arm64 macOS box, comparing this binary against *itself* through
 * scripts/bench_regression_gate.sh produced false 40-45% "regressions"
 * because of exactly that. So: warm up first, then time every iteration
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
    if (s->n < s->cap)
        s->ms[s->n++] = seconds * 1000.0;
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

    if (s->n <= 0)
        return st;

    qsort(s->ms, (size_t)s->n, sizeof(double), ph_bench_cmp);
    for (i = 0; i < s->n; i++)
        sum += s->ms[i];

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
typedef ph_error_t (*ph_hash_func_t)(ph_context_t *, uint64_t *);

struct ph_hash_algo {
    const char *label;
    ph_hash_func_t func;
};

ph_error_t ph_compute_whash_fast_wrapper(ph_context_t *ctx, uint64_t *out_hash) {
    ph_context_set_whash_mode(ctx, PH_WHASH_FAST);
    return ph_compute_whash(ctx, out_hash);
}

ph_error_t ph_compute_whash_full_wrapper(ph_context_t *ctx, uint64_t *out_hash) {
    ph_context_set_whash_mode(ctx, PH_WHASH_FULL);
    return ph_compute_whash(ctx, out_hash);
}

/* --- Benchmark Functions --- */
void benchmark_hashing(ph_context_t *ctx, int iterations) {
    uint64_t hash;
    ph_digest_t digest;
    double start;
    int warmup = PH_BENCH_WARMUP(iterations);
    ph_bench_samples samples;

    if (!ph_bench_samples_init(&samples, iterations)) {
        fprintf(stderr, "benchmark: out of memory for %d samples\n", iterations);
        return;
    }

    if (!g_json_output) {
        printf("\n--- Hashing Performance (%d iterations, %d warmup) ---\n", iterations, warmup);
        printf("%-15s | %-12s | %-12s | %-12s | %-12s\n", "Algorithm", "Total Time", "Min (ms/op)",
               "Median", "p90");
        printf("----------------|--------------|--------------|--------------|--------------\n");
    } else {
        printf("\"hashing\": [");
    }

    struct ph_hash_algo uint64_algos[] = {{"aHash", ph_compute_ahash},
                                          {"dHash", ph_compute_dhash},
                                          {"pHash", ph_compute_phash},
                                          {"wHash (Fast)", ph_compute_whash_fast_wrapper},
                                          {"wHash (Full)", ph_compute_whash_full_wrapper},
                                          {"ColorHash", ph_compute_color_hash},
                                          {NULL, NULL}};

    for (int i = 0; uint64_algos[i].label; i++) {
        ph_bench_stats st;

        for (int j = 0; j < warmup; j++)
            uint64_algos[i].func(ctx, &hash);

        samples.n = 0;
        for (int j = 0; j < iterations; j++) {
            start = get_time_sec();
            uint64_algos[i].func(ctx, &hash);
            ph_bench_add(&samples, get_time_sec() - start);
        }
        st = ph_bench_summarize(&samples);

        if (!g_json_output) {
            ph_bench_print_row(uint64_algos[i].label, &st, "");
        } else {
            printf("%s{\"name\": \"%s\", ", (i == 0 ? "" : ", "), uint64_algos[i].label);
            ph_bench_print_json_stats(&st);
            printf("}");
        }
    }

    /* Digest-based (mHash, Radial): an order of magnitude or more slower per op, so they
     * get 1/10 of the iterations. Warmup is still taken from the full count -- a
     * cheap warmup on the slowest algorithm is the wrong place to save time. */
    {
        int mh_iters = iterations / 10;
        int mh_warmup;
        ph_bench_stats st;

        if (mh_iters < 1)
            mh_iters = 1;
        mh_warmup = PH_BENCH_WARMUP(mh_iters);

        for (int i = 0; i < mh_warmup; i++) {
            if (ph_compute_mhash(ctx, &digest) != PH_SUCCESS) {
                /* ignore for benchmark */
            }
        }

        samples.n = 0;
        for (int i = 0; i < mh_iters; i++) {
            start = get_time_sec();
            if (ph_compute_mhash(ctx, &digest) != PH_SUCCESS) {
                /* ignore for benchmark */
            }
            ph_bench_add(&samples, get_time_sec() - start);
        }
        st = ph_bench_summarize(&samples);

        if (!g_json_output) {
            ph_bench_print_row("mHash", &st, " (1/10 iter)");
        } else {
            printf(", {\"name\": \"mHash\", ");
            ph_bench_print_json_stats(&st);
            printf("}");
        }
    }

    {
        int radial_iters = iterations / 10;
        int radial_warmup;
        ph_bench_stats st;

        if (radial_iters < 1)
            radial_iters = 1;
        radial_warmup = PH_BENCH_WARMUP(radial_iters);

        for (int i = 0; i < radial_warmup; i++) {
            if (ph_compute_radial_hash(ctx, &digest) != PH_SUCCESS) {
                /* ignore for benchmark */
            }
        }

        samples.n = 0;
        for (int i = 0; i < radial_iters; i++) {
            start = get_time_sec();
            if (ph_compute_radial_hash(ctx, &digest) != PH_SUCCESS) {
                /* ignore for benchmark */
            }
            ph_bench_add(&samples, get_time_sec() - start);
        }
        st = ph_bench_summarize(&samples);

        if (!g_json_output) {
            ph_bench_print_row("Radial", &st, " (1/10 iter)");
        } else {
            printf(", {\"name\": \"Radial\", ");
            ph_bench_print_json_stats(&st);
            printf("}]");
        }
    }

    ph_bench_samples_free(&samples);
}

void benchmark_directory(const char *path, int grayscale) {
    DIR *dir = opendir(path);
    if (!dir) {
        if (!g_json_output)
            fprintf(stderr, "Error: Could not open directory: %s\n", path);
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
            snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);
            if (ph_load_from_file(ctx, full_path) == PH_SUCCESS) {
                count++;
                if (!g_json_output && count % 100 == 0)
                    printf(".");
            }
        }
    }
    if (!g_json_output)
        printf("\n");

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

void benchmark_loading(const char *img, int iterations, int grayscale) {
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
     * intent -- the profile this project optimizes against (see tasks/README.md)
     * is decode-bound, and disk latency would only add variance. */
    for (int i = 0; i < warmup + iterations; i++) {
        double start = get_time_sec();
        ph_context_t *ctx;
        if (ph_create(&ctx) == PH_SUCCESS) {
            ph_context_set_load_grayscale(ctx, grayscale);
            if (ph_load_from_file(ctx, img) == PH_SUCCESS) {
                /* ignore for benchmark */
            }
            ph_free(ctx);
        }
        if (i >= warmup)
            ph_bench_add(&samples, get_time_sec() - start);
    }
    st = ph_bench_summarize(&samples);

    if (!g_json_output) {
        printf("Warmup: %d iterations\n", warmup);
        printf("Total: %.4fs, Min: %.4fms, Median: %.4fms, p90: %.4fms per load\n", st.total_s,
               st.min_ms, st.median_ms, st.p90_ms);
    } else {
        printf("\"loading_%s\": {\"image\": \"%s\", ", grayscale ? "grayscale" : "rgb", img);
        ph_bench_print_json_stats(&st);
        printf("}");
    }

    ph_bench_samples_free(&samples);
}

/* --- Main --- */
void print_usage(const char *prog) {
    printf("Usage: %s [options] [command] [args]\n", prog);
    printf("Options:\n");
    printf("  --json                Output results in JSON format\n");
    printf("Commands:\n");
    printf("  hash [file] [iters]   Benchmark hashing algorithms for a single image\n");
    printf("  dir  [path]           Benchmark loading performance for a directory\n");
    printf("  full [file] [iters]   Benchmark both loading and hashing\n");
    printf("  load [file] [iters]   Benchmark loading an image\n");
    printf("  smoke                 Run a standard set of benchmarks for CI\n");
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
        if (ph_create(&ctx) != PH_SUCCESS)
            return 1;
        if (ph_load_from_file(ctx, img) != PH_SUCCESS) {
            if (!g_json_output)
                fprintf(stderr, "Failed to load %s\n", img);
            ph_free(ctx);
            return 1;
        }
        benchmark_hashing(ctx, iters);
        ph_free(ctx);

    } else if (strcmp(cmd, "dir") == 0) {
        const char *path = (arg_idx + 1 < argc) ? argv[arg_idx + 1] : TEST_DATA_DIR;
        benchmark_directory(path, 1);
        if (g_json_output)
            printf(", ");
        benchmark_directory(path, 0);

    } else if (strcmp(cmd, "full") == 0) {
        img = (arg_idx + 1 < argc) ? argv[arg_idx + 1] : TEST_DATA_DIR "/photo.jpeg";
        iters = (arg_idx + 2 < argc) ? atoi(argv[arg_idx + 2]) : 100;

        if (!g_json_output)
            printf("--- Full Pipeline Benchmark ---\n");

        ph_context_t *ctx;
        if (ph_create(&ctx) != PH_SUCCESS)
            return 1;

        int warmup = PH_BENCH_WARMUP(iters);
        ph_bench_samples samples;
        ph_bench_stats st;

        if (!ph_bench_samples_init(&samples, iters)) {
            fprintf(stderr, "benchmark: out of memory for %d samples\n", iters);
            ph_free(ctx);
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
            if (i >= warmup)
                ph_bench_add(&samples, get_time_sec() - start);
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
        benchmark_loading(img, iters, 1);
        if (g_json_output)
            printf(", ");
        benchmark_loading(img, iters, 0);
    } else if (strcmp(cmd, "smoke") == 0) {
        /* Standard CI smoke test. 200 iterations, not 50: at 50 the whole
         * measurement window for a load metric is ~35ms, short enough that a
         * single OS stall dominates it. See docs/development.md for the
         * measured noise floor this number was chosen from. */
        img = TEST_DATA_DIR "/photo.jpeg";
        iters = 200;

        benchmark_loading(img, iters, 1);
        if (g_json_output)
            printf(", ");
        benchmark_loading(img, iters, 0);
        if (g_json_output)
            printf(", ");

        ph_context_t *ctx;
        if (ph_create(&ctx) == PH_SUCCESS && ph_load_from_file(ctx, img) == PH_SUCCESS) {
            benchmark_hashing(ctx, iters);
            ph_free(ctx);
        }
    } else {
        print_usage(argv[0]);
        if (g_json_output)
            printf("}");
        return 1;
    }

    if (g_json_output) {
        printf("}\n");
    }

    return 0;
}
