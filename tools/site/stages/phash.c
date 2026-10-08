/* site_stages: pHash. */
#include "stages.h"

#include <math.h>
#include <string.h>

enum {
    N = 32,
    MIN_R = PH_DCT_MIN_REDUCTION_SIZE,
    MAX_R = PH_DCT_MAX_REDUCTION_SIZE,
    SIZES = MAX_R - MIN_R + 1,
};

/* --8<-- [start:phash] */
/* The DCT matrix of side n, by the library's formula (Zauner's definition 3.3, as
 * src/hashes/phash.c builds it for a dct_size other than 32). */
static void dct_matrix(float *matrix, int n) {
    float c = (float)sqrt(1.0 / n);
    for (int j = 0; j < n; j++) {
        matrix[j] = c;
    }
    c = (float)sqrt(2.0 / n);
    for (int i = 1; i < n; i++) {
        for (int j = 0; j < n; j++) {
            matrix[i * n + j] = (float)((double)c * cos(M_PI * i * (j + 0.5) / n));
        }
    }
}

/* pHash's block of r x r DCT coefficients of the image's d x d area grid, by the
 * library's own ph_dct2_partial(); its threshold, the median of the AC terms raised by
 * the margin; and the hash recomputed from them, checked against ph_compute_phash() with
 * the same parameters when the library accepts them (r from 4 to 8). Returns 0 on a
 * match. */
static int phash_block(ph_context_t *ctx, int d, int r, uint8_t grid[N * N], float block[64],
                       float *threshold, uint64_t *bits) {
    static float matrix[N * N];
    const float *mat = ph_get_dct_matrix_32();
    if (d != N) {
        dct_matrix(matrix, d);
        mat = matrix;
    }
    if (!ph_area_downscale(ctx, d, d, grid) ||
        ph_dct2_partial(mat, grid, d, r, block) != PH_SUCCESS) {
        return fail("cannot compute the DCT block", NULL);
    }
    float ac[63];
    const int m = r * r - 1;
    memcpy(ac, block + 1, (size_t)m * sizeof(float));
    sort_floats(ac, m); /* for the median and the range */
    float median = m % 2 ? ac[m / 2] : (ac[m / 2 - 1] + ac[m / 2]) * 0.5f;
    *threshold = median + PH_PHASH_MEDIAN_MARGIN * (ac[m - 1] - ac[0]);
    *bits = 0;
    for (int i = 0; i < r * r; i++) {
        if (block[i] > *threshold) {
            *bits |= 1ULL << i;
        }
    }
    if (r < MIN_R) {
        return 0; /* below the range ph_context_set_phash_params() accepts */
    }
    uint64_t lib = 0;
    int bad = ph_context_set_phash_params(ctx, d, r) != PH_SUCCESS ||
              ph_compute_phash(ctx, &lib) != PH_SUCCESS;
    if (ph_context_set_phash_params(ctx, N, MAX_R) != PH_SUCCESS || bad) {
        return fail("ph_compute_phash failed", NULL);
    }
    char what[48];
    snprintf(what, sizeof(what), "pHash (dct_size %d, block %d)", d, r);
    return check_hash64(what, *bits, lib);
}

/* --8<-- [end:phash] */

/* The dct_size values `site_stages phash` draws and `phash-variants` measures, with the
 * default block; the default last. */
static const int dct_sizes[] = {8, 16, 24, 32};

/* The whole 32x32 transform, for the map of coefficients: the same two passes as
 * ph_dct2_partial() in the same order, over every row and column, so that its top-left
 * corner is the library's block bit for bit (the caller checks). */
static void full_dct(const uint8_t *grid, float dct[N * N]) {
    const float *mat = ph_get_dct_matrix_32();
    static float temp[N * N];
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
}

/* site_stages phash <image> <outdir>
 *     original.ppm, gray.pgm, and phash.json: the 32x32 grid, its whole DCT, the first
 *     eight DCT basis vectors, the 8x8 block, the threshold, the bits in coefficient
 *     order and the hash, checked against ph_compute_phash(); the block, threshold and
 *     hash for every block size from 4 to 8, each checked against ph_compute_phash()
 *     with that size; the grid and hash for every dct_size of dct_sizes[], checked the
 *     same way; and the hash of the image loaded with ph_context_set_load_grayscale(). */
int mode_phash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;
    uint8_t grid[N * N], scratch[N * N];
    if (!write_gray_stages(ctx, outdir, &status) || !ph_area_downscale(ctx, N, N, grid)) {
        ph_free(ctx);
        return fail("cannot reduce the image to 32x32", NULL);
    }

    /* Every block size the API accepts, the default, 8, last. */
    float blocks[SIZES][64], thresholds[SIZES];
    uint64_t hashes[SIZES];
    int bad = 0;
    for (int r = MIN_R; r <= MAX_R && !bad; r++) {
        bad = phash_block(ctx, N, r, scratch, blocks[r - MIN_R], &thresholds[r - MIN_R],
                          &hashes[r - MIN_R]);
    }

    /* Every dct_size of dct_sizes[] with the default block: its grid and its bits. */
    enum {
        DCTS = COUNT(dct_sizes),
    };

    static uint8_t dct_grids[DCTS][N * N];
    float dct_block[64], dct_threshold;
    uint64_t dct_hashes[DCTS];
    for (size_t k = 0; k < DCTS && !bad; k++) {
        bad = phash_block(ctx, dct_sizes[k], MAX_R, dct_grids[k], dct_block, &dct_threshold,
                          &dct_hashes[k]);
    }
    ph_free(ctx);
    const int def = MAX_R - MIN_R;

    uint8_t dec_grid[N * N];
    float dec_block[64], dec_threshold;
    uint64_t dec_hash = 0;
    if (!bad) {
        bad = load_decoder_gray(&ctx, argv[0]) ||
              phash_block(ctx, N, MAX_R, dec_grid, dec_block, &dec_threshold, &dec_hash);
        ph_free(ctx);
    }

    static float dct[N * N];
    full_dct(grid, dct);
    for (int i = 0; i < MAX_R * MAX_R && !bad; i++) {
        if (dct[(i / MAX_R) * N + i % MAX_R] != blocks[def][i]) {
            bad = fail("the full DCT differs from ph_dct2_partial()", NULL);
        }
    }

    FILE *f = bad ? NULL : open_out(outdir, "phash.json");
    if (!f) {
        return 1;
    }
    json_t j = json_begin(f);
    json_int(&j, "width", w);
    json_int(&j, "height", h);
    json_int(&j, "dct_size", N);
    json_int(&j, "block_size", MAX_R);
    json_u8s(&j, "grid", grid, N * N);
    json_floats(&j, "dct", dct, N * N);
    json_floats(&j, "matrix", ph_get_dct_matrix_32(), MAX_R * N); /* the first 8 basis vectors */
    json_floats(&j, "block", blocks[def], MAX_R * MAX_R);
    json_double(&j, "margin", PH_PHASH_MEDIAN_MARGIN);
    json_double(&j, "threshold", thresholds[def]);
    json_bits_lsb(&j, "bits", hashes[def], MAX_R * MAX_R);
    json_hex64(&j, "hash", hashes[def]);
    json_hex64(&j, "hash_load_grayscale", dec_hash);
    json_t reductions = json_array(&j, "reductions");
    for (int r = MIN_R; r <= MAX_R; r++) {
        json_t o = json_object(&reductions, NULL);
        json_int(&o, "size", r);
        json_floats(&o, "block", blocks[r - MIN_R], r * r);
        json_double(&o, "threshold", thresholds[r - MIN_R]);
        json_bits_lsb(&o, "bits", hashes[r - MIN_R], r * r);
        json_hex64(&o, "hash", hashes[r - MIN_R]);
        json_close_object(&o);
    }
    json_close_array(&reductions);
    json_t sizes = json_array(&j, "dct_sizes");
    for (size_t k = 0; k < DCTS; k++) {
        json_t o = json_object(&sizes, NULL);
        json_int(&o, "size", dct_sizes[k]);
        json_u8s(&o, "grid", dct_grids[k], dct_sizes[k] * dct_sizes[k]);
        json_bits_lsb(&o, "bits", dct_hashes[k], MAX_R * MAX_R);
        json_hex64(&o, "hash", dct_hashes[k]);
        json_close_object(&o);
    }
    json_close_array(&sizes);
    json_end(&j);
    return status | (fclose(f) != 0);
}

/* --8<-- [start:phash-variants] */
/* One image of `site_stages phash-variants`: its pHash at every block size from 2 to 8
 * ("r2" … "r8", dct_size 32; 2 and 3 are below what the library accepts, and computed
 * the same way) and at every dct_size of dct_sizes[] with the default block ("d8" …
 * "d32"), each that the library accepts checked against ph_compute_phash(). */
static int phash_variants_row(ph_context_t *ctx, json_t *row, const char *path) {
    (void)path;
    uint8_t grid[N * N];
    float block[64], threshold;
    uint64_t bits;
    char key[8];
    for (int r = 2; r <= MAX_R; r++) {
        if (phash_block(ctx, N, r, grid, block, &threshold, &bits)) {
            return 1;
        }
        snprintf(key, sizeof(key), "r%d", r);
        json_hex64(row, key, bits);
    }
    for (size_t k = 0; k < COUNT(dct_sizes); k++) {
        if (phash_block(ctx, dct_sizes[k], MAX_R, grid, block, &threshold, &bits)) {
            return 1;
        }
        snprintf(key, sizeof(key), "d%d", dct_sizes[k]);
        json_hex64(row, key, bits);
    }
    return 0;
}

/* site_stages phash-variants <image>...: one JSON line per image, as above. */
int mode_phash_variants(int argc, char **argv) {
    return for_each_image(argc, argv, phash_variants_row);
}

/* --8<-- [end:phash-variants] */
