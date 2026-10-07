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
 *   site_stages dhash <image> <outdir>
 *       The same for dHash: original.ppm, gray.pgm, and dhash.json (the 9x8 grid, the
 *       64 bits and the hash), checked against ph_compute_dhash(). The JSON also holds
 *       the grid and hash of the same image loaded with ph_context_set_load_grayscale(),
 *       where the decoder converts to grayscale, checked the same way.
 *
 *   site_stages phash <image> <outdir>
 *       The same for pHash: original.ppm, gray.pgm, and phash.json (the 32x32 grid, its
 *       whole DCT, the first eight DCT basis vectors, the 8x8 block, the threshold, the
 *       bits in coefficient order and the hash), checked against ph_compute_phash(); and
 *       the block, threshold and hash for every block size from 4 to 8, each checked
 *       against ph_compute_phash() with that size; and the hash of the image loaded with
 *       ph_context_set_load_grayscale(), checked the same way.
 *
 *   site_stages whash <image> <outdir>
 *       The same for wHash, in both modes and each with and without remove_max_haar_ll:
 *       original.ppm, gray.pgm, and whash.json (for each of the four, the side of the
 *       grayscale reduction, the 8x8 LL band, its median, the bits in coefficient order
 *       and the hash; without the removal also the reduction and its whole decomposition),
 *       every hash checked against ph_compute_whash() with those settings; and the hash of
 *       the image loaded with ph_context_set_load_grayscale(), checked the same way.
 *
 *   site_stages whash-modes <image>...
 *       Prints one JSON object per image: its wHash as ph_compute_whash() computes it in
 *       both modes, each with and without remove_max_haar_ll; and aHash's 8x8 area grid
 *       thresholded at its median and packed the same way, which the LL band amounts to;
 *       and its aHash, for the count of bits each sets.
 *
 *   site_stages measure <reference> <variant>...
 *       Prints one JSON object per variant: for each of the nine algorithms, the
 *       variant compared with the reference by that algorithm's own metric (similarity
 *       in [0, 1] for bit hashes, peak correlation for Radial, histogram intersection for
 *       ColorHash, L2 distance for ColorMoments), or null where it does not apply.
 *
 *   site_stages pairs <image>...
 *       The same comparison for every pair of distinct images: one JSON object per pair
 *       (i < j, in the order given), with the indices "a" and "b".
 *
 *   site_stages mhash <image> <outdir>
 *       The same for mHash: original.ppm, gray.pgm, blurred.pgm (the sigma-1 blur),
 *       resized.pgm (normalized to 512x512), equalized.pgm, response.f32 (the response to
 *       the kernel at every pixel, raw floats) and mhash.json (the kernel, the 31x31 block
 *       grid and the digest, for the defaults and for other kernel scales and sizes, each
 *       checked against ph_compute_mhash() with those parameters; the digest the
 *       definition gives evaluated pixel by pixel, in double and in float; and the digest
 *       of the image loaded with ph_context_set_load_grayscale(), checked the same way).
 *
 *   site_stages mhash-direct <image>...
 *       Prints one JSON object per image: how many bits of its mHash the definition,
 *       evaluated pixel by pixel in double and in float, gives differently from
 *       ph_compute_mhash().
 *
 *   site_stages bmh <image> <outdir>
 *       The same for BMH: original.ppm, gray.pgm, and bmh.json (for block sizes 4, 8, 16
 *       and 32: the grid of block means, its median and mean, the digest and the digest
 *       the same grid gives thresholded at its mean), every digest checked against
 *       ph_compute_bmh() with that block size; and the digest of the image loaded with
 *       ph_context_set_load_grayscale(), checked the same way.
 *
 *   site_stages bmh-variants <image>...
 *       Prints one JSON object per image: its BMH digest at each of those block sizes,
 *       as ph_compute_bmh() computes it, and the digest thresholded at the mean.
 *
 *   site_stages radial <image> <outdir>
 *       The same for Radial: original.ppm, gray.pgm, blurred.pgm (the blur, and the gamma,
 *       which is the identity at its default), sigma-<s>.pgm and gamma-<g>.pgm for the other
 *       settings shown, and radial.json (the center and radius; for the default, the
 *       variance along every line, the profile standardized, the 40 DCT coefficients and
 *       the digest, and the points each line reads; the same without the points for the
 *       other sigmas and gammas), every digest checked against ph_compute_radial_hash()
 *       with those settings; and the digest of the image loaded with
 *       ph_context_set_load_grayscale(), checked the same way.
 *
 *   site_stages radial-profiles <reference> <image>...
 *       Prints one JSON object per image, the reference first: its variance profile and
 *       digest, checked as above, and the digest compared with the reference's by
 *       ph_radial_similarity().
 *
 *   site_stages radial-variants <image>...
 *       Prints one JSON object per image: its Radial digest as ph_compute_radial_hash()
 *       computes it under each setting of radial_variants_list[].
 *
 *   site_stages color_hash <image> <outdir>
 *       The same for ColorHash: original.ppm, bins.pgm (each pixel's bin, 0 to 107), and
 *       color_hash.json (the count of every bin, the largest, and the digest, checked
 *       against ph_compute_color_hash(); for every bin, how many of the 2^24 8-bit colors
 *       fall into it and their mean; and the error ph_compute_color_hash() returns for the
 *       image loaded with ph_context_set_load_grayscale(), which must be
 *       PH_ERR_REQUIRES_COLOR).
 *
 *   site_stages time <image>
 *       Times decoding the image and every hash on it (the first hash on a loaded image,
 *       caches dropped before each run), and the variants of time_cases[]; prints the
 *       minimum and the median of the runs, in milliseconds, as one JSON object.
 *
 *   site_stages corpus <outdir>
 *       Writes the synthetic corpus of the tests (tests/src/synthetic_corpus.h) as
 *       00.ppm ... 23.ppm, so the site measures the very images the tests do.
 */
#include "context.h"
#include "hashes/hashes.h"
#include "image/image.h"
#include "libphash.h"

#include "synthetic_corpus.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* dHash of a loaded image, from its own 9x8 Mitchell grid: the bits are recomputed from
 * `grid` and checked against ph_compute_dhash(). Returns 0 on a match. */
static int dhash_stages(ph_context_t *ctx, const uint8_t *gray, uint8_t grid[72], uint64_t *bits) {
    enum {
        W = 9,
        H = 8,
    };

    if (!ph_resize_mitchell(gray, ctx->image.width, ctx->image.height, grid, W, H)) {
        return fail("ph_resize_mitchell failed", NULL);
    }
    *bits = 0;
    for (int row = 0; row < H; row++) {
        for (int col = 0; col < W - 1; col++) {
            if (grid[row * W + col] < grid[row * W + col + 1]) {
                *bits |= 1ULL << (63 - (row * (W - 1) + col));
            }
        }
    }
    uint64_t lib = 0;
    if (ph_compute_dhash(ctx, &lib) != PH_SUCCESS) {
        return fail("ph_compute_dhash failed", NULL);
    }
    if (lib != *bits) {
        fprintf(stderr,
                "site_stages: dHash recomputed from the grid (%016llx) differs from "
                "ph_compute_dhash() (%016llx)\n",
                (unsigned long long)*bits, (unsigned long long)lib);
        return 1;
    }
    return 0;
}

/* site_stages dhash <image> <outdir> */
static int stages_dhash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;
    const uint8_t *gray = write_gray_stages(ctx, outdir, &status);
    uint8_t grid[72];
    uint64_t bits = 0;
    if (!gray || dhash_stages(ctx, gray, grid, &bits)) {
        ph_free(ctx);
        return 1;
    }
    ph_free(ctx);

    /* The decoder's grayscale: a JPEG decoder converts with its own coefficients. */
    uint8_t dec_grid[72];
    uint64_t dec_bits = 0;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return fail("ph_create failed", NULL);
    }
    if (ph_context_set_load_grayscale(ctx, 1) != PH_SUCCESS ||
        ph_load_from_file(ctx, argv[0]) != PH_SUCCESS) {
        ph_free(ctx);
        return fail("cannot load in grayscale", argv[0]);
    }
    gray = ph_get_gray(ctx);
    if (!gray || dhash_stages(ctx, gray, dec_grid, &dec_bits)) {
        ph_free(ctx);
        return 1;
    }
    ph_free(ctx);

    FILE *f = open_out(outdir, "dhash.json");
    if (!f) {
        return 1;
    }
    json_t j = {f, 0};
    json_int(&j, "width", w);
    json_int(&j, "height", h);
    json_int(&j, "grid_width", 9);
    json_int(&j, "grid_height", 8);
    json_u8s(&j, "grid", grid, 72);
    json_bits(&j, "bits", bits, 64);
    json_hex64(&j, "hash", bits);
    json_u8s(&j, "grid_load_grayscale", dec_grid, 72);
    json_hex64(&j, "hash_load_grayscale", dec_bits);
    json_end(&j);
    status |= fclose(f) != 0;
    return status;
}

/* pHash's block of r x r DCT coefficients of the 32x32 grid, by the library's own
 * ph_dct2_partial(), its threshold (the median of the AC terms raised by the margin), and
 * its hash recomputed from them and checked against ph_compute_phash() with that block
 * size. Returns 0 on a match. */
static int phash_block(ph_context_t *ctx, const uint8_t *grid, int r, float block[64],
                       float *threshold, uint64_t *bits) {
    if (ph_dct2_partial(ph_get_dct_matrix_32(), grid, 32, r, block) != PH_SUCCESS) {
        return fail("ph_dct2_partial failed", NULL);
    }
    float ac[63];
    int m = r * r - 1;
    for (int i = 0; i < m; i++) {
        ac[i] = block[i + 1];
    }
    for (int i = 1; i < m; i++) { /* sorted, for the median and the range */
        for (int k = i; k > 0 && ac[k - 1] > ac[k]; k--) {
            float t = ac[k];
            ac[k] = ac[k - 1];
            ac[k - 1] = t;
        }
    }
    float median = m % 2 ? ac[m / 2] : (ac[m / 2 - 1] + ac[m / 2]) * 0.5f;
    *threshold = median + PH_PHASH_MEDIAN_MARGIN * (ac[m - 1] - ac[0]);
    *bits = 0;
    for (int i = 0; i < r * r; i++) {
        if (block[i] > *threshold) {
            *bits |= 1ULL << i;
        }
    }
    uint64_t lib = 0;
    if (ph_context_set_phash_params(ctx, 32, r) != PH_SUCCESS ||
        ph_compute_phash(ctx, &lib) != PH_SUCCESS) {
        return fail("ph_compute_phash failed", NULL);
    }
    if (lib != *bits) {
        fprintf(stderr,
                "site_stages: pHash (block %d) recomputed from the DCT (%016llx) differs from "
                "ph_compute_phash() (%016llx)\n",
                r, (unsigned long long)*bits, (unsigned long long)lib);
        return 1;
    }
    return 0;
}

static void json_floats(json_t *j, const char *key, const float *v, int n) {
    json_key(j, key);
    for (int i = 0; i < n; i++) {
        fprintf(j->f, "%s%.9g", i ? ", " : "[", (double)v[i]);
    }
    fputs("]", j->f);
}

/* The n bits of `bits` from the least significant one up, as 0/1: bit i is coefficient i
 * for pHash, which packs LSB first. */
static void json_bits_lsb(json_t *j, const char *key, uint64_t bits, int n) {
    json_key(j, key);
    for (int i = 0; i < n; i++) {
        fprintf(j->f, "%s%d", i ? ", " : "[", (int)((bits >> i) & 1));
    }
    fputs("]", j->f);
}

/* site_stages phash <image> <outdir> */
static int stages_phash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;

    enum {
        N = 32,
        MIN_R = PH_DCT_MIN_REDUCTION_SIZE,
        MAX_R = PH_DCT_MAX_REDUCTION_SIZE,
    };

    uint8_t grid[N * N];
    if (!write_gray_stages(ctx, outdir, &status) || !ph_area_downscale(ctx, N, N, grid)) {
        ph_free(ctx);
        return fail("cannot reduce the image to 32x32", NULL);
    }

    /* Every block size the API accepts; the default, 8, last, so the context is left at
     * it. */
    float blocks[MAX_R - MIN_R + 1][64], thresholds[MAX_R - MIN_R + 1];
    uint64_t hashes[MAX_R - MIN_R + 1];
    for (int r = MIN_R; r <= MAX_R; r++) {
        if (phash_block(ctx, grid, r, blocks[r - MIN_R], &thresholds[r - MIN_R],
                        &hashes[r - MIN_R])) {
            ph_free(ctx);
            return 1;
        }
    }
    ph_free(ctx);
    const float *block = blocks[MAX_R - MIN_R];

    /* The decoder's grayscale, as for dHash: the same steps on what the decoder gives. */
    uint8_t dec_grid[N * N];
    float dec_block[64], dec_threshold;
    uint64_t dec_hash = 0;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return fail("ph_create failed", NULL);
    }
    if (ph_context_set_load_grayscale(ctx, 1) != PH_SUCCESS ||
        ph_load_from_file(ctx, argv[0]) != PH_SUCCESS || !ph_area_downscale(ctx, N, N, dec_grid) ||
        phash_block(ctx, dec_grid, MAX_R, dec_block, &dec_threshold, &dec_hash)) {
        ph_free(ctx);
        return fail("cannot hash the image loaded in grayscale", argv[0]);
    }
    ph_free(ctx);

    /* The whole 32x32 transform, for the map of coefficients: the same two passes as
     * ph_dct2_partial() in the same order, over every row and column, so its top-left
     * 8x8 is the library's block bit for bit (checked). */
    const float *mat = ph_get_dct_matrix_32();
    static float temp[N * N], dct[N * N];
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            float sum = 0;
            for (int k = 0; k < N; k++) {
                sum += mat[j * N + k] * grid[i * N + k];
            }
            temp[i * N + j] = sum;
        }
    }
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            float sum = 0;
            for (int k = 0; k < N; k++) {
                sum += mat[i * N + k] * temp[k * N + j];
            }
            dct[i * N + j] = sum;
        }
    }
    for (int i = 0; i < MAX_R * MAX_R; i++) {
        if (dct[(i / MAX_R) * N + i % MAX_R] != block[i]) {
            return fail("the full DCT differs from ph_dct2_partial()", NULL);
        }
    }

    FILE *f = open_out(outdir, "phash.json");
    if (!f) {
        return 1;
    }
    json_t j = {f, 0};
    json_int(&j, "width", w);
    json_int(&j, "height", h);
    json_int(&j, "dct_size", N);
    json_int(&j, "block_size", MAX_R);
    json_u8s(&j, "grid", grid, N * N);
    json_floats(&j, "dct", dct, N * N);
    json_floats(&j, "matrix", mat, MAX_R * N); /* the first 8 basis vectors */
    json_floats(&j, "block", block, MAX_R * MAX_R);
    json_double(&j, "margin", PH_PHASH_MEDIAN_MARGIN);
    json_double(&j, "threshold", thresholds[MAX_R - MIN_R]);
    json_bits_lsb(&j, "bits", hashes[MAX_R - MIN_R], MAX_R * MAX_R);
    json_hex64(&j, "hash", hashes[MAX_R - MIN_R]);
    json_hex64(&j, "hash_load_grayscale", dec_hash);
    json_key(&j, "reductions");
    for (int r = MIN_R; r <= MAX_R; r++) {
        json_t o = {f, 0};
        fputs(r > MIN_R ? ", " : "[", f);
        json_int(&o, "size", r);
        json_floats(&o, "block", blocks[r - MIN_R], r * r);
        json_double(&o, "threshold", thresholds[r - MIN_R]);
        json_bits_lsb(&o, "bits", hashes[r - MIN_R], r * r);
        json_hex64(&o, "hash", hashes[r - MIN_R]);
        fputs("}", f);
    }
    fputs("]", f);
    json_end(&j);
    status |= fclose(f) != 0;
    return status;
}

/* ImageHash's remove_max_haar_ll on a size x size image: Haar levels down to one LL
 * coefficient, that coefficient zeroed, and the inverse levels back up, with the
 * library's own transforms in the library's order. */
static void whash_remove_max_haar_ll(float *d, int size) {
    float ta[4096], tb[4096];
    int s = size;
    while (s > 1) {
        ph_haar_2d_level(d, s, size, ta, tb);
        s /= 2;
    }
    d[0] = 0.0f;
    while (s < size) {
        s *= 2;
        ph_haar_2d_level_inverse(d, s, size, ta, tb);
    }
}

/* The median of wHash's 8x8 LL band and its bits, as ph_median_bitpack() sets them. */
static uint64_t whash_bits(const float ll[64], float *median) {
    float sorted[64];
    memcpy(sorted, ll, sizeof(sorted));
    for (int i = 1; i < 64; i++) {
        for (int k = i; k > 0 && sorted[k - 1] > sorted[k]; k--) {
            float t = sorted[k];
            sorted[k] = sorted[k - 1];
            sorted[k - 1] = t;
        }
    }
    *median = (sorted[31] + sorted[32]) * 0.5f;
    uint64_t bits = 0;
    for (int i = 0; i < 64; i++) {
        if (ll[i] > *median) {
            bits |= 1ULL << i;
        }
    }
    return bits;
}

/* One wHash computation, recomputed from its stages and checked against
 * ph_compute_whash() in the same mode and with the same removal setting: `size` is the
 * side of the grayscale reduction (16 in PH_WHASH_FAST), `grid` that reduction,
 * `coef` its decomposition in Mallat's layout (each level's LL band in the top-left
 * quarter of the previous one, its three detail bands around it), down to 8x8. */
typedef struct {
    int size, levels, remove;
    uint8_t *grid;
    float *coef;
    float ll[64], median;
    uint64_t bits;
} whash_stages_t;

static int whash_run(ph_context_t *ctx, ph_whash_mode_t mode, int remove, whash_stages_t *s) {
    int w = ctx->image.width, h = ctx->image.height, min_dim = w < h ? w : h;
    s->remove = remove;
    if (mode == PH_WHASH_FAST) {
        s->size = 2 * PH_CORE_HASH_SIZE;
    } else {
        s->size = 1;
        while (s->size * 2 <= min_dim) {
            s->size *= 2;
        }
        if (s->size < PH_CORE_HASH_SIZE) {
            s->size = PH_CORE_HASH_SIZE;
        }
    }
    if (s->size > 4096) {
        return fail("image too large for the wHash stages", NULL);
    }
    const size_t n = (size_t)s->size * (size_t)s->size;
    s->grid = malloc(n);
    s->coef = malloc(n * sizeof(float));
    if (!s->grid || !s->coef) {
        return fail("out of memory", NULL);
    }
    const uint8_t *gray = ph_get_gray(ctx);
    int ok = mode == PH_WHASH_FAST ? ph_area_downscale(ctx, s->size, s->size, s->grid)
                                   : gray && ph_resize_box(gray, w, h, s->grid, s->size, s->size);
    if (!ok) {
        return fail("cannot reduce the image for wHash", NULL);
    }
    for (size_t i = 0; i < n; i++) {
        s->coef[i] = s->grid[i] / 255.0f;
    }
    if (remove) {
        whash_remove_max_haar_ll(s->coef, s->size);
    }
    float ta[4096], tb[4096];
    s->levels = 0;
    for (int size = s->size; size > PH_CORE_HASH_SIZE; size /= 2) {
        ph_haar_2d_level(s->coef, size, s->size, ta, tb);
        s->levels++;
    }
    for (int i = 0; i < 64; i++) {
        s->ll[i] = s->coef[(i / 8) * s->size + i % 8];
    }
    s->bits = whash_bits(s->ll, &s->median);

    uint64_t lib = 0;
    if (ph_context_set_whash_mode(ctx, mode) != PH_SUCCESS ||
        ph_context_set_whash_remove_max_haar_ll(ctx, remove) != PH_SUCCESS ||
        ph_compute_whash(ctx, &lib) != PH_SUCCESS) {
        return fail("ph_compute_whash failed", NULL);
    }
    if (lib != s->bits) {
        fprintf(stderr,
                "site_stages: wHash (%s, removal %s) recomputed from the LL band (%016llx) "
                "differs from ph_compute_whash() (%016llx)\n",
                mode == PH_WHASH_FAST ? "fast" : "full", remove ? "on" : "off",
                (unsigned long long)s->bits, (unsigned long long)lib);
        return 1;
    }
    return 0;
}

static void whash_json(json_t *j, const char *key, const whash_stages_t *s, int with_coef) {
    json_key(j, key);
    json_t o = {j->f, 0};
    json_int(&o, "size", s->size);
    json_int(&o, "levels", s->levels);
    json_int(&o, "remove_max_haar_ll", s->remove);
    if (with_coef) {
        json_u8s(&o, "grid", s->grid, s->size * s->size);
        json_floats(&o, "coef", s->coef, s->size * s->size);
    }
    json_floats(&o, "ll", s->ll, 64);
    json_key(&o, "median");
    fprintf(j->f, "%.9g", (double)s->median); /* exactly the float, for ties */
    json_bits_lsb(&o, "bits", s->bits, 64);
    json_hex64(&o, "hash", s->bits);
    fputs("}", j->f);
}

/* site_stages whash <image> <outdir> */
static int stages_whash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;
    whash_stages_t runs[4] = {{0}};

    static const struct {
        ph_whash_mode_t mode;
        int remove;
        const char *key;
    } kinds[4] = {{PH_WHASH_FAST, 0, "fast"},
                  {PH_WHASH_FAST, 1, "fast_removed"},
                  {PH_WHASH_FULL, 0, "full"},
                  {PH_WHASH_FULL, 1, "full_removed"}};

    int bad = !write_gray_stages(ctx, outdir, &status);
    for (int k = 0; k < 4 && !bad; k++) {
        bad = whash_run(ctx, kinds[k].mode, kinds[k].remove, &runs[k]);
    }
    ph_free(ctx);

    /* The decoder's grayscale, as for dHash: the default mode on what the decoder gives. */
    whash_stages_t dec = {0};
    if (!bad) {
        bad =
            ph_create(&ctx) != PH_SUCCESS || ph_context_set_load_grayscale(ctx, 1) != PH_SUCCESS ||
            ph_load_from_file(ctx, argv[0]) != PH_SUCCESS || whash_run(ctx, PH_WHASH_FAST, 0, &dec);
        ph_free(ctx);
    }

    FILE *f = bad ? NULL : open_out(outdir, "whash.json");
    if (f) {
        json_t j = {f, 0};
        json_int(&j, "width", w);
        json_int(&j, "height", h);
        for (int k = 0; k < 4; k++) {
            whash_json(&j, kinds[k].key, &runs[k], !kinds[k].remove);
        }
        json_hex64(&j, "hash_load_grayscale", dec.bits);
        json_end(&j);
        status |= fclose(f) != 0;
    }
    for (int k = 0; k < 4; k++) {
        free(runs[k].grid);
        free(runs[k].coef);
    }
    free(dec.grid);
    free(dec.coef);
    return bad || !f ? 1 : status;
}

/* site_stages whash-modes <image>...: the wHash of every image in both modes, each with
 * and without remove_max_haar_ll, as the library computes them: one JSON line per image. */
static int whash_modes(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        ph_context_t *ctx = NULL;
        if (load(&ctx, argv[i])) {
            return 1;
        }
        json_t j = {stdout, 0};
        json_string(&j, "file", argv[i]);
        static const char *keys[2][2] = {{"fast", "fast_removed"}, {"full", "full_removed"}};
        for (int mode = 0; mode < 2; mode++) {
            for (int remove = 0; remove < 2; remove++) {
                uint64_t hash = 0;
                if (ph_context_set_whash_mode(ctx, (ph_whash_mode_t)mode) != PH_SUCCESS ||
                    ph_context_set_whash_remove_max_haar_ll(ctx, remove) != PH_SUCCESS ||
                    ph_compute_whash(ctx, &hash) != PH_SUCCESS) {
                    ph_free(ctx);
                    return fail("ph_compute_whash failed", argv[i]);
                }
                json_hex64(&j, keys[mode][remove], hash);
            }
        }
        /* aHash's 8x8 area grid thresholded at its median instead of its mean, packed as
         * wHash packs its LL band: what the LL band amounts to. */
        uint8_t grid[64];
        float values[64], median;
        if (!ph_area_downscale(ctx, 8, 8, grid)) {
            ph_free(ctx);
            return fail("ph_area_downscale failed", argv[i]);
        }
        for (int k = 0; k < 64; k++) {
            values[k] = grid[k];
        }
        json_hex64(&j, "grid_median", whash_bits(values, &median));
        uint64_t ahash = 0;
        if (ph_compute_ahash(ctx, &ahash) != PH_SUCCESS) {
            ph_free(ctx);
            return fail("ph_compute_ahash failed", argv[i]);
        }
        json_hex64(&j, "ahash", ahash);
        json_end(&j);
        ph_free(ctx);
    }
    return 0;
}

/* mHash's response to its kernel at every pixel of the n x n image, edges replicated, the
 * taps summed in raster order: in double, as the definition reads; or in float, as a
 * direct implementation of it would compute. */
static void mhash_response(const uint8_t *img, int n, const float *kernel, int side, int in_double,
                           float *out) {
    const int half = side / 2;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            double accd = 0.0;
            float accf = 0.0f;
            for (int ky = 0; ky < side; ky++) {
                int sy = y + ky - half < 0 ? 0 : y + ky - half >= n ? n - 1 : y + ky - half;
                for (int kx = 0; kx < side; kx++) {
                    int sx = x + kx - half < 0 ? 0 : x + kx - half >= n ? n - 1 : x + kx - half;
                    float k = kernel[ky * side + kx];
                    uint8_t v = img[(size_t)sy * (size_t)n + (size_t)sx];
                    if (in_double) {
                        accd += (double)k * (double)v;
                    } else {
                        accf += k * (float)v;
                    }
                }
            }
            out[(size_t)y * (size_t)n + (size_t)x] = in_double ? (float)accd : accf;
        }
    }
}

/* The 576 bits of mHash from its 31x31 block grid, packed as ph_compute_mhash() packs
 * them: nine per 3x3 window at stride 4, each against its window's mean, MSB first. */
static void mhash_bits(const float *blocks, uint8_t digest[PH_MH_BYTES]) {
    memset(digest, 0, PH_MH_BYTES);
    int bit = 0;
    for (int wy = 0; wy < PH_MH_WINDOWS_PER_AXIS; wy++) {
        for (int wx = 0; wx < PH_MH_WINDOWS_PER_AXIS; wx++) {
            const float *w =
                &blocks[wy * PH_MH_WINDOW_STRIDE * PH_MH_GRID + wx * PH_MH_WINDOW_STRIDE];
            float sum = 0.0f;
            for (int y = 0; y < PH_MH_WINDOW; y++) {
                for (int x = 0; x < PH_MH_WINDOW; x++) {
                    sum += w[y * PH_MH_GRID + x];
                }
            }
            float mean = sum / (float)(PH_MH_WINDOW * PH_MH_WINDOW);
            for (int y = 0; y < PH_MH_WINDOW; y++) {
                for (int x = 0; x < PH_MH_WINDOW; x++, bit++) {
                    if (w[y * PH_MH_GRID + x] > mean) {
                        digest[bit / 8] |= (uint8_t)(0x80u >> (bit % 8));
                    }
                }
            }
        }
    }
}

/* The block grid summed from a per-pixel response, in the response's precision. */
static void mhash_blocks_from(const float *response, int n, int block, int in_double,
                              float *blocks) {
    for (int by = 0; by < PH_MH_GRID; by++) {
        for (int bx = 0; bx < PH_MH_GRID; bx++) {
            double sd = 0.0;
            float sf = 0.0f;
            for (int y = by * block; y < (by + 1) * block; y++) {
                for (int x = bx * block; x < (bx + 1) * block; x++) {
                    float v = response[(size_t)y * (size_t)n + (size_t)x];
                    sd += v;
                    sf += v;
                }
            }
            blocks[by * PH_MH_GRID + bx] = in_double ? (float)sd : sf;
        }
    }
}

static int bits_apart(const uint8_t *a, const uint8_t *b, int bytes) {
    int n = 0;
    for (int i = 0; i < bytes; i++) {
        n += __builtin_popcount((unsigned)(a[i] ^ b[i]));
    }
    return n;
}

/* One mHash computation with the given parameters, recomputed from its stages and checked
 * against ph_compute_mhash() with the same parameters: `blurred` is the grayscale image
 * after the sigma-1 blur, which no parameter changes. `norm` (size x size) receives the
 * normalized image, equalized. */
typedef struct {
    float alpha, level;
    int size, block, side;
    float kernel[PH_MH_MAX_KERNEL_SIDE * PH_MH_MAX_KERNEL_SIDE];
    float blocks[PH_MH_GRID * PH_MH_GRID];
    uint8_t digest[PH_MH_BYTES];
    uint8_t *norm, *resized;
} mhash_stages_t;

static int mhash_run(ph_context_t *ctx, const uint8_t *blurred, float alpha, float level, int size,
                     mhash_stages_t *s) {
    s->alpha = alpha;
    s->level = level;
    s->size = size;
    s->block = size / PH_MH_GRID;
    const size_t npix = (size_t)size * (size_t)size;
    s->norm = malloc(npix);
    s->resized = malloc(npix);
    if (!s->norm || !s->resized) {
        return fail("out of memory", NULL);
    }
    if (!ph_resize_mitchell(blurred, ctx->image.width, ctx->image.height, s->resized, size, size)) {
        return fail("ph_resize_mitchell failed", NULL);
    }
    memcpy(s->norm, s->resized, npix);
    ph_equalize_histogram(s->norm, npix, PH_MH_EQUALIZE_LEVELS);
    s->side = ph_mh_kernel(alpha, level, s->kernel, PH_MH_MAX_KERNEL_SIDE);
    if (s->side <= 0) {
        return fail("ph_mh_kernel refused the parameters", NULL);
    }
    uint8_t *scratch = malloc(ph_mh_block_sums_scratch(size, s->side / 2));
    if (!scratch) {
        return fail("out of memory", NULL);
    }
    ph_mh_block_sums(s->norm, size, s->block, s->kernel, s->side, scratch, s->blocks);
    free(scratch);
    mhash_bits(s->blocks, s->digest);

    ph_digest_t lib;
    if (ph_context_set_mhash_params(ctx, alpha, level, size) != PH_SUCCESS ||
        ph_compute_mhash(ctx, &lib) != PH_SUCCESS) {
        return fail("ph_compute_mhash failed", NULL);
    }
    if (lib.size != PH_MH_BYTES || memcmp(lib.data, s->digest, PH_MH_BYTES) != 0) {
        fprintf(stderr,
                "site_stages: mHash (alpha %g, level %g, size %d) recomputed from the block "
                "grid differs from ph_compute_mhash() in %d bits\n",
                (double)alpha, (double)level, size, bits_apart(lib.data, s->digest, PH_MH_BYTES));
        return 1;
    }
    return 0;
}

static void json_hexbytes(json_t *j, const char *key, const uint8_t *v, int n) {
    json_key(j, key);
    fputc('"', j->f);
    for (int i = 0; i < n; i++) {
        fprintf(j->f, "%02x", v[i]);
    }
    fputc('"', j->f);
}

/* One run as a JSON object: the value of `key` in `j`, or, with no key, a bare object. */
static void mhash_json(json_t *j, const char *key, const mhash_stages_t *s, int with_kernel) {
    if (key) {
        json_key(j, key);
    }
    json_t o = {j->f, 0};
    json_double(&o, "alpha", s->alpha);
    json_double(&o, "level", s->level);
    json_int(&o, "size", s->size);
    json_int(&o, "block", s->block);
    json_int(&o, "side", s->side);
    if (with_kernel) {
        json_floats(&o, "kernel", s->kernel, s->side * s->side);
    }
    json_floats(&o, "blocks", s->blocks, PH_MH_GRID * PH_MH_GRID);
    json_hexbytes(&o, "digest", s->digest, PH_MH_BYTES);
    fputs("}", j->f);
}

/* Writes n floats to <dir>/<name>, raw, in the machine's byte order. */
static int write_f32(const char *dir, const char *name, const float *v, size_t n) {
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) {
        return fail("path too long", name);
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return fail(path, strerror(errno));
    }
    int ok = fwrite(v, sizeof(float), n, f) == n;
    ok = (fclose(f) == 0) && ok;
    return ok ? 0 : fail("cannot write", path);
}

/* The grayscale image of `ctx` blurred at mHash's sigma, as ph_compute_mhash() blurs it;
 * malloc'd. */
static uint8_t *mhash_blur(ph_context_t *ctx) {
    size_t n = (size_t)ctx->image.width * (size_t)ctx->image.height;
    const uint8_t *gray = ph_get_gray(ctx);
    uint8_t *out = malloc(n);
    float *scratch = malloc(n * sizeof(float));
    if (gray && out && scratch) {
        ph_gaussian_blur_sigma(gray, ctx->image.width, ctx->image.height, PH_MH_BLUR_SIGMA, scratch,
                               out);
    } else {
        free(out);
        out = NULL;
    }
    free(scratch);
    return out;
}

/* site_stages mhash <image> <outdir> */
static int stages_mhash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;

    /* The default last among the scales, so each list ends at the default. */
    static const float levels[] = {0.0f, 2.0f, PH_MH_LEVEL};
    static const int sizes[] = {256, 1024, PH_MH_IMAGE_SIZE};

    enum {
        NL = sizeof(levels) / sizeof(levels[0]),
        NS = sizeof(sizes) / sizeof(sizes[0]),
    };

    mhash_stages_t by_level[NL] = {{0}}, by_size[NS] = {{0}};
    uint8_t *blurred = NULL;
    float *response = NULL, *direct = NULL;
    int bad = !write_gray_stages(ctx, outdir, &status) || !(blurred = mhash_blur(ctx));
    for (int k = 0; k < NL && !bad; k++) {
        bad = mhash_run(ctx, blurred, PH_MH_ALPHA, levels[k], PH_MH_IMAGE_SIZE, &by_level[k]);
    }
    for (int k = 0; k < NS && !bad; k++) {
        bad = mhash_run(ctx, blurred, PH_MH_ALPHA, PH_MH_LEVEL, sizes[k], &by_size[k]);
    }
    const mhash_stages_t *def = &by_level[NL - 1];
    const int n = PH_MH_IMAGE_SIZE;

    /* The response at every pixel, as the definition computes it, in double and in float;
     * and the bits each gives when summed over the blocks directly. */
    float direct_blocks[PH_MH_GRID * PH_MH_GRID];
    uint8_t digest_double[PH_MH_BYTES], digest_float[PH_MH_BYTES];
    if (!bad) {
        response = malloc((size_t)n * n * sizeof(float));
        direct = malloc((size_t)n * n * sizeof(float));
        bad = !response || !direct;
    }
    if (!bad) {
        mhash_response(def->norm, n, def->kernel, def->side, 1, response);
        mhash_blocks_from(response, n, def->block, 1, direct_blocks);
        mhash_bits(direct_blocks, digest_double);
        mhash_response(def->norm, n, def->kernel, def->side, 0, direct);
        mhash_blocks_from(direct, n, def->block, 0, direct_blocks);
        mhash_bits(direct_blocks, digest_float);
        status |= write_pnm(outdir, "blurred.pgm", blurred, w, h, 1);
        status |= write_pnm(outdir, "resized.pgm", def->resized, n, n, 1);
        status |= write_pnm(outdir, "equalized.pgm", def->norm, n, n, 1);
        status |= write_f32(outdir, "response.f32", response, (size_t)n * n);
    }
    ph_free(ctx);

    /* The decoder's grayscale, as for dHash: the default parameters on what the decoder
     * gives. */
    mhash_stages_t dec = {0};
    uint8_t *dec_blurred = NULL;
    if (!bad) {
        bad = ph_create(&ctx) != PH_SUCCESS ||
              ph_context_set_load_grayscale(ctx, 1) != PH_SUCCESS ||
              ph_load_from_file(ctx, argv[0]) != PH_SUCCESS || !(dec_blurred = mhash_blur(ctx)) ||
              mhash_run(ctx, dec_blurred, PH_MH_ALPHA, PH_MH_LEVEL, PH_MH_IMAGE_SIZE, &dec);
        ph_free(ctx);
    }

    FILE *f = bad ? NULL : open_out(outdir, "mhash.json");
    if (f) {
        json_t j = {f, 0};
        json_int(&j, "width", w);
        json_int(&j, "height", h);
        json_int(&j, "grid", PH_MH_GRID);
        json_int(&j, "window", PH_MH_WINDOW);
        json_int(&j, "stride", PH_MH_WINDOW_STRIDE);
        mhash_json(&j, "default", def, 1);
        json_key(&j, "levels");
        for (int k = 0; k < NL; k++) {
            fputs(k ? ", " : "[", f);
            mhash_json(&j, NULL, &by_level[k], 1);
        }
        fputs("]", f);
        json_key(&j, "sizes");
        for (int k = 0; k < NS; k++) {
            fputs(k ? ", " : "[", f);
            mhash_json(&j, NULL, &by_size[k], 0);
        }
        fputs("]", f);
        json_hexbytes(&j, "digest_direct_double", digest_double, PH_MH_BYTES);
        json_hexbytes(&j, "digest_direct_float", digest_float, PH_MH_BYTES);
        json_hexbytes(&j, "digest_load_grayscale", dec.digest, PH_MH_BYTES);
        json_end(&j);
        status |= fclose(f) != 0;
    }
    for (int k = 0; k < NL; k++) {
        free(by_level[k].norm);
        free(by_level[k].resized);
    }
    for (int k = 0; k < NS; k++) {
        free(by_size[k].norm);
        free(by_size[k].resized);
    }
    free(dec.norm);
    free(dec.resized);
    free(blurred);
    free(dec_blurred);
    free(response);
    free(direct);
    return bad || !f ? 1 : status;
}

/* site_stages mhash-direct <image>...: for every image, how many of mHash's bits the
 * definition evaluated pixel by pixel gives differently from ph_compute_mhash(), with the
 * response computed in double and in float. One JSON line per image. */
static int mhash_direct(int argc, char **argv) {
    const int n = PH_MH_IMAGE_SIZE;
    float *response = malloc((size_t)n * n * sizeof(float));
    if (!response) {
        return fail("out of memory", NULL);
    }
    for (int i = 0; i < argc; i++) {
        ph_context_t *ctx = NULL;
        mhash_stages_t s = {0};
        uint8_t *blurred = NULL;
        if (load(&ctx, argv[i])) {
            free(response);
            return 1;
        }
        int bad = !(blurred = mhash_blur(ctx)) ||
                  mhash_run(ctx, blurred, PH_MH_ALPHA, PH_MH_LEVEL, n, &s);
        ph_free(ctx);
        free(blurred);
        if (!bad) {
            float blocks[PH_MH_GRID * PH_MH_GRID];
            uint8_t digest[PH_MH_BYTES];
            json_t j = {stdout, 0};
            json_string(&j, "file", argv[i]);
            for (int in_double = 1; in_double >= 0; in_double--) {
                mhash_response(s.norm, n, s.kernel, s.side, in_double, response);
                mhash_blocks_from(response, n, s.block, in_double, blocks);
                mhash_bits(blocks, digest);
                json_int(&j, in_double ? "double" : "float",
                         bits_apart(digest, s.digest, PH_MH_BYTES));
            }
            json_end(&j);
        }
        free(s.norm);
        free(s.resized);
        if (bad) {
            free(response);
            return 1;
        }
    }
    free(response);
    return 0;
}

/* --8<-- [start:bmh] */
/* One BMH computation at a given block size, recomputed from its grid and checked against
 * ph_compute_bmh() with that size: the grid of block means, the median as the library
 * takes it (the upper of the two central values), the digest (bit i set where block i >=
 * the median, LSB first within each byte); and, for comparison, the digest thresholded
 * at the mean of the blocks instead, packed the same way. */
typedef struct {
    int size;
    uint8_t grid[PH_BLOCK_MAX_SIZE * PH_BLOCK_MAX_SIZE];
    int median;
    double mean;
    uint8_t digest[PH_DIGEST_MAX_BYTES], digest_mean[PH_DIGEST_MAX_BYTES];
} bmh_stages_t;

static int bmh_run(ph_context_t *ctx, int size, bmh_stages_t *s) {
    const int n = size * size, bytes = (n + 7) / 8;
    s->size = size;
    if (!ph_area_downscale(ctx, size, size, s->grid)) {
        return fail("ph_area_downscale failed", NULL);
    }
    int count[256] = {0}, seen = 0;
    uint64_t sum = 0;
    for (int i = 0; i < n; i++) {
        count[s->grid[i]]++;
        sum += s->grid[i];
    }
    s->median = 255;
    for (int v = 0; v < 256; v++) {
        seen += count[v];
        if (seen > n / 2) {
            s->median = v;
            break;
        }
    }
    s->mean = (double)sum / n;
    memset(s->digest, 0, sizeof(s->digest));
    memset(s->digest_mean, 0, sizeof(s->digest_mean));
    for (int i = 0; i < n; i++) {
        if (s->grid[i] >= s->median) {
            s->digest[i / 8] |= (uint8_t)(1u << (i % 8));
        }
        if ((uint64_t)s->grid[i] * (uint64_t)n >= sum) {
            s->digest_mean[i / 8] |= (uint8_t)(1u << (i % 8));
        }
    }

    ph_digest_t lib;
    int bad = ph_context_set_block_params(ctx, size) != PH_SUCCESS ||
              ph_compute_bmh(ctx, &lib) != PH_SUCCESS;
    if (ph_context_set_block_params(ctx, PH_BLOCK_SIZE) != PH_SUCCESS || bad) {
        return fail("ph_compute_bmh failed", NULL);
    }
    if (lib.size != bytes || memcmp(lib.data, s->digest, (size_t)bytes) != 0) {
        fprintf(stderr,
                "site_stages: BMH (block_size %d) recomputed from the grid differs from "
                "ph_compute_bmh() in %d bits\n",
                size, bits_apart(lib.data, s->digest, bytes));
        return 1;
    }
    return 0;
}

/* --8<-- [end:bmh] */

/* The block sizes `site_stages bmh` draws, the default among them. */
static const int bmh_sizes[] = {4, 8, 16, 32};

/* site_stages bmh <image> <outdir> */
static int stages_bmh(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;

    enum {
        N_SIZES = sizeof(bmh_sizes) / sizeof(bmh_sizes[0]),
    };

    bmh_stages_t runs[N_SIZES];
    int bad = !write_gray_stages(ctx, outdir, &status);
    for (int k = 0; k < N_SIZES && !bad; k++) {
        bad = bmh_run(ctx, bmh_sizes[k], &runs[k]);
    }
    ph_free(ctx);

    /* The decoder's grayscale, as for the other hashes: the default size. */
    bmh_stages_t dec;
    if (!bad) {
        bad = ph_create(&ctx) != PH_SUCCESS ||
              ph_context_set_load_grayscale(ctx, 1) != PH_SUCCESS ||
              ph_load_from_file(ctx, argv[0]) != PH_SUCCESS || bmh_run(ctx, PH_BLOCK_SIZE, &dec);
        ph_free(ctx);
    }

    FILE *f = bad ? NULL : open_out(outdir, "bmh.json");
    if (f) {
        json_t j = {f, 0};
        json_int(&j, "width", w);
        json_int(&j, "height", h);
        json_int(&j, "default", PH_BLOCK_SIZE);
        json_key(&j, "sizes");
        fputc('[', f);
        for (int k = 0; k < N_SIZES; k++) {
            const bmh_stages_t *s = &runs[k];
            const int n = s->size * s->size;
            json_t o = {f, 0};
            if (k) {
                fputs(", ", f);
            }
            json_int(&o, "size", s->size);
            json_u8s(&o, "grid", s->grid, n);
            json_int(&o, "median", s->median);
            json_double(&o, "mean", s->mean);
            json_hexbytes(&o, "digest", s->digest, (n + 7) / 8);
            json_hexbytes(&o, "digest_mean", s->digest_mean, (n + 7) / 8);
            fputc('}', f);
        }
        fputc(']', f);
        json_hexbytes(&j, "digest_load_grayscale", dec.digest, PH_BLOCK_SIZE * PH_BLOCK_SIZE / 8);
        json_end(&j);
        status |= fclose(f) != 0;
    }
    return bad || !f ? 1 : status;
}

/* --8<-- [start:bmh-variants] */
/* site_stages bmh-variants <image>...: for every image, the BMH digest at each block
 * size of bmh_sizes[], as ph_compute_bmh() computes it, and the same grid thresholded
 * at its mean: one JSON line per image. */
static int bmh_variants(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        ph_context_t *ctx = NULL;
        if (load(&ctx, argv[i])) {
            return 1;
        }
        json_t j = {stdout, 0};
        json_string(&j, "file", argv[i]);
        for (size_t k = 0; k < sizeof(bmh_sizes) / sizeof(bmh_sizes[0]); k++) {
            bmh_stages_t s;
            if (bmh_run(ctx, bmh_sizes[k], &s)) {
                ph_free(ctx);
                return 1;
            }
            char key[32];
            const int bytes = (s.size * s.size + 7) / 8;
            snprintf(key, sizeof(key), "median_%d", s.size);
            json_hexbytes(&j, key, s.digest, bytes);
            snprintf(key, sizeof(key), "mean_%d", s.size);
            json_hexbytes(&j, key, s.digest_mean, bytes);
        }
        json_end(&j);
        ph_free(ctx);
    }
    return 0;
}

/* --8<-- [end:bmh-variants] */

/* --8<-- [start:radial] */
/* One Radial computation with the given settings, recomputed from its stages and checked
 * against ph_compute_radial_hash() with the same settings: the grayscale image blurred at
 * `sigma` and put through `gamma`; the variance along each of `projections` lines through
 * the center, `samples` points on each; the profile's mean and spread; the profile
 * standardized; its first PH_RADIAL_COEFFS DCT coefficients; and the digest, the
 * coefficients mapped onto 0..255 by their own minimum and maximum (all zero when the
 * profile has no spread worth the name). */
typedef struct {
    int projections, samples;
    float sigma, gamma;
    double center_x, center_y, radius;
    double *variance, *standardized; /* projections each */
    double mean, spread_sq;
    int structure;
    double coefficients[PH_RADIAL_COEFFS];
    uint8_t digest[PH_RADIAL_COEFFS];
    uint8_t *blurred; /* width x height */
} radial_stages_t;

static void radial_free(radial_stages_t *s) {
    free(s->variance);
    free(s->standardized);
    free(s->blurred);
    s->variance = s->standardized = NULL;
    s->blurred = NULL;
}

static int radial_settings(ph_context_t *ctx, int projections, int samples, float sigma,
                           float gamma) {
    return ph_context_set_radial_params(ctx, projections, samples, sigma) == PH_SUCCESS &&
           ph_context_set_gamma(ctx, gamma) == PH_SUCCESS;
}

static int radial_run(ph_context_t *ctx, int projections, int samples, float sigma, float gamma,
                      radial_stages_t *s) {
    const int w = ctx->image.width, h = ctx->image.height;
    const size_t npix = (size_t)w * (size_t)h;
    memset(s, 0, sizeof(*s));
    s->projections = projections;
    s->samples = samples;
    s->sigma = sigma;
    s->gamma = gamma;
    s->variance = malloc((size_t)projections * sizeof(double));
    s->standardized = calloc((size_t)projections, sizeof(double));
    s->blurred = malloc(npix);
    float *scratch = malloc(npix * sizeof(float));
    const uint8_t *gray = ph_get_gray(ctx);
    if (!s->variance || !s->standardized || !s->blurred || !scratch || !gray ||
        !radial_settings(ctx, projections, samples, sigma, gamma)) {
        free(scratch);
        return fail("cannot set up Radial", NULL);
    }
    ph_gaussian_blur_sigma(gray, w, h, sigma, scratch, s->blurred);
    free(scratch);
    ph_apply_gamma(ctx, s->blurred, w, h);

    s->center_x = w / 2.0;
    s->center_y = h / 2.0;
    s->radius = (w < h ? w : h) / 2.0;
    double sum = 0.0, sum_sq = 0.0;
    for (int i = 0; i < projections; i++) {
        double theta = i * M_PI / projections;
        s->variance[i] =
            ph_projection_variance(s->blurred, w, h, s->center_x, s->center_y, s->radius,
                                   (float)cos(theta), (float)sin(theta), samples);
        sum += s->variance[i];
        sum_sq += s->variance[i] * s->variance[i];
    }
    s->mean = sum / projections;
    s->spread_sq = sum_sq / projections - s->mean * s->mean;
    s->structure = s->mean > PH_RADIAL_MIN_MEAN_VARIANCE &&
                   s->spread_sq > PH_RADIAL_MIN_RELATIVE_SPREAD * s->mean * s->mean;
    if (s->structure) {
        for (int i = 0; i < projections; i++) {
            s->standardized[i] = (s->variance[i] - s->mean) / sqrt(s->spread_sq);
        }
        if (ph_dct1d_partial(s->standardized, projections, PH_RADIAL_COEFFS, s->coefficients) !=
            PH_SUCCESS) {
            return fail("ph_dct1d_partial failed", NULL);
        }
        double lo = s->coefficients[0], hi = s->coefficients[0];
        for (int k = 1; k < PH_RADIAL_COEFFS; k++) {
            lo = s->coefficients[k] < lo ? s->coefficients[k] : lo;
            hi = s->coefficients[k] > hi ? s->coefficients[k] : hi;
        }
        for (int k = 0; k < PH_RADIAL_COEFFS; k++) {
            s->digest[k] = (uint8_t)(255.0 * (s->coefficients[k] - lo) / (hi - lo));
        }
    }

    ph_digest_t lib;
    int bad = ph_compute_radial_hash(ctx, &lib) != PH_SUCCESS;
    if (!radial_settings(ctx, PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, PH_RADIAL_DEFAULT_SIGMA,
                         1.0f) ||
        bad) {
        return fail("ph_compute_radial_hash failed", NULL);
    }
    if (lib.size != PH_RADIAL_COEFFS || memcmp(lib.data, s->digest, PH_RADIAL_COEFFS) != 0) {
        fprintf(stderr,
                "site_stages: Radial (%d x %d, sigma %g, gamma %g) recomputed from its stages "
                "differs from ph_compute_radial_hash()\n",
                projections, samples, (double)sigma, (double)gamma);
        return 1;
    }
    return 0;
}

/* --8<-- [end:radial] */

static void json_doubles(json_t *j, const char *key, const double *v, int n) {
    json_key(j, key);
    for (int i = 0; i < n; i++) {
        fprintf(j->f, "%s%.9g", i ? ", " : "[", v[i]);
    }
    fputs("]", j->f);
}

/* One run as a bare JSON object; the profile only when asked for. */
static void radial_json(FILE *f, const radial_stages_t *s, int with_profile) {
    json_t o = {f, 0};
    json_int(&o, "projections", s->projections);
    json_int(&o, "samples", s->samples);
    json_double(&o, "sigma", s->sigma);
    json_double(&o, "gamma", s->gamma);
    if (with_profile) {
        json_doubles(&o, "variance", s->variance, s->projections);
        json_doubles(&o, "standardized", s->standardized, s->projections);
    }
    json_double(&o, "mean", s->mean);
    json_double(&o, "relative_spread", s->mean > 0.0 ? s->spread_sq / (s->mean * s->mean) : 0.0);
    json_int(&o, "structure", s->structure);
    json_doubles(&o, "coefficients", s->coefficients, PH_RADIAL_COEFFS);
    json_hexbytes(&o, "digest", s->digest, PH_RADIAL_COEFFS);
    fputs("}", f);
}

/* The points one projection reads, as ph_projection_variance() places them: `samples`
 * points spaced radius / (samples / 2) apart from -radius on, each value bilinear, -1
 * where the point falls outside the image. */
static void radial_line(const radial_stages_t *s, int w, int h, int i, float *out) {
    double theta = i * M_PI / s->projections;
    float c = (float)cos(theta), sn = (float)sin(theta);
    float half = (float)s->samples / 2.0f, scale = (float)s->radius / half;
    for (int r = 0; r < s->samples; r++) {
        float dist = ((float)r - half) * scale;
        out[r] = ph_get_pixel_bilinear(s->blurred, w, h, (float)s->center_x + dist * c,
                                       (float)s->center_y + dist * sn);
    }
}

/* The settings `site_stages radial` shows besides the default, one at a time. */
static const float radial_sigmas[] = {1.0f, 8.0f};
static const float radial_gammas[] = {0.5f, 2.0f};

/* site_stages radial <image> <outdir> */
static int stages_radial(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;

    enum {
        N_SIGMAS = sizeof(radial_sigmas) / sizeof(radial_sigmas[0]),
        N_GAMMAS = sizeof(radial_gammas) / sizeof(radial_gammas[0]),
    };

    radial_stages_t def, sig[N_SIGMAS], gam[N_GAMMAS], dec;
    memset(sig, 0, sizeof(sig));
    memset(gam, 0, sizeof(gam));
    memset(&dec, 0, sizeof(dec));
    int bad = !write_gray_stages(ctx, outdir, &status) ||
              radial_run(ctx, PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, PH_RADIAL_DEFAULT_SIGMA,
                         1.0f, &def);
    for (int k = 0; k < N_SIGMAS && !bad; k++) {
        bad = radial_run(ctx, PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, radial_sigmas[k], 1.0f,
                         &sig[k]);
    }
    for (int k = 0; k < N_GAMMAS && !bad; k++) {
        bad = radial_run(ctx, PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, PH_RADIAL_DEFAULT_SIGMA,
                         radial_gammas[k], &gam[k]);
    }
    ph_free(ctx);

    /* The decoder's grayscale, as for the other hashes. */
    if (!bad) {
        bad = ph_create(&ctx) != PH_SUCCESS ||
              ph_context_set_load_grayscale(ctx, 1) != PH_SUCCESS ||
              ph_load_from_file(ctx, argv[0]) != PH_SUCCESS ||
              radial_run(ctx, PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, PH_RADIAL_DEFAULT_SIGMA,
                         1.0f, &dec);
        ph_free(ctx);
    }

    float *lines = bad ? NULL : malloc((size_t)def.projections * def.samples * sizeof(float));
    FILE *f = NULL;
    if (lines) {
        for (int i = 0; i < def.projections; i++) {
            radial_line(&def, w, h, i, lines + (size_t)i * def.samples);
        }
        status |= write_pnm(outdir, "blurred.pgm", def.blurred, w, h, 1);
        for (int k = 0; k < N_SIGMAS; k++) {
            char name[32];
            snprintf(name, sizeof(name), "sigma-%g.pgm", (double)radial_sigmas[k]);
            status |= write_pnm(outdir, name, sig[k].blurred, w, h, 1);
        }
        for (int k = 0; k < N_GAMMAS; k++) {
            char name[32];
            snprintf(name, sizeof(name), "gamma-%g.pgm", (double)radial_gammas[k]);
            status |= write_pnm(outdir, name, gam[k].blurred, w, h, 1);
        }
        f = open_out(outdir, "radial.json");
    }
    if (f) {
        json_t j = {f, 0};
        json_int(&j, "width", w);
        json_int(&j, "height", h);
        json_double(&j, "center_x", def.center_x);
        json_double(&j, "center_y", def.center_y);
        json_double(&j, "radius", def.radius);
        json_double(&j, "min_mean_variance", PH_RADIAL_MIN_MEAN_VARIANCE);
        json_double(&j, "min_relative_spread", PH_RADIAL_MIN_RELATIVE_SPREAD);
        json_key(&j, "default");
        radial_json(f, &def, 1);
        json_floats(&j, "lines", lines, def.projections * def.samples);
        json_key(&j, "sigmas");
        for (int k = 0; k < N_SIGMAS; k++) {
            fputs(k ? ", " : "[", f);
            radial_json(f, &sig[k], 1);
        }
        fputs("]", f);
        json_key(&j, "gammas");
        for (int k = 0; k < N_GAMMAS; k++) {
            fputs(k ? ", " : "[", f);
            radial_json(f, &gam[k], 1);
        }
        fputs("]", f);
        json_hexbytes(&j, "digest_load_grayscale", dec.digest, PH_RADIAL_COEFFS);
        json_end(&j);
        status |= fclose(f) != 0;
    }
    free(lines);
    radial_free(&def);
    radial_free(&dec);
    for (int k = 0; k < N_SIGMAS; k++) {
        radial_free(&sig[k]);
    }
    for (int k = 0; k < N_GAMMAS; k++) {
        radial_free(&gam[k]);
    }
    return bad || !f ? 1 : status;
}

/* --8<-- [start:radial-profiles] */
/* site_stages radial-profiles <reference> <image>...: for every image, the default
 * Radial's variance profile and digest, recomputed from its stages and checked against
 * ph_compute_radial_hash(), and the digest compared with the reference's by
 * ph_radial_similarity() (null where the library refuses, an image with no angular
 * structure): one JSON line per image, the reference first. */
static int radial_profiles(int argc, char **argv) {
    ph_digest_t ref = {0};
    for (int i = 0; i < argc; i++) {
        ph_context_t *ctx = NULL;
        if (load(&ctx, argv[i])) {
            return 1;
        }
        radial_stages_t s;
        int bad = radial_run(ctx, PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, PH_RADIAL_DEFAULT_SIGMA,
                             1.0f, &s);
        ph_digest_t d;
        bad = bad || ph_compute_radial_hash(ctx, &d) != PH_SUCCESS;
        ph_free(ctx);
        if (bad) {
            radial_free(&s);
            return 1;
        }
        if (i == 0) {
            ref = d;
        }
        json_t j = {stdout, 0};
        json_string(&j, "file", argv[i]);
        json_key(&j, "radial");
        radial_json(stdout, &s, 1);
        double pcc = 0.0;
        if (ph_radial_similarity(&ref, &d, &pcc) == PH_SUCCESS) {
            json_double(&j, "similarity", pcc);
        } else {
            json_null(&j, "similarity");
        }
        json_end(&j);
        radial_free(&s);
    }
    return 0;
}

/* --8<-- [end:radial-profiles] */

/* --8<-- [start:radial-variants] */
/* The settings `site_stages radial-variants` hashes every image with: the default first,
 * then one setting changed at a time. */
static const struct {
    const char *name;
    int projections, samples;
    float sigma, gamma;
} radial_variants_list[] = {
    {"default", PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, PH_RADIAL_DEFAULT_SIGMA, 1.0f},
    {"sigma_1", PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, 1.0f, 1.0f},
    {"sigma_8", PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, 8.0f, 1.0f},
    {"gamma_0.5", PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, PH_RADIAL_DEFAULT_SIGMA, 0.5f},
    {"gamma_2", PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, PH_RADIAL_DEFAULT_SIGMA, 2.0f},
    {"grid_40x32", 40, 32, PH_RADIAL_DEFAULT_SIGMA, 1.0f},
    {"grid_90x64", 90, 64, PH_RADIAL_DEFAULT_SIGMA, 1.0f},
    {"grid_360x256", 360, 256, PH_RADIAL_DEFAULT_SIGMA, 1.0f},
    {"grid_1440x1024", 1440, 1024, PH_RADIAL_DEFAULT_SIGMA, 1.0f},
    {"grid_4096x4096", PH_RADIAL_MAX_PROJECTIONS, PH_RADIAL_MAX_SAMPLES, PH_RADIAL_DEFAULT_SIGMA,
     1.0f},
};

/* site_stages radial-variants <image>...: every image's Radial digest under each setting
 * of radial_variants_list[], as ph_compute_radial_hash() computes it: one JSON line per
 * image. */
static int radial_variants(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        ph_context_t *ctx = NULL;
        if (load(&ctx, argv[i])) {
            return 1;
        }
        json_t j = {stdout, 0};
        json_string(&j, "file", argv[i]);
        for (size_t k = 0; k < sizeof(radial_variants_list) / sizeof(radial_variants_list[0]);
             k++) {
            ph_digest_t d;
            if (!radial_settings(ctx, radial_variants_list[k].projections,
                                 radial_variants_list[k].samples, radial_variants_list[k].sigma,
                                 radial_variants_list[k].gamma) ||
                ph_compute_radial_hash(ctx, &d) != PH_SUCCESS) {
                ph_free(ctx);
                return fail("ph_compute_radial_hash failed", radial_variants_list[k].name);
            }
            json_hexbytes(&j, radial_variants_list[k].name, d.data, d.size);
        }
        json_end(&j);
        ph_free(ctx);
    }
    return 0;
}

/* --8<-- [end:radial-variants] */

/* --8<-- [start:color_hash] */
/* ColorHash recomputed from its stages and checked against ph_compute_color_hash(): the
 * bin of every pixel, by ph_color_histogram_bin(); the count of each bin; and the digest,
 * every count scaled against the largest, rounded to nearest (all zero for no pixels).
 * `bin_of_pixel`, when given, receives each pixel's bin. */
typedef struct {
    uint64_t counts[PH_COLOR_BINS], max_count;
    uint8_t digest[PH_COLOR_BINS];
} color_stages_t;

static int color_run(ph_context_t *ctx, uint8_t *bin_of_pixel, color_stages_t *s) {
    const size_t n = (size_t)ctx->image.width * (size_t)ctx->image.height;
    const size_t channels = (size_t)ctx->image.channels;
    memset(s, 0, sizeof(*s));
    for (size_t i = 0; i < n; i++) {
        const uint8_t *p = ctx->image.raw_rgb + i * channels;
        int bin = ph_color_histogram_bin(p[0], p[1], p[2]);
        s->counts[bin]++;
        if (bin_of_pixel) {
            bin_of_pixel[i] = (uint8_t)bin;
        }
    }
    for (int b = 0; b < PH_COLOR_BINS; b++) {
        s->max_count = s->counts[b] > s->max_count ? s->counts[b] : s->max_count;
    }
    for (int b = 0; b < PH_COLOR_BINS && s->max_count; b++) {
        s->digest[b] = (uint8_t)((s->counts[b] * 255 + s->max_count / 2) / s->max_count);
    }

    ph_digest_t lib;
    if (ph_compute_color_hash(ctx, &lib) != PH_SUCCESS) {
        return fail("ph_compute_color_hash failed", NULL);
    }
    if (lib.size != PH_COLOR_BINS || memcmp(lib.data, s->digest, PH_COLOR_BINS) != 0) {
        return fail("ColorHash recomputed from the bin counts differs from "
                    "ph_compute_color_hash()",
                    NULL);
    }
    return 0;
}

/* --8<-- [end:color_hash] */

/* site_stages color_hash <image> <outdir> */
static int stages_color_hash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load(&ctx, argv[0])) {
        return 1;
    }
    const int w = ctx->image.width, h = ctx->image.height;
    if (ctx->image.channels < 3) {
        ph_free(ctx);
        return fail("the example image must be in color", argv[0]);
    }
    int status = write_pnm(outdir, "original.ppm", ctx->image.raw_rgb, w, h, 3);
    uint8_t *bins = malloc((size_t)w * (size_t)h);
    color_stages_t s;
    int bad = !bins || color_run(ctx, bins, &s);
    if (!bad) {
        status |= write_pnm(outdir, "bins.pgm", bins, w, h, 1);
    }
    free(bins);
    ph_free(ctx);

    /* The same image through the decoder's grayscale: ColorHash refuses it. */
    ph_error_t gray_err = PH_SUCCESS;
    if (!bad) {
        ph_digest_t d;
        bad = ph_create(&ctx) != PH_SUCCESS ||
              ph_context_set_load_grayscale(ctx, 1) != PH_SUCCESS ||
              ph_load_from_file(ctx, argv[0]) != PH_SUCCESS;
        gray_err = bad ? PH_SUCCESS : ph_compute_color_hash(ctx, &d);
        ph_free(ctx);
        if (!bad && gray_err != PH_ERR_REQUIRES_COLOR) {
            return fail("ph_compute_color_hash() on a grayscale load did not refuse", NULL);
        }
    }

    /* What each bin holds: of the 2^24 8-bit colors, how many fall into it, and their
     * mean, the color a picture of the bin is painted in. */
    static uint64_t volume[PH_COLOR_BINS], sum[PH_COLOR_BINS][3];
    for (int r = 0; r < 256; r++) {
        for (int g = 0; g < 256; g++) {
            for (int b = 0; b < 256; b++) {
                int bin = ph_color_histogram_bin(r, g, b);
                volume[bin]++;
                sum[bin][0] += (uint64_t)r;
                sum[bin][1] += (uint64_t)g;
                sum[bin][2] += (uint64_t)b;
            }
        }
    }

    FILE *f = bad ? NULL : open_out(outdir, "color_hash.json");
    if (f) {
        json_t j = {f, 0};
        json_int(&j, "width", w);
        json_int(&j, "height", h);
        json_int(&j, "bins_rg", PH_COLOR_BINS_RG);
        json_int(&j, "bins_by", PH_COLOR_BINS_BY);
        json_int(&j, "bins_wb", PH_COLOR_BINS_WB);
        json_key(&j, "counts");
        for (int b = 0; b < PH_COLOR_BINS; b++) {
            fprintf(f, "%s%llu", b ? ", " : "[", (unsigned long long)s.counts[b]);
        }
        fputc(']', f);
        json_int(&j, "max_count", (long long)s.max_count);
        json_hexbytes(&j, "digest", s.digest, PH_COLOR_BINS);
        json_key(&j, "volume");
        for (int b = 0; b < PH_COLOR_BINS; b++) {
            fprintf(f, "%s%llu", b ? ", " : "[", (unsigned long long)volume[b]);
        }
        fputc(']', f);
        json_key(&j, "bin_color");
        for (int b = 0; b < PH_COLOR_BINS; b++) {
            fputs(b ? ", [" : "[[", f);
            for (int c = 0; c < 3; c++) {
                fprintf(f, "%s%.1f", c ? ", " : "",
                        volume[b] ? (double)sum[b][c] / (double)volume[b] : 0.0);
            }
            fputc(']', f);
        }
        fputc(']', f);
        json_string(&j, "load_grayscale", ph_get_error_string(gray_err));
        json_end(&j);
        status |= fclose(f) != 0;
    }
    return bad || !f ? 1 : status;
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

/* --8<-- [start:pairs] */
/* site_stages pairs <image>...: every image's digests, then one JSON line per pair of
 * distinct images with their comparison, per algorithm. */
static int pairs(int argc, char **argv) {
    ph_digest_t(*dig)[PH_ALGORITHM_COUNT] = calloc((size_t)argc, sizeof(*dig));
    int (*have)[PH_ALGORITHM_COUNT] = calloc((size_t)argc, sizeof(*have));
    if (!dig || !have) {
        free(dig);
        free(have);
        return fail("out of memory", NULL);
    }
    for (int i = 0; i < argc; i++) {
        ph_context_t *ctx = NULL;
        if (load(&ctx, argv[i])) {
            free(dig);
            free(have);
            return 1;
        }
        for (int a = 0; a < PH_ALGORITHM_COUNT; a++) {
            have[i][a] = ph_compute_digest(ctx, (ph_algorithm_t)a, &dig[i][a]) == PH_SUCCESS;
        }
        ph_free(ctx);
    }
    for (int i = 0; i < argc; i++) {
        for (int k = i + 1; k < argc; k++) {
            json_t j = {stdout, 0};
            json_int(&j, "a", i);
            json_int(&j, "b", k);
            for (int a = 0; a < PH_ALGORITHM_COUNT; a++) {
                const char *name = ph_algorithm_name((ph_algorithm_t)a);
                double value = 0.0;
                if (have[i][a] && have[k][a] && compare(&dig[i][a], &dig[k][a], &value)) {
                    json_double(&j, name, value);
                } else {
                    json_null(&j, name);
                }
            }
            json_end(&j);
        }
    }
    free(dig);
    free(have);
    return 0;
}

/* --8<-- [end:pairs] */

/* --8<-- [start:time] */
/* Seconds on a clock that only moves forward for the purpose: C11's timespec_get(). */
static double seconds(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* One timed case: `run` does the work once on a loaded image and returns nonzero on
 * failure; `arg` selects a variant of it. */
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

/* Radial with the settings of radial_variants_list[arg]. */
static int time_radial(ph_context_t *ctx, const char *path, int arg) {
    (void)path;
    ph_digest_t d;
    int bad = !radial_settings(ctx, radial_variants_list[arg].projections,
                               radial_variants_list[arg].samples, radial_variants_list[arg].sigma,
                               radial_variants_list[arg].gamma) ||
              ph_compute_radial_hash(ctx, &d) != PH_SUCCESS;
    return !radial_settings(ctx, PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, PH_RADIAL_DEFAULT_SIGMA,
                            1.0f) ||
           bad;
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
    {"radial_sigma_1", time_radial, 1},
    {"radial_sigma_8", time_radial, 2},
    {"radial_gamma_2", time_radial, 4},
    {"radial_grid_40x32", time_radial, 5},
    {"radial_grid_90x64", time_radial, 6},
    {"radial_grid_360x256", time_radial, 7},
    {"radial_grid_1440x1024", time_radial, 8},
    {"radial_grid_4096x4096", time_radial, 9},
};

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* site_stages time <image>: every case of time_cases[] on the image, one warm-up run and
 * then at least TIME_MIN_RUNS runs and TIME_MIN_SECONDS, at most TIME_MAX_RUNS; the
 * minimum and the median of the runs, in milliseconds, as one JSON object. */
enum {
    TIME_MIN_RUNS = 5,
    TIME_MAX_RUNS = 300,
};

#define TIME_MIN_SECONDS 1.0

static int time_image(int argc, char **argv) {
    (void)argc;
    ph_context_t *ctx = NULL;
    if (load(&ctx, argv[0])) {
        return 1;
    }
    json_t j = {stdout, 0};
    json_int(&j, "width", ctx->image.width);
    json_int(&j, "height", ctx->image.height);
    json_string(&j, "build_info", ph_get_build_info());
#ifdef __VERSION__
    json_string(&j, "compiler", __VERSION__);
#endif
    json_key(&j, "cases");
    json_t cases = {stdout, 0};
    static double ms[TIME_MAX_RUNS];
    for (size_t c = 0; c < sizeof(time_cases) / sizeof(time_cases[0]); c++) {
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
        json_key(&cases, tc->name);
        json_t o = {stdout, 0};
        json_double(&o, "min_ms", ms[0]);
        json_double(&o, "median_ms", ms[runs / 2]);
        json_int(&o, "runs", runs);
        fputs("}", stdout);
    }
    fputs("}", stdout);
    json_end(&j);
    ph_free(ctx);
    return 0;
}

/* --8<-- [end:time] */

/* site_stages corpus <outdir> */
static int corpus(int argc, char **argv) {
    (void)argc;
    int status = 0;
    for (int i = 0; i < NUM_BASE; i++) {
        char name[16];
        snprintf(name, sizeof(name), "%02d.ppm", i);
        image_t im = make_base(i);
        status |= write_pnm(argv[0], name, im.px, im.w, im.h, 3);
        image_free(&im);
    }
    return status;
}

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
    {"dhash", "<image> <outdir>", 2, 0, stages_dhash},
    {"phash", "<image> <outdir>", 2, 0, stages_phash},
    {"whash", "<image> <outdir>", 2, 0, stages_whash},
    {"whash-modes", "<image>...", 1, 1, whash_modes},
    {"mhash", "<image> <outdir>", 2, 0, stages_mhash},
    {"mhash-direct", "<image>...", 1, 1, mhash_direct},
    {"bmh", "<image> <outdir>", 2, 0, stages_bmh},
    {"bmh-variants", "<image>...", 1, 1, bmh_variants},
    {"radial", "<image> <outdir>", 2, 0, stages_radial},
    {"radial-profiles", "<reference> <image>...", 1, 1, radial_profiles},
    {"radial-variants", "<image>...", 1, 1, radial_variants},
    {"color_hash", "<image> <outdir>", 2, 0, stages_color_hash},
    {"measure", "<reference> <variant>...", 2, 1, measure},
    {"pairs", "<image> <image>...", 2, 1, pairs},
    {"corpus", "<outdir>", 1, 0, corpus},
    {"time", "<image>", 1, 0, time_image},
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
