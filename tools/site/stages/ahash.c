/* site_stages: aHash. */
#include "stages.h"

/* site_stages ahash <image> <outdir>
 *     original.ppm, gray.pgm, and ahash.json: the 8x8 grid, its mean, the 64 bits and
 *     the hash, recomputed from the grid and checked against ph_compute_ahash(). */
int mode_ahash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;

    enum {
        N = 8,
    };

    uint8_t grid[N * N];
    if (!write_gray_stages(ctx, outdir, &status) || !ph_area_downscale(ctx, N, N, grid)) {
        ph_free(ctx);
        return fail("cannot reduce the image to 8x8", NULL);
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
    int bad = ph_compute_ahash(ctx, &lib) != PH_SUCCESS || check_hash64("aHash", bits, lib);
    ph_free(ctx);
    FILE *f = bad ? NULL : open_out(outdir, "ahash.json");
    if (!f) {
        return 1;
    }
    json_t j = json_begin(f);
    json_int(&j, "width", w);
    json_int(&j, "height", h);
    json_int(&j, "grid_size", N);
    json_u8s(&j, "grid", grid, N * N);
    json_double(&j, "mean", (double)sum / (N * N));
    json_bits_msb(&j, "bits", bits, N * N);
    json_hex64(&j, "hash", bits);
    json_end(&j);
    return status | (fclose(f) != 0);
}
