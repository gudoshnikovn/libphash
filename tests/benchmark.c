#include "../include/libphash.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

double get_time_sec() {
#ifdef __APPLE__
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

void benchmark_hash(const char *label, ph_error_t (*func)(ph_context_t *, uint64_t *), ph_context_t *ctx, int iterations) {
    uint64_t hash;
    double start = get_time_sec();
    for (int i = 0; i < iterations; i++) {
        func(ctx, &hash);
    }
    double end = get_time_sec();
    double total_time = end - start;
    printf("[BENCH] %-10s | %d iterations | Total: %.4fs | Avg: %.6fs/op\n", 
           label, iterations, total_time, total_time / iterations);
}

int main(int argc, char **argv) {
    const char *img_path = "tests/photo.jpeg";
    int iterations = 100;

    if (argc > 1) img_path = argv[1];
    if (argc > 2) iterations = atoi(argv[2]);

    printf("--- libphash Performance Benchmark ---\n");
    printf("Image: %s\n", img_path);
    printf("Iterations: %d\n\n", iterations);

    ph_context_t *ctx;
    if (ph_create(&ctx) != PH_SUCCESS) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    double start_load = get_time_sec();
    if (ph_load_from_file(ctx, img_path) != PH_SUCCESS) {
        fprintf(stderr, "Failed to load image %s\n", img_path);
        ph_free(ctx);
        return 1;
    }
    double end_load = get_time_sec();
    printf("[INFO] Image loaded in %.4fs\n", end_load - start_load);

    benchmark_hash("aHash", ph_compute_ahash, ctx, iterations);
    benchmark_hash("dHash", ph_compute_dhash, ctx, iterations);
    benchmark_hash("pHash", ph_compute_phash, ctx, iterations);
    benchmark_hash("wHash", ph_compute_whash, ctx, iterations);

    ph_digest_t digest;
    double start_rad = get_time_sec();
    for (int i = 0; i < iterations / 10; i++) {
        ph_compute_radial_hash(ctx, &digest);
    }
    double end_rad = get_time_sec();
    printf("[BENCH] Radial     | %d iterations | Total: %.4fs | Avg: %.6fs/op\n", 
           iterations / 10, end_rad - start_rad, (end_rad - start_rad) / (iterations / 10));

    ph_free(ctx);
    printf("\nBenchmark complete.\n");

    return 0;
}
