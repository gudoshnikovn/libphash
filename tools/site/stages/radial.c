/* site_stages: Radial. */
#include "stages.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --8<-- [start:radial-variants-list] */
const radial_variant_t radial_variants[RADIAL_VARIANTS] = {
    [RADIAL_DEFAULT] = {"default", PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES,
                        PH_RADIAL_DEFAULT_SIGMA, 1.0f},
    [RADIAL_SIGMA_1] = {"sigma_1", PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, 1.0f, 1.0f},
    [RADIAL_SIGMA_8] = {"sigma_8", PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES, 8.0f, 1.0f},
    [RADIAL_GAMMA_05] = {"gamma_0.5", PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES,
                         PH_RADIAL_DEFAULT_SIGMA, 0.5f},
    [RADIAL_GAMMA_2] = {"gamma_2", PH_RADIAL_PROJECTIONS, PH_RADIAL_SAMPLES,
                        PH_RADIAL_DEFAULT_SIGMA, 2.0f},
    [RADIAL_GRID_40X32] = {"grid_40x32", 40, 32, PH_RADIAL_DEFAULT_SIGMA, 1.0f},
    [RADIAL_GRID_90X64] = {"grid_90x64", 90, 64, PH_RADIAL_DEFAULT_SIGMA, 1.0f},
    [RADIAL_GRID_360X256] = {"grid_360x256", 360, 256, PH_RADIAL_DEFAULT_SIGMA, 1.0f},
    [RADIAL_GRID_1440X1024] = {"grid_1440x1024", 1440, 1024, PH_RADIAL_DEFAULT_SIGMA, 1.0f},
    [RADIAL_GRID_4096X4096] = {"grid_4096x4096", PH_RADIAL_MAX_PROJECTIONS, PH_RADIAL_MAX_SAMPLES,
                               PH_RADIAL_DEFAULT_SIGMA, 1.0f},
};
/* --8<-- [end:radial-variants-list] */

int radial_settings(ph_context_t *ctx, const radial_variant_t *v) {
    return ph_context_set_radial_params(ctx, v->projections, v->samples, v->sigma) == PH_SUCCESS &&
           ph_context_set_gamma(ctx, v->gamma) == PH_SUCCESS;
}

/* --8<-- [start:radial] */
/* One Radial computation with the given settings, recomputed from its stages and checked
 * against ph_compute_radial_hash() with the same settings: the grayscale image blurred at
 * `sigma` and put through `gamma`; the variance along each of `projections` lines through
 * the center, `samples` points on each; the profile's mean and spread; the profile
 * standardized; its first PH_RADIAL_COEFFS DCT coefficients; and the digest, the
 * coefficients mapped onto 0..255 by their own minimum and maximum (all zero when the
 * profile has no spread worth the name). The context is left at the default settings. */
typedef struct {
    radial_variant_t settings;
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

static int radial_run(ph_context_t *ctx, const radial_variant_t *v, radial_stages_t *s) {
    const int w = ctx->image.width, h = ctx->image.height, projections = v->projections;
    const size_t npix = (size_t)w * (size_t)h;
    memset(s, 0, sizeof(*s));
    s->settings = *v;
    s->variance = malloc((size_t)projections * sizeof(double));
    s->standardized = calloc((size_t)projections, sizeof(double));
    s->blurred = malloc(npix);
    float *scratch = malloc(npix * sizeof(float));
    const uint8_t *gray = ph_get_gray(ctx);
    if (!s->variance || !s->standardized || !s->blurred || !scratch || !gray ||
        !radial_settings(ctx, v)) {
        free(scratch);
        return fail("cannot set up Radial", NULL);
    }
    ph_gaussian_blur_sigma(gray, w, h, v->sigma, scratch, s->blurred);
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
                                   (float)cos(theta), (float)sin(theta), v->samples);
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
    if (!radial_settings(ctx, &radial_variants[RADIAL_DEFAULT]) || bad) {
        return fail("ph_compute_radial_hash failed", NULL);
    }
    char what[64];
    snprintf(what, sizeof(what), "Radial (%d x %d, sigma %g, gamma %g)", projections, v->samples,
             (double)v->sigma, (double)v->gamma);
    return check_digest(what, s->digest, PH_RADIAL_COEFFS, &lib);
}

/* --8<-- [end:radial] */

/* One run as a JSON object, under `key` or as an array element; the profile only when
 * asked for. */
static void radial_json(json_t *j, const char *key, const radial_stages_t *s, int with_profile) {
    json_t o = json_object(j, key);
    json_int(&o, "projections", s->settings.projections);
    json_int(&o, "samples", s->settings.samples);
    json_double(&o, "sigma", s->settings.sigma);
    json_double(&o, "gamma", s->settings.gamma);
    if (with_profile) {
        json_doubles(&o, "variance", s->variance, s->settings.projections);
        json_doubles(&o, "standardized", s->standardized, s->settings.projections);
    }
    json_double(&o, "mean", s->mean);
    json_double(&o, "relative_spread", s->mean > 0.0 ? s->spread_sq / (s->mean * s->mean) : 0.0);
    json_int(&o, "structure", s->structure);
    json_doubles(&o, "coefficients", s->coefficients, PH_RADIAL_COEFFS);
    json_hexbytes(&o, "digest", s->digest, PH_RADIAL_COEFFS);
    json_close_object(&o);
}

/* The points one projection reads, as ph_projection_variance() places them: `samples`
 * points spaced radius / (samples / 2) apart from -radius on, each value bilinear, -1
 * where the point falls outside the image. */
static void radial_line(const radial_stages_t *s, int w, int h, int i, float *out) {
    double theta = i * M_PI / s->settings.projections;
    float c = (float)cos(theta), sn = (float)sin(theta);
    float half = (float)s->settings.samples / 2.0f, scale = (float)s->radius / half;
    for (int r = 0; r < s->settings.samples; r++) {
        float dist = ((float)r - half) * scale;
        out[r] = ph_get_pixel_bilinear(s->blurred, w, h, (float)s->center_x + dist * c,
                                       (float)s->center_y + dist * sn);
    }
}

/* The settings `site_stages radial` shows besides the default, one at a time. */
static const int radial_sigmas[] = {RADIAL_SIGMA_1, RADIAL_SIGMA_8};
static const int radial_gammas[] = {RADIAL_GAMMA_05, RADIAL_GAMMA_2};

/* site_stages radial <image> <outdir>
 *     original.ppm, gray.pgm, blurred.pgm (the blur, and the gamma, which is the identity
 *     at its default), sigma-<s>.pgm and gamma-<g>.pgm for the other settings shown, and
 *     radial.json: the center and radius; for the default, the variance along every
 *     line, the profile standardized, the 40 DCT coefficients and the digest, and the
 *     points each line reads; the same without the points for the other sigmas and
 *     gammas; every digest checked against ph_compute_radial_hash() with those
 *     settings; and the digest of the image loaded with ph_context_set_load_grayscale(),
 *     checked the same way. */
int mode_radial(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;
    radial_stages_t def = {0}, sig[COUNT(radial_sigmas)] = {{0}}, gam[COUNT(radial_gammas)] = {{0}},
                    dec = {0};
    int bad = !write_gray_stages(ctx, outdir, &status) ||
              radial_run(ctx, &radial_variants[RADIAL_DEFAULT], &def);
    for (size_t k = 0; k < COUNT(radial_sigmas) && !bad; k++) {
        bad = radial_run(ctx, &radial_variants[radial_sigmas[k]], &sig[k]);
    }
    for (size_t k = 0; k < COUNT(radial_gammas) && !bad; k++) {
        bad = radial_run(ctx, &radial_variants[radial_gammas[k]], &gam[k]);
    }
    ph_free(ctx);
    if (!bad) {
        bad = load_decoder_gray(&ctx, argv[0]) ||
              radial_run(ctx, &radial_variants[RADIAL_DEFAULT], &dec);
        ph_free(ctx);
    }

    const int projections = def.settings.projections, samples = def.settings.samples;
    float *lines = bad ? NULL : malloc((size_t)projections * samples * sizeof(float));
    FILE *f = NULL;
    if (lines) {
        for (int i = 0; i < projections; i++) {
            radial_line(&def, w, h, i, lines + (size_t)i * samples);
        }
        status |= write_pnm(outdir, "blurred.pgm", def.blurred, w, h, 1);
        for (size_t k = 0; k < COUNT(radial_sigmas); k++) {
            char name[32];
            snprintf(name, sizeof(name), "sigma-%g.pgm", (double)sig[k].settings.sigma);
            status |= write_pnm(outdir, name, sig[k].blurred, w, h, 1);
        }
        for (size_t k = 0; k < COUNT(radial_gammas); k++) {
            char name[32];
            snprintf(name, sizeof(name), "gamma-%g.pgm", (double)gam[k].settings.gamma);
            status |= write_pnm(outdir, name, gam[k].blurred, w, h, 1);
        }
        f = open_out(outdir, "radial.json");
    }
    if (f) {
        json_t j = json_begin(f);
        json_int(&j, "width", w);
        json_int(&j, "height", h);
        json_double(&j, "center_x", def.center_x);
        json_double(&j, "center_y", def.center_y);
        json_double(&j, "radius", def.radius);
        json_double(&j, "min_mean_variance", PH_RADIAL_MIN_MEAN_VARIANCE);
        json_double(&j, "min_relative_spread", PH_RADIAL_MIN_RELATIVE_SPREAD);
        radial_json(&j, "default", &def, 1);
        json_floats(&j, "lines", lines, projections * samples);
        json_t a = json_array(&j, "sigmas");
        for (size_t k = 0; k < COUNT(radial_sigmas); k++) {
            radial_json(&a, NULL, &sig[k], 1);
        }
        json_close_array(&a);
        a = json_array(&j, "gammas");
        for (size_t k = 0; k < COUNT(radial_gammas); k++) {
            radial_json(&a, NULL, &gam[k], 1);
        }
        json_close_array(&a);
        json_hexbytes(&j, "digest_load_grayscale", dec.digest, PH_RADIAL_COEFFS);
        json_end(&j);
        status |= fclose(f) != 0;
    }
    free(lines);
    radial_free(&def);
    radial_free(&dec);
    for (size_t k = 0; k < COUNT(radial_sigmas); k++) {
        radial_free(&sig[k]);
    }
    for (size_t k = 0; k < COUNT(radial_gammas); k++) {
        radial_free(&gam[k]);
    }
    return f ? status : 1;
}

/* --8<-- [start:radial-profiles] */
/* site_stages radial-profiles <reference> <image>...: for every image, the default
 * Radial's variance profile and digest, recomputed from its stages and checked against
 * ph_compute_radial_hash(), and the digest compared with the reference's by
 * ph_radial_similarity() (null where the library refuses, an image with no angular
 * structure): one JSON line per image, the reference first. */
int mode_radial_profiles(int argc, char **argv) {
    ph_digest_t ref = {0};
    for (int i = 0; i < argc; i++) {
        ph_context_t *ctx = NULL;
        if (load_image(&ctx, argv[i])) {
            return 1;
        }
        radial_stages_t s;
        ph_digest_t d;
        int bad = radial_run(ctx, &radial_variants[RADIAL_DEFAULT], &s) ||
                  ph_compute_radial_hash(ctx, &d) != PH_SUCCESS;
        ph_free(ctx);
        if (bad) {
            radial_free(&s);
            return 1;
        }
        if (i == 0) {
            ref = d;
        }
        json_t j = json_begin(stdout);
        json_string(&j, "file", argv[i]);
        radial_json(&j, "radial", &s, 1);
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
/* One image of `site_stages radial-variants`: its Radial digest under each setting of
 * radial_variants[], as ph_compute_radial_hash() computes it. */
static int radial_variants_row(ph_context_t *ctx, json_t *row, const char *path) {
    (void)path;
    for (int k = 0; k < RADIAL_VARIANTS; k++) {
        ph_digest_t d;
        if (!radial_settings(ctx, &radial_variants[k]) ||
            ph_compute_radial_hash(ctx, &d) != PH_SUCCESS) {
            return fail("ph_compute_radial_hash failed", radial_variants[k].name);
        }
        json_hexbytes(row, radial_variants[k].name, d.data, d.size);
    }
    return 0;
}

/* site_stages radial-variants <image>...: one JSON line per image, as above. */
int mode_radial_variants(int argc, char **argv) {
    return for_each_image(argc, argv, radial_variants_row);
}

/* --8<-- [end:radial-variants] */
