#include "../../include/libphash.h"
#include "test_macros.h"
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

/* --- Benchmark Data Types --- */
typedef ph_error_t (*ph_hash_func_t)(ph_context_t *, uint64_t *);

struct ph_hash_algo {
    const char *label;
    ph_hash_func_t func;
};

/* --- Benchmark Functions --- */
void benchmark_hashing(ph_context_t *ctx, int iterations) {
    uint64_t hash;
    ph_digest_t digest;
    double start, end, total;

    if (!g_json_output) {
        printf("\n--- Hashing Performance (%d iterations) ---\n", iterations);
        printf("%-15s | %-12s | %-12s\n", "Algorithm", "Total Time", "Avg (ms/op)");
        printf("----------------|--------------|--------------\n");
    } else {
        printf("\"hashing\": [");
    }

    struct ph_hash_algo uint64_algos[] = {{"aHash", ph_compute_ahash},
                                          {"dHash", ph_compute_dhash},
                                          {"pHash", ph_compute_phash},
                                          {"wHash", ph_compute_whash},
                                          {"mHash", ph_compute_mhash},
                                          {"ColorHash", ph_compute_color_hash},
                                          {NULL, NULL}};

    for (int i = 0; uint64_algos[i].label; i++) {
        start = get_time_sec();
        for (int j = 0; j < iterations; j++) {
            uint64_algos[i].func(ctx, &hash);
        }
        end = get_time_sec();
        total = end - start;
        if (!g_json_output) {
            printf("%-15s | %10.4fs | %10.4fms\n", uint64_algos[i].label, total,
                   (total / iterations) * 1000.0);
        } else {
            printf("%s{\"name\": \"%s\", \"total_s\": %.6f, \"avg_ms\": %.6f}",
                   (i == 0 ? "" : ", "), uint64_algos[i].label, total,
                   (total / iterations) * 1000.0);
        }
    }

    // Digest-based (Radial)
    int radial_iters = iterations / 10;
    if (radial_iters < 1)
        radial_iters = 1;

    start = get_time_sec();
    for (int i = 0; i < radial_iters; i++) {
        (void)ph_compute_radial_hash(ctx, &digest);
    }
    end = get_time_sec();
    total = end - start;
    if (!g_json_output) {
        printf("%-15s | %10.4fs | %10.4fms (1/10 iter)\n", "Radial", total,
               (total / radial_iters) * 1000.0);
    } else {
        printf(", {\"name\": \"Radial\", \"total_s\": %.6f, \"avg_ms\": %.6f}", total,
               (total / radial_iters) * 1000.0);
        printf("]");
    }
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

    double start = get_time_sec();
    for (int i = 0; i < iterations; i++) {
        ph_context_t *ctx;
        if (ph_create(&ctx) == PH_SUCCESS) {
            ph_context_set_load_grayscale(ctx, grayscale);
            (void)ph_load_from_file(ctx, img);
            ph_free(ctx);
        }
    }
    double end = get_time_sec();
    double total = end - start;

    if (!g_json_output) {
        printf("Total time: %.4fs, Avg: %.4fms/load\n", total, (total / iterations) * 1000.0);
    } else {
        printf("\"loading_%s\": {\"image\": \"%s\", \"iterations\": %d, \"total_s\": %.6f, "
               "\"avg_ms\": %.6f}",
               grayscale ? "grayscale" : "rgb", img, iterations, total,
               (total / iterations) * 1000.0);
    }
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

        double start = get_time_sec();
        for (int i = 0; i < iters; i++) {
            if (ph_load_from_file(ctx, img) == PH_SUCCESS) {
                uint64_t hash;
                (void)ph_compute_phash(ctx, &hash);
            }
        }
        double end = get_time_sec();
        if (!g_json_output) {
            printf("Load + pHash (iters=%d): Total %.4fs, Avg %.4fms\n", iters, end - start,
                   ((end - start) / iters) * 1000.0);
        } else {
            printf("\"full_pipeline\": {\"image\": \"%s\", \"iterations\": %d, \"total_s\": %.6f, "
                   "\"avg_ms\": %.6f}",
                   img, iters, end - start, ((end - start) / iters) * 1000.0);
        }

        ph_free(ctx);
    } else if (strcmp(cmd, "load") == 0) {
        img = (arg_idx + 1 < argc) ? argv[arg_idx + 1] : TEST_DATA_DIR "/photo.jpeg";
        iters = (arg_idx + 2 < argc) ? atoi(argv[arg_idx + 2]) : 100;
        benchmark_loading(img, iters, 1);
        if (g_json_output)
            printf(", ");
        benchmark_loading(img, iters, 0);
    } else if (strcmp(cmd, "smoke") == 0) {
        // Standard CI smoke test
        img = TEST_DATA_DIR "/photo.jpeg";
        iters = 50;

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
