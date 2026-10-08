/* site_stages: aHash. */
#include "stages.h"

enum {
    N = 8,
};

/* --8<-- [start:grids] */
/* The 8x8 grids `site_stages ahash` draws side by side and `ahash-variants` measures:
 * the library's exact area average first, then two reductions it does not use. */
enum {
    GRID_AREA,     /* ph_area_downscale(): every pixel, weighted by its overlap */
    GRID_MITCHELL, /* ph_resize_mitchell(), the filter dHash reduces with */
    GRID_NEAREST,  /* one pixel per cell, the one nearest its center */
    GRIDS,
};

static const char *const grid_names[GRIDS] = {"area", "mitchell", "nearest"};

/* The 8x8 grid of a loaded image by one of the reductions above. */
static int ahash_grid(ph_context_t *ctx, int which, uint8_t grid[N * N]) {
    if (which == GRID_AREA) {
        return ph_area_downscale(ctx, N, N, grid);
    }
    const uint8_t *gray = ph_get_gray(ctx);
    const int w = ctx->image.width, h = ctx->image.height;
    if (!gray) {
        return 0;
    }
    if (which == GRID_MITCHELL) {
        return ph_resize_mitchell(gray, w, h, grid, N, N);
    }
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            grid[r * N + c] = gray[(size_t)((2 * r + 1) * h / (2 * N)) * (size_t)w +
                                   (size_t)((2 * c + 1) * w / (2 * N))];
        }
    }
    return 1;
}

/* --8<-- [end:grids] */

/* --8<-- [start:ahash] */
/* The bits of an 8x8 grid: set where the cell is at or above the mean of the 64, compared
 * exactly as cell * 64 >= sum. With `floor_mean`, against the mean rounded down to an
 * integer instead, which the library does not do. */
static uint64_t ahash_bits(const uint8_t grid[N * N], int floor_mean) {
    uint64_t sum = 0;
    for (int i = 0; i < N * N; i++) {
        sum += grid[i];
    }
    uint64_t bits = 0;
    for (int i = 0; i < N * N; i++) {
        int set = floor_mean ? grid[i] >= sum / (N * N) : (uint64_t)grid[i] * (N * N) >= sum;
        if (set) {
            bits |= 1ULL << (63 - i);
        }
    }
    return bits;
}

/* aHash of a loaded image from its area grid, checked against ph_compute_ahash().
 * Returns 0 on a match. */
static int ahash_run(ph_context_t *ctx, uint8_t grid[N * N], uint64_t *bits) {
    if (!ahash_grid(ctx, GRID_AREA, grid)) {
        return fail("cannot reduce the image to 8x8", NULL);
    }
    *bits = ahash_bits(grid, 0);
    uint64_t lib = 0;
    if (ph_compute_ahash(ctx, &lib) != PH_SUCCESS) {
        return fail("ph_compute_ahash failed", NULL);
    }
    return check_hash64("aHash", *bits, lib);
}

/* --8<-- [end:ahash] */

/* site_stages ahash <image> <outdir>
 *     original.ppm, gray.pgm, and ahash.json: the 8x8 grid, its mean, the 64 bits and the
 *     hash, recomputed from the grid and checked against ph_compute_ahash(); the grids and
 *     bits of the other two reductions; and the hash of the same image loaded with
 *     ph_context_set_load_grayscale(), checked the same way. */
int mode_ahash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;
    uint8_t grids[GRIDS][N * N], dec_grid[N * N];
    uint64_t bits = 0, dec_bits = 0;
    int bad = !write_gray_stages(ctx, outdir, &status) || ahash_run(ctx, grids[GRID_AREA], &bits);
    for (int k = GRID_AREA + 1; k < GRIDS && !bad; k++) {
        bad = !ahash_grid(ctx, k, grids[k]) && fail("cannot reduce the image to 8x8", NULL);
    }
    ph_free(ctx);
    if (!bad) {
        bad = load_decoder_gray(&ctx, argv[0]) || ahash_run(ctx, dec_grid, &dec_bits);
        ph_free(ctx);
    }
    FILE *f = bad ? NULL : open_out(outdir, "ahash.json");
    if (!f) {
        return 1;
    }
    uint64_t sum = 0;
    for (int i = 0; i < N * N; i++) {
        sum += grids[GRID_AREA][i];
    }
    json_t j = json_begin(f);
    json_int(&j, "width", w);
    json_int(&j, "height", h);
    json_int(&j, "grid_size", N);
    json_u8s(&j, "grid", grids[GRID_AREA], N * N);
    json_double(&j, "mean", (double)sum / (N * N));
    json_bits_msb(&j, "bits", bits, N * N);
    json_hex64(&j, "hash", bits);
    json_t o = json_object(&j, "reductions");
    for (int k = 0; k < GRIDS; k++) {
        json_t r = json_object(&o, grid_names[k]);
        json_u8s(&r, "grid", grids[k], N * N);
        json_bits_msb(&r, "bits", ahash_bits(grids[k], 0), N * N);
        json_close_object(&r);
    }
    json_close_object(&o);
    json_hex64(&j, "hash_load_grayscale", dec_bits);
    json_end(&j);
    return status | (fclose(f) != 0);
}

/* --8<-- [start:ahash-variants] */
/* One image of `site_stages ahash-variants`: its aHash as ph_compute_ahash() computes it
 * (ahash_run() checks), the hash of the same grid against its mean rounded down, and the
 * hashes of the grids the other two reductions give. */
static int ahash_variants_row(ph_context_t *ctx, json_t *row, const char *path) {
    (void)path;
    uint8_t grid[N * N];
    uint64_t bits = 0;
    if (ahash_run(ctx, grid, &bits)) {
        return 1;
    }
    json_hex64(row, "area", bits);
    json_hex64(row, "floor_mean", ahash_bits(grid, 1));
    for (int k = GRID_AREA + 1; k < GRIDS; k++) {
        if (!ahash_grid(ctx, k, grid)) {
            return fail("cannot reduce the image to 8x8", NULL);
        }
        json_hex64(row, grid_names[k], ahash_bits(grid, 0));
    }
    return 0;
}

/* site_stages ahash-variants <image>...: one JSON line per image, as above. */
int mode_ahash_variants(int argc, char **argv) {
    return for_each_image(argc, argv, ahash_variants_row);
}

/* --8<-- [end:ahash-variants] */
