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

static int stages_ahash(const char *image, const char *outdir) {
    ph_context_t *ctx = NULL;
    if (load(&ctx, image)) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height, ch = ctx->image.channels;
    int status = 0;
    if (ch == 3) {
        status |= write_pnm(outdir, "original.ppm", ctx->image.raw_rgb, w, h, 3);
    }
    const uint8_t *gray = ph_get_gray(ctx);
    if (!gray) {
        ph_free(ctx);
        return fail("ph_get_gray failed", NULL);
    }
    status |= write_pnm(outdir, "gray.pgm", gray, w, h, 1);

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

    char path[4096];
    snprintf(path, sizeof(path), "%s/ahash.json", outdir);
    FILE *f = fopen(path, "w");
    if (!f) {
        return fail(path, strerror(errno));
    }
    fprintf(f, "{\"width\": %d, \"height\": %d, \"grid_size\": %d, \"grid\": [", w, h, N);
    for (int i = 0; i < N * N; i++) {
        fprintf(f, "%s%u", i ? ", " : "", grid[i]);
    }
    fprintf(f, "], \"mean\": %.6f, \"bits\": [", (double)sum / (N * N));
    for (int i = 0; i < N * N; i++) {
        fprintf(f, "%s%d", i ? ", " : "", (int)((bits >> (63 - i)) & 1));
    }
    fprintf(f, "], \"hash\": \"%016llx\"}\n", (unsigned long long)bits);
    status |= fclose(f) != 0;
    return status;
}

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
        printf("{\"file\": \"%s\"", argv[v]);
        for (int a = 0; a < PH_ALGORITHM_COUNT; a++) {
            ph_digest_t d;
            double value = 0.0;
            int ok = have[a] && ph_compute_digest(ctx, (ph_algorithm_t)a, &d) == PH_SUCCESS &&
                     compare(&ref[a], &d, &value);
            if (ok) {
                printf(", \"%s\": %.6f", ph_algorithm_name((ph_algorithm_t)a), value);
            } else {
                printf(", \"%s\": null", ph_algorithm_name((ph_algorithm_t)a));
            }
        }
        printf("}\n");
        ph_free(ctx);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "ahash") == 0) {
        return stages_ahash(argv[2], argv[3]);
    }
    if (argc >= 4 && strcmp(argv[1], "measure") == 0) {
        return measure(argc - 2, argv + 2);
    }
    fprintf(stderr, "usage: site_stages ahash <image> <outdir>\n"
                    "       site_stages measure <reference> <variant>...\n");
    return 2;
}
