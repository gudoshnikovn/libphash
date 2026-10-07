/* The documentation site's measuring tool (scripts/site.sh, tools/site/render.py).
 *
 * Every number and picture on the site's algorithm pages comes from this program, run
 * against the library the site documents, so a page cannot describe a computation the
 * code does not perform. It reads internal state (the grayscale image, the reduced
 * grid), so it links like the tests do.
 *
 *   site_stages ahash <image> <outdir>
 *       Writes the stages of aHash on <image>: original.ppm, gray.pgm, and ahash.json
 *       (the 8x8 grid, its mean, the 64 bits and the hash). The bits are recomputed here
 *       from the grid and checked against ph_compute_ahash(); a mismatch exits 1, so a
 *       picture of the steps is always a picture of what the library does.
 *
 *   site_stages measure <reference> <variant>...
 *       Prints one JSON object per variant: for each of the nine algorithms, the
 *       variant compared with the reference by that algorithm's own metric (similarity
 *       in [0, 1] for bit hashes, peak correlation for Radial, histogram intersection for
 *       ColorHash, L2 distance for ColorMoments), or null where it does not apply.
 */
#include "context.h"
#include "image/image.h"
#include "libphash.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *what, const char *detail) {
    fprintf(stderr, "site_stages: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    return 1;
}

static int load(ph_context_t **ctx, const char *path) {
    if (ph_create(ctx) != PH_SUCCESS) {
        return fail("ph_create failed", NULL);
    }
    ph_error_t err = ph_load_from_file(*ctx, path);
    if (err != PH_SUCCESS) {
        fprintf(stderr, "site_stages: cannot load %s: %s (%s)\n", path, ph_get_error_string(err),
                ph_get_last_error_message(*ctx));
        ph_free(*ctx);
        *ctx = NULL;
        return 1;
    }
    return 0;
}

static int write_pnm(const char *dir, const char *name, const uint8_t *px, int w, int h,
                     int channels) {
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) {
        return fail("path too long", name);
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return fail(path, strerror(errno));
    }
    fprintf(f, "P%d\n%d %d\n255\n", channels == 3 ? 6 : 5, w, h);
    size_t n = (size_t)w * (size_t)h * (size_t)channels;
    int ok = fwrite(px, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    return ok ? 0 : fail("cannot write", path);
}

/* A JSON object written field by field: `{"a": 1, "b": [2, 3]}`. The site's readers
 * (render.py) take what these helpers write, so a mode only names its fields. */
typedef struct {
    FILE *f;
    int fields;
} json_t;

static void json_key(json_t *j, const char *key) {
    fprintf(j->f, "%s\"%s\": ", j->fields++ ? ", " : "{", key);
}

static void json_int(json_t *j, const char *key, long long v) {
    json_key(j, key);
    fprintf(j->f, "%lld", v);
}

static void json_double(json_t *j, const char *key, double v) {
    json_key(j, key);
    fprintf(j->f, "%.6f", v);
}

static void json_string(json_t *j, const char *key, const char *v) {
    json_key(j, key);
    fprintf(j->f, "\"%s\"", v);
}

static void json_null(json_t *j, const char *key) {
    json_key(j, key);
    fputs("null", j->f);
}

static void json_u8s(json_t *j, const char *key, const uint8_t *v, int n) {
    json_key(j, key);
    for (int i = 0; i < n; i++) {
        fprintf(j->f, "%s%u", i ? ", " : "[", v[i]);
    }
    fputs("]", j->f);
}

/* The n bits of `bits` from the most significant one down, as 0/1. */
static void json_bits(json_t *j, const char *key, uint64_t bits, int n) {
    json_key(j, key);
    for (int i = 0; i < n; i++) {
        fprintf(j->f, "%s%d", i ? ", " : "[", (int)((bits >> (n - 1 - i)) & 1));
    }
    fputs("]", j->f);
}

static void json_hex64(json_t *j, const char *key, uint64_t v) {
    json_key(j, key);
    fprintf(j->f, "\"%016llx\"", (unsigned long long)v);
}

static void json_end(json_t *j) { fputs("}\n", j->f); }

/* Opens <dir>/<name> for a mode's JSON file. */
static FILE *open_out(const char *dir, const char *name) {
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) {
        fail("path too long", name);
        return NULL;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        fail(path, strerror(errno));
    }
    return f;
}

/* Writes original.ppm (when the image is RGB) and gray.pgm into `outdir`: the first two
 * stages of every grayscale hash. Returns the grayscale buffer, owned by `ctx`. */
static const uint8_t *write_gray_stages(ph_context_t *ctx, const char *outdir, int *status) {
    int w = ctx->image.width, h = ctx->image.height;
    if (ctx->image.channels == 3) {
        *status |= write_pnm(outdir, "original.ppm", ctx->image.raw_rgb, w, h, 3);
    }
    const uint8_t *gray = ph_get_gray(ctx);
    if (!gray) {
        fail("ph_get_gray failed", NULL);
        return NULL;
    }
    *status |= write_pnm(outdir, "gray.pgm", gray, w, h, 1);
    return gray;
}

/* site_stages ahash <image> <outdir> */
static int stages_ahash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;
    if (!write_gray_stages(ctx, outdir, &status)) {
        ph_free(ctx);
        return 1;
    }

    enum {
        N = 8,
    };

    uint8_t grid[N * N];
    if (!ph_area_downscale(ctx, N, N, grid)) {
        ph_free(ctx);
        return fail("ph_area_downscale failed", NULL);
    }
    uint64_t sum = 0;
    for (int i = 0; i < N * N; i++) {
        sum += grid[i];
    }
    uint64_t bits = 0;
    for (int i = 0; i < N * N; i++) {
        if ((uint64_t)grid[i] * (N * N) >= sum) {
            bits |= 1ULL << (63 - i);
        }
    }
    uint64_t lib = 0;
    if (ph_compute_ahash(ctx, &lib) != PH_SUCCESS) {
        ph_free(ctx);
        return fail("ph_compute_ahash failed", NULL);
    }
    ph_free(ctx);
    if (lib != bits) {
        fprintf(stderr,
                "site_stages: aHash recomputed from the grid (%016llx) differs from "
                "ph_compute_ahash() (%016llx)\n",
                (unsigned long long)bits, (unsigned long long)lib);
        return 1;
    }

    FILE *f = open_out(outdir, "ahash.json");
    if (!f) {
        return 1;
    }
    json_t j = {f, 0};
    json_int(&j, "width", w);
    json_int(&j, "height", h);
    json_int(&j, "grid_size", N);
    json_u8s(&j, "grid", grid, N * N);
    json_double(&j, "mean", (double)sum / (N * N));
    json_bits(&j, "bits", bits, N * N);
    json_hex64(&j, "hash", bits);
    json_end(&j);
    status |= fclose(f) != 0;
    return status;
}

/* --8<-- [start:compare] */
/* One algorithm's comparison of two digests by its own metric; 0 when it does not apply. */
static int compare(const ph_digest_t *a, const ph_digest_t *b, double *out) {
    switch (a->kind) {
        case PH_DIGEST_KIND_BITS:
            *out = ph_similarity_digest(a, b);
            return *out >= 0.0;
        case PH_DIGEST_KIND_COEFFICIENTS:
            return ph_radial_similarity(a, b, out) == PH_SUCCESS;
        case PH_DIGEST_KIND_HISTOGRAM:
            return ph_histogram_intersection(a, b, out) == PH_SUCCESS;
        case PH_DIGEST_KIND_VECTOR16:
            *out = ph_l2_distance(a, b);
            return *out >= 0.0;
        default:
            return 0;
    }
}

/* --8<-- [end:compare] */

/* --8<-- [start:measure] */
/* site_stages measure <reference> <variant>...: every algorithm's digest of the reference,
 * then one JSON line per variant with its comparison to the reference, per algorithm. */
static int measure(int argc, char **argv) {
    ph_context_t *ctx = NULL;
    if (load(&ctx, argv[0])) {
        return 1;
    }
    ph_digest_t ref[PH_ALGORITHM_COUNT];
    int have[PH_ALGORITHM_COUNT];
    for (int a = 0; a < PH_ALGORITHM_COUNT; a++) {
        have[a] = ph_compute_digest(ctx, (ph_algorithm_t)a, &ref[a]) == PH_SUCCESS;
    }
    ph_free(ctx);

    for (int v = 1; v < argc; v++) {
        if (load(&ctx, argv[v])) {
            return 1;
        }
        json_t j = {stdout, 0};
        json_string(&j, "file", argv[v]);
        for (int a = 0; a < PH_ALGORITHM_COUNT; a++) {
            const char *name = ph_algorithm_name((ph_algorithm_t)a);
            ph_digest_t d;
            double value = 0.0;
            if (have[a] && ph_compute_digest(ctx, (ph_algorithm_t)a, &d) == PH_SUCCESS &&
                compare(&ref[a], &d, &value)) {
                json_double(&j, name, value);
            } else {
                json_null(&j, name);
            }
        }
        json_end(&j);
        ph_free(ctx);
    }
    return 0;
}

/* --8<-- [end:measure] */

/* The modes: name, arguments, and the least number of them (a mode taking a list
 * accepts more). */
static const struct {
    const char *name;
    const char *args;
    int min_args;
    int variadic;
    int (*run)(int argc, char **argv);
} modes[] = {
    {"ahash", "<image> <outdir>", 2, 0, stages_ahash},
    {"measure", "<reference> <variant>...", 2, 1, measure},
};

int main(int argc, char **argv) {
    for (size_t m = 0; argc >= 2 && m < sizeof(modes) / sizeof(modes[0]); m++) {
        int n = argc - 2;
        if (strcmp(argv[1], modes[m].name) == 0 &&
            (n == modes[m].min_args || (modes[m].variadic && n > modes[m].min_args))) {
            return modes[m].run(n, argv + 2);
        }
    }
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        fprintf(stderr, "%s site_stages %s %s\n", m ? "      " : "usage:", modes[m].name,
                modes[m].args);
    }
    return 2;
}
