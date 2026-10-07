/* site_stages: ColorMoments. */
#include "stages.h"

#include <math.h>
#include <string.h>

/* --8<-- [start:color_moments] */
/* ColorMoments recomputed from each channel's histogram and checked against
 * ph_compute_color_moments_hash(): the count of every level of R, G and B; from it the
 * mean, the standard deviation and the cube root of the third central moment; each moment
 * rounded to 1/128 of a level and written as a signed 16-bit big-endian number. */
typedef struct {
    uint64_t hist[PH_COLOR_CHANNELS][256];
    double moment[PH_COLOR_CHANNELS][PH_COLOR_MOMENTS]; /* mean, std dev, skew */
    uint8_t digest[PH_COLOR_MOMENTS_DIGEST_BYTES];
} moments_stages_t;

static int moments_run(ph_context_t *ctx, moments_stages_t *s) {
    const size_t n = (size_t)ctx->image.width * (size_t)ctx->image.height;
    const size_t channels = (size_t)ctx->image.channels;
    memset(s, 0, sizeof(*s));
    for (size_t i = 0; i < n; i++) {
        for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
            s->hist[c][ctx->image.raw_rgb[i * channels + (size_t)c]]++;
        }
    }
    for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
        uint64_t sum = 0;
        for (int v = 0; v < 256; v++) {
            sum += s->hist[c][v] * (uint64_t)v;
        }
        const double mean = (double)sum / (double)n;
        double second = 0.0, third = 0.0;
        for (int v = 0; v < 256; v++) {
            const double d = v - mean, k = (double)s->hist[c][v];
            second += k * d * d;
            third += k * d * d * d;
        }
        const double m[PH_COLOR_MOMENTS] = {mean, sqrt(second / (double)n),
                                            cbrt(third / (double)n)};
        for (int k = 0; k < PH_COLOR_MOMENTS; k++) {
            s->moment[c][k] = m[k];
            const uint16_t bits = (uint16_t)(int16_t)round(m[k] * PH_COLOR_MOMENT_SCALE);
            const int at = (c * PH_COLOR_MOMENTS + k) * PH_COLOR_MOMENT_BYTES;
            s->digest[at] = (uint8_t)(bits >> 8);
            s->digest[at + 1] = (uint8_t)(bits & 0xFF);
        }
    }

    ph_digest_t lib;
    if (ph_compute_color_moments_hash(ctx, &lib) != PH_SUCCESS) {
        return fail("ph_compute_color_moments_hash failed", NULL);
    }
    return check_digest("ColorMoments", s->digest, PH_COLOR_MOMENTS_DIGEST_BYTES, &lib);
}

/* --8<-- [end:color_moments] */

/* site_stages color_moments <image> <outdir>
 *     original.ppm and color_moments.json: the count of every level of each channel, the
 *     three moments of each channel recomputed from those counts, and the digest they
 *     encode, checked against ph_compute_color_moments_hash(); and the error
 *     ph_compute_color_moments_hash() returns for the image loaded with
 *     ph_context_set_load_grayscale(), which must be PH_ERR_REQUIRES_COLOR. */
int mode_color_moments(int argc, char **argv) {
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
    static moments_stages_t s;
    int bad = moments_run(ctx, &s);
    ph_free(ctx);
    ph_error_t gray_err = PH_SUCCESS;
    bad = bad || color_refuses_gray(argv[0], ph_compute_color_moments_hash,
                                    "ph_compute_color_moments_hash()", &gray_err);

    FILE *f = bad ? NULL : open_out(outdir, "color_moments.json");
    if (!f) {
        return 1;
    }
    json_t j = json_begin(f);
    json_int(&j, "width", w);
    json_int(&j, "height", h);
    json_int(&j, "scale", PH_COLOR_MOMENT_SCALE);
    json_t a = json_array(&j, "hist");
    for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
        json_u64s(&a, NULL, s.hist[c], 256);
    }
    json_close_array(&a);
    json_doubles(&j, "moments", &s.moment[0][0], PH_COLOR_CHANNELS * PH_COLOR_MOMENTS);
    json_hexbytes(&j, "digest", s.digest, PH_COLOR_MOMENTS_DIGEST_BYTES);
    json_string(&j, "load_grayscale", ph_get_error_string(gray_err));
    json_end(&j);
    return status | (fclose(f) != 0);
}

/* --8<-- [start:color_moments-digests] */
/* One image of `site_stages color_moments-digests`: its ColorMoments digest, as
 * ph_compute_color_moments_hash() computes it. */
static int digests_row(ph_context_t *ctx, json_t *row, const char *path) {
    ph_digest_t d;
    if (ph_compute_color_moments_hash(ctx, &d) != PH_SUCCESS) {
        return fail("ph_compute_color_moments_hash failed", path);
    }
    json_hexbytes(row, "digest", d.data, d.size);
    return 0;
}

/* site_stages color_moments-digests <image>...: one JSON line per image, as above. */
int mode_color_moments_digests(int argc, char **argv) {
    return for_each_image(argc, argv, digests_row);
}

/* --8<-- [end:color_moments-digests] */
