/* site_stages: BMH. */
#include "stages.h"

#include <string.h>

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
    char what[32];
    snprintf(what, sizeof(what), "BMH (block_size %d)", size);
    return check_digest(what, s->digest, bytes, &lib);
}

/* --8<-- [end:bmh] */

/* The block sizes `site_stages bmh` draws and `bmh-variants` measures, the default among
 * them. */
static const int bmh_sizes[] = {4, 8, 16, 32};

/* site_stages bmh <image> <outdir>
 *     original.ppm, gray.pgm, and bmh.json: for each of bmh_sizes[], the grid of block
 *     means, its median and mean, the digest and the digest the same grid gives
 *     thresholded at its mean, every digest checked against ph_compute_bmh() with that
 *     block size; and the digest of the image loaded with
 *     ph_context_set_load_grayscale(), checked the same way. */
int mode_bmh(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;
    bmh_stages_t runs[COUNT(bmh_sizes)], dec;
    int bad = !write_gray_stages(ctx, outdir, &status);
    for (size_t k = 0; k < COUNT(bmh_sizes) && !bad; k++) {
        bad = bmh_run(ctx, bmh_sizes[k], &runs[k]);
    }
    ph_free(ctx);
    if (!bad) {
        bad = load_decoder_gray(&ctx, argv[0]) || bmh_run(ctx, PH_BLOCK_SIZE, &dec);
        ph_free(ctx);
    }

    FILE *f = bad ? NULL : open_out(outdir, "bmh.json");
    if (!f) {
        return 1;
    }
    json_t j = json_begin(f);
    json_int(&j, "width", w);
    json_int(&j, "height", h);
    json_int(&j, "default", PH_BLOCK_SIZE);
    json_t a = json_array(&j, "sizes");
    for (size_t k = 0; k < COUNT(bmh_sizes); k++) {
        const bmh_stages_t *s = &runs[k];
        const int n = s->size * s->size;
        json_t o = json_object(&a, NULL);
        json_int(&o, "size", s->size);
        json_u8s(&o, "grid", s->grid, n);
        json_int(&o, "median", s->median);
        json_double(&o, "mean", s->mean);
        json_hexbytes(&o, "digest", s->digest, (n + 7) / 8);
        json_hexbytes(&o, "digest_mean", s->digest_mean, (n + 7) / 8);
        json_close_object(&o);
    }
    json_close_array(&a);
    json_hexbytes(&j, "digest_load_grayscale", dec.digest, PH_BLOCK_SIZE * PH_BLOCK_SIZE / 8);
    json_end(&j);
    return status | (fclose(f) != 0);
}

/* --8<-- [start:bmh-variants] */
/* One image of `site_stages bmh-variants`: its BMH digest at each block size of
 * bmh_sizes[], as ph_compute_bmh() computes it (bmh_run() checks), and the same grid
 * thresholded at its mean. */
static int bmh_variants_row(ph_context_t *ctx, json_t *row, const char *path) {
    (void)path;
    for (size_t k = 0; k < COUNT(bmh_sizes); k++) {
        bmh_stages_t s;
        if (bmh_run(ctx, bmh_sizes[k], &s)) {
            return 1;
        }
        char key[32];
        const int bytes = (s.size * s.size + 7) / 8;
        snprintf(key, sizeof(key), "median_%d", s.size);
        json_hexbytes(row, key, s.digest, bytes);
        snprintf(key, sizeof(key), "mean_%d", s.size);
        json_hexbytes(row, key, s.digest_mean, bytes);
    }
    return 0;
}

/* site_stages bmh-variants <image>...: one JSON line per image, as above. */
int mode_bmh_variants(int argc, char **argv) {
    return for_each_image(argc, argv, bmh_variants_row);
}

/* --8<-- [end:bmh-variants] */
