/* site_stages: pHash. */
#include "stages.h"

#include <string.h>

enum {
    N = 32,
    MIN_R = PH_DCT_MIN_REDUCTION_SIZE,
    MAX_R = PH_DCT_MAX_REDUCTION_SIZE,
    SIZES = MAX_R - MIN_R + 1,
};

/* pHash's block of r x r DCT coefficients of the 32x32 grid, by the library's own
 * ph_dct2_partial(); its threshold, the median of the AC terms raised by the margin; and
 * its hash recomputed from them and checked against ph_compute_phash() with that block
 * size. Returns 0 on a match. */
static int phash_block(ph_context_t *ctx, const uint8_t *grid, int r, float block[64],
                       float *threshold, uint64_t *bits) {
    if (ph_dct2_partial(ph_get_dct_matrix_32(), grid, N, r, block) != PH_SUCCESS) {
        return fail("ph_dct2_partial failed", NULL);
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
    uint64_t lib = 0;
    if (ph_context_set_phash_params(ctx, N, r) != PH_SUCCESS ||
        ph_compute_phash(ctx, &lib) != PH_SUCCESS) {
        return fail("ph_compute_phash failed", NULL);
    }
    char what[32];
    snprintf(what, sizeof(what), "pHash (block %d)", r);
    return check_hash64(what, *bits, lib);
}

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
 *     with that size; and the hash of the image loaded with
 *     ph_context_set_load_grayscale(), checked the same way. */
int mode_phash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;
    uint8_t grid[N * N];
    if (!write_gray_stages(ctx, outdir, &status) || !ph_area_downscale(ctx, N, N, grid)) {
        ph_free(ctx);
        return fail("cannot reduce the image to 32x32", NULL);
    }

    /* Every block size the API accepts, the default, 8, last. */
    float blocks[SIZES][64], thresholds[SIZES];
    uint64_t hashes[SIZES];
    int bad = 0;
    for (int r = MIN_R; r <= MAX_R && !bad; r++) {
        bad = phash_block(ctx, grid, r, blocks[r - MIN_R], &thresholds[r - MIN_R],
                          &hashes[r - MIN_R]);
    }
    ph_free(ctx);
    const int def = MAX_R - MIN_R;

    uint8_t dec_grid[N * N];
    float dec_block[64], dec_threshold;
    uint64_t dec_hash = 0;
    if (!bad) {
        bad = load_decoder_gray(&ctx, argv[0]) || !ph_area_downscale(ctx, N, N, dec_grid) ||
              phash_block(ctx, dec_grid, MAX_R, dec_block, &dec_threshold, &dec_hash);
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
    json_end(&j);
    return status | (fclose(f) != 0);
}
