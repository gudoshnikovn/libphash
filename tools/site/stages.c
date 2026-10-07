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
 *       thresholded at its median and packed the same way, which the LL band amounts to.
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
        json_end(&j);
        ph_free(ctx);
    }
    return 0;
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
    {"measure", "<reference> <variant>...", 2, 1, measure},
    {"pairs", "<image> <image>...", 2, 1, pairs},
    {"corpus", "<outdir>", 1, 0, corpus},
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
