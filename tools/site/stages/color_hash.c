/* site_stages: ColorHash. */
#include "stages.h"

#include <stdlib.h>
#include <string.h>

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
    return check_digest("ColorHash", s->digest, PH_COLOR_BINS, &lib);
}

/* --8<-- [end:color_hash] */

int color_refuses_gray(const char *path, ph_error_t (*compute)(ph_context_t *, ph_digest_t *),
                       const char *name, ph_error_t *err) {
    ph_context_t *ctx = NULL;
    if (load_decoder_gray(&ctx, path)) {
        return 1;
    }
    ph_digest_t d;
    *err = compute(ctx, &d);
    ph_free(ctx);
    return *err == PH_ERR_REQUIRES_COLOR ? 0 : fail(name, "did not refuse a grayscale load");
}

/* What each bin holds: of the 2^24 8-bit colors, how many fall into it, and their mean,
 * the color a picture of the bin is painted in. */
static void bin_volumes(uint64_t volume[PH_COLOR_BINS], double color[PH_COLOR_BINS][3]) {
    static uint64_t sum[PH_COLOR_BINS][3];
    memset(volume, 0, PH_COLOR_BINS * sizeof(volume[0]));
    memset(sum, 0, sizeof(sum));
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
    for (int bin = 0; bin < PH_COLOR_BINS; bin++) {
        for (int c = 0; c < 3; c++) {
            color[bin][c] = volume[bin] ? (double)sum[bin][c] / (double)volume[bin] : 0.0;
        }
    }
}

/* site_stages color_hash <image> <outdir>
 *     original.ppm, bins.pgm (each pixel's bin, 0 to 107), and color_hash.json: the count
 *     of every bin, the largest, and the digest, checked against
 *     ph_compute_color_hash(); for every bin, how many of the 2^24 8-bit colors fall
 *     into it and their mean; and the error ph_compute_color_hash() returns for the
 *     image loaded with ph_context_set_load_grayscale(), which must be
 *     PH_ERR_REQUIRES_COLOR. */
int mode_color_hash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[0])) {
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
    ph_error_t gray_err = PH_SUCCESS;
    bad = bad ||
          color_refuses_gray(argv[0], ph_compute_color_hash, "ph_compute_color_hash()", &gray_err);

    static uint64_t volume[PH_COLOR_BINS];
    static double color[PH_COLOR_BINS][3];
    bin_volumes(volume, color);

    FILE *f = bad ? NULL : open_out(outdir, "color_hash.json");
    if (!f) {
        return 1;
    }
    json_t j = json_begin(f);
    json_int(&j, "width", w);
    json_int(&j, "height", h);
    json_int(&j, "bins_rg", PH_COLOR_BINS_RG);
    json_int(&j, "bins_by", PH_COLOR_BINS_BY);
    json_int(&j, "bins_wb", PH_COLOR_BINS_WB);
    json_u64s(&j, "counts", s.counts, PH_COLOR_BINS);
    json_int(&j, "max_count", (long long)s.max_count);
    json_hexbytes(&j, "digest", s.digest, PH_COLOR_BINS);
    json_u64s(&j, "volume", volume, PH_COLOR_BINS);
    json_t a = json_array(&j, "bin_color");
    for (int b = 0; b < PH_COLOR_BINS; b++) {
        json_doubles_fixed(&a, NULL, color[b], 3, 1);
    }
    json_close_array(&a);
    json_string(&j, "load_grayscale", ph_get_error_string(gray_err));
    json_end(&j);
    return status | (fclose(f) != 0);
}
