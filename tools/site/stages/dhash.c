/* site_stages: dHash. */
#include "stages.h"

enum {
    W = 9,
    H = 8,
};

/* --8<-- [start:dhash] */
/* The bits of a 9x8 grid: set where a cell is darker than its right-hand neighbor. */
static uint64_t dhash_bits(const uint8_t grid[W * H]) {
    uint64_t bits = 0;
    for (int row = 0; row < H; row++) {
        for (int col = 0; col < W - 1; col++) {
            if (grid[row * W + col] < grid[row * W + col + 1]) {
                bits |= 1ULL << (63 - (row * (W - 1) + col));
            }
        }
    }
    return bits;
}

/* dHash of a loaded image from its own 9x8 Mitchell grid: the bits recomputed from
 * `grid` and checked against ph_compute_dhash(). Returns 0 on a match. */
static int dhash_run(ph_context_t *ctx, uint8_t grid[W * H], uint64_t *bits) {
    const uint8_t *gray = ph_get_gray(ctx);
    if (!gray || !ph_resize_mitchell(gray, ctx->image.width, ctx->image.height, grid, W, H)) {
        return fail("cannot reduce the image to 9x8", NULL);
    }
    *bits = dhash_bits(grid);
    uint64_t lib = 0;
    if (ph_compute_dhash(ctx, &lib) != PH_SUCCESS) {
        return fail("ph_compute_dhash failed", NULL);
    }
    return check_hash64("dHash", *bits, lib);
}

/* The same image's 9x8 grid by the exact area average aHash reduces with, which dHash
 * does not use. */
static int dhash_area_grid(ph_context_t *ctx, uint8_t grid[W * H]) {
    return ph_area_downscale(ctx, W, H, grid) ? 0 : fail("cannot reduce the image to 9x8", NULL);
}

/* --8<-- [end:dhash] */

/* site_stages dhash <image> <outdir>
 *     original.ppm, gray.pgm, and dhash.json: the 9x8 grid, the 64 bits and the hash,
 *     checked against ph_compute_dhash(); and the grid and hash of the same image loaded
 *     with ph_context_set_load_grayscale(), where the decoder converts to grayscale (a
 *     JPEG decoder with its own coefficients), checked the same way; and the grid an exact
 *     area average gives, with its bits. */
int mode_dhash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;
    uint8_t grid[W * H], dec_grid[W * H], area_grid[W * H];
    uint64_t bits = 0, dec_bits = 0;
    int bad = !write_gray_stages(ctx, outdir, &status) || dhash_run(ctx, grid, &bits) ||
              dhash_area_grid(ctx, area_grid);
    ph_free(ctx);
    if (!bad) {
        bad = load_decoder_gray(&ctx, argv[0]) || dhash_run(ctx, dec_grid, &dec_bits);
        ph_free(ctx);
    }
    FILE *f = bad ? NULL : open_out(outdir, "dhash.json");
    if (!f) {
        return 1;
    }
    json_t j = json_begin(f);
    json_int(&j, "width", w);
    json_int(&j, "height", h);
    json_int(&j, "grid_width", W);
    json_int(&j, "grid_height", H);
    json_u8s(&j, "grid", grid, W * H);
    json_bits_msb(&j, "bits", bits, 64);
    json_hex64(&j, "hash", bits);
    json_u8s(&j, "grid_load_grayscale", dec_grid, W * H);
    json_hex64(&j, "hash_load_grayscale", dec_bits);
    json_u8s(&j, "grid_area", area_grid, W * H);
    json_bits_msb(&j, "bits_area", dhash_bits(area_grid), 64);
    json_end(&j);
    return status | (fclose(f) != 0);
}

/* --8<-- [start:dhash-variants] */
/* One image of `site_stages dhash-variants`: its dHash as ph_compute_dhash() computes it
 * (dhash_run() checks), and the hash the same comparisons give on an area-average grid. */
static int dhash_variants_row(ph_context_t *ctx, json_t *row, const char *path) {
    (void)path;
    uint8_t grid[W * H];
    uint64_t bits = 0;
    if (dhash_run(ctx, grid, &bits) || dhash_area_grid(ctx, grid)) {
        return 1;
    }
    json_hex64(row, "mitchell", bits);
    json_hex64(row, "area", dhash_bits(grid));
    return 0;
}

/* site_stages dhash-variants <image>...: one JSON line per image, as above. */
int mode_dhash_variants(int argc, char **argv) {
    return for_each_image(argc, argv, dhash_variants_row);
}

/* --8<-- [end:dhash-variants] */
