/* site_stages: the times on the site's Cost rows and tables (tools/site/measure/timing.py). */
#include "stages.h"

#include <stdlib.h>
#include <time.h>

/* --8<-- [start:time] */
/* Seconds on a clock that only moves forward for the purpose: C11's timespec_get(). */
static double seconds(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
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

/* Radial with the settings of radial_variants[variant]. */
static int time_radial(ph_context_t *ctx, const char *path, int variant) {
    (void)path;
    ph_digest_t d;
    int bad = !radial_settings(ctx, &radial_variants[variant]) ||
              ph_compute_radial_hash(ctx, &d) != PH_SUCCESS;
    return !radial_settings(ctx, &radial_variants[RADIAL_DEFAULT]) || bad;
}

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
    {"mhash_size_62", time_mhash_size, 62},
    {"mhash_size_128", time_mhash_size, 128},
    {"mhash_size_256", time_mhash_size, 256},
    {"mhash_size_512", time_mhash_size, 512},
    {"mhash_size_1024", time_mhash_size, 1024},
    {"mhash_size_2048", time_mhash_size, 2048},
    {"mhash_size_4096", time_mhash_size, 4096},
    {"radial_sigma_1", time_radial, RADIAL_SIGMA_1},
    {"radial_sigma_8", time_radial, RADIAL_SIGMA_8},
    {"radial_gamma_2", time_radial, RADIAL_GAMMA_2},
    {"radial_grid_40x32", time_radial, RADIAL_GRID_40X32},
    {"radial_grid_90x64", time_radial, RADIAL_GRID_90X64},
    {"radial_grid_360x256", time_radial, RADIAL_GRID_360X256},
    {"radial_grid_1440x1024", time_radial, RADIAL_GRID_1440X1024},
    {"radial_grid_4096x4096", time_radial, RADIAL_GRID_4096X4096},
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

/* site_stages time <image>: every case of time_cases[] on the image; the minimum and the
 * median of its runs, in milliseconds, as one JSON object, with the build that ran them. */
int mode_time(int argc, char **argv) {
    (void)argc;
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
