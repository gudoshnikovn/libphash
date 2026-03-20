#include "../include/libphash.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

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

/* --- Benchmark Functions --- */
void benchmark_hashing(ph_context_t *ctx, int iterations) {
    uint64_t hash;
    ph_digest_t digest;
    double start, end, total;

    printf("\n--- Hashing Performance (%d iterations) ---\n", iterations);
    printf("%-15s | %-12s | %-12s\n", "Algorithm", "Total Time", "Avg (ms/op)");
    printf("----------------|--------------|--------------\n");

    struct {
        const char *label;
        ph_error_t (*func)(ph_context_t *, uint64_t *);
    } uint64_algos[] = {{"aHash", ph_compute_ahash},
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
        printf("%-15s | %10.4fs | %10.4fms\n", uint64_algos[i].label, total,
               (total / iterations) * 1000.0);
    }

    // Digest-based (Radial)
    start = get_time_sec();
    for (int i = 0; i < iterations / 10; i++) {
        (void)ph_compute_radial_hash(ctx, &digest);
    }
    end = get_time_sec();
    total = end - start;
    printf("%-15s | %10.4fs | %10.4fms (1/10 iter)\n", "Radial", total,
           (total / (iterations / 10)) * 1000.0);
}

void benchmark_directory(const char *path, int grayscale) {
    DIR *dir = opendir(path);
    if (!dir) {
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

    printf("\n--- Directory Loading Performance (%s) ---\n", path);
    printf("Mode: %s\n", grayscale ? "Grayscale (Fast)" : "RGB (Full)");

    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, ".jpg") || strstr(ent->d_name, ".jpeg") ||
            strstr(ent->d_name, ".png")) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);
            if (ph_load_from_file(ctx, full_path) == PH_SUCCESS) {
                count++;
                if (count % 100 == 0)
                    printf(".");
            }
        }
    }
    printf("\n");

    double end = get_time_sec();
    double total = end - start;

    if (count > 0) {
        printf("Loaded %d images in %.4fs (Avg: %.4fms/image)\n", count, total,
               (total / count) * 1000.0);
        printf("Throughput: %.2f images/sec\n", count / total);
    } else {
        printf("No valid images found in %s\n", path);
    }

    ph_free(ctx);
    closedir(dir);
}

void benchmark_loading(const char *img, int iterations, int grayscale) {
    printf("\n--- Loading Performance (%s, %d iterations) ---\n", img, iterations);
    printf("Mode: %s\n", grayscale ? "Grayscale (Fast)" : "RGB (Full)");

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

    printf("Total time: %.4fs, Avg: %.4fms/load\n", total, (total / iterations) * 1000.0);
}

/* --- Main --- */
void print_usage(const char *prog) {
    printf("Usage: %s [command] [args]\n", prog);
    printf("Commands:\n");
    printf("  hash [file] [iters]   Benchmark hashing algorithms for a single image\n");
    printf("  dir  [path]           Benchmark loading performance for a directory\n");
    printf("  full [file] [iters]   Benchmark both loading and hashing\n");
    printf("  load [file] [iters]   Benchmark loading an image\n");
}

int main(int argc, char **argv) {
    const char *cmd = "hash";
    const char *img = "tests/photo.jpeg";
    int iters = 10;

    if (argc >= 2) {
        cmd = argv[1];
    } else {
        printf("No command provided. Running default smoke test (hash %s %d)...\n", img, iters);
    }

    if (strcmp(cmd, "hash") == 0) {
        const char *img = (argc > 3) ? argv[2] : "tests/photo.jpeg";
        int iters = (argc > 3) ? atoi(argv[3]) : 100;

        ph_context_t *ctx;
        if (ph_create(&ctx) != PH_SUCCESS)
            return 1;
        if (ph_load_from_file(ctx, img) != PH_SUCCESS) {
            fprintf(stderr, "Failed to load %s\n", img);
            ph_free(ctx);
            return 1;
        }
        benchmark_hashing(ctx, iters);
        ph_free(ctx);

    } else if (strcmp(cmd, "dir") == 0) {
        const char *path = (argc > 2) ? argv[2] : "benchmarks/augmented";
        benchmark_directory(path, 1); // Benchmark grayscale loading (common case)
        benchmark_directory(path, 0); // Benchmark RGB loading

    } else if (strcmp(cmd, "full") == 0) {
        const char *img = (argc > 2) ? argv[2] : "tests/photo.jpeg";
        int iters = (argc > 3) ? atoi(argv[3]) : 100;

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
        printf("Load + pHash (iters=%d): Total %.4fs, Avg %.4fms\n", iters, end - start,
               ((end - start) / iters) * 1000.0);

        ph_free(ctx);
    } else if (strcmp(cmd, "load") == 0) {
        const char *img = (argc > 2) ? argv[2] : "tests/photo.jpeg";
        int iters = (argc > 3) ? atoi(argv[3]) : 100;
        benchmark_loading(img, iters, 1); // Grayscale
        benchmark_loading(img, iters, 0); // RGB
    } else {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
