/* Radial -- radial variance hash.
 *
 * C. De Roover, C. De Vleeschouwer, F. Lefebvre, B. Macq, "Robust image hashing based
 * on radial variance of pixels", ICIP 2005, vol. 3, pp. 77-80,
 * doi:10.1109/ICIP.2005.1530575. This is the algorithm pHash implements. Its
 * predecessor, RASH (Lefebvre, Macq, Legat, EUSIPCO 2002), was reported by its own
 * authors as troubled and superseded by the above; cite it as background only.
 * Both papers are paywalled; the description followed here is Zauner's (Diplomarbeit,
 * FH Hagenberg 2010, sections 3.1.3 and 3.2.3).
 *
 * The variance formula below is the source's definition 3.6 exactly:
 *   R[a] = E[I^2] - (E[I])^2 over the pixels on the projection line at angle a.
 *
 * KNOWN DIVERGENCES FROM THE SOURCE, all tracked as defects in
 * docs/algorithm-provenance.md:
 *
 *   1. The source computes R[a] for a = 0..179 -- 180 angles -- and then applies a DCT
 *      to that vector, keeping the FIRST 40 COEFFICIENTS as the hash, which is what
 *      decorrelates it. This code takes 40 angles and no DCT: the 40 was transplanted
 *      from the coefficient count to the angle count.
 *   2. The source compares two hashes by the peak of cross-correlation, which is what
 *      turns a rotation -- a cyclic shift of the radial vector -- into a match. This
 *      library compares digests element-wise, so no rotation invariance is delivered.
 *   3. pHash's authors suggest sigma = 1 and gamma = 1; PH_DEFAULT_GAMMA is 2.2, so the
 *      reference and this code see different pixels before the variance is computed.
 *
 * Deliberate differences: a fixed sample count per projection with bilinear
 * interpolation, rather than summing the pixels of a one-pixel-wide strip whose length
 * varies with angle and resolution; a radius capped at min(w,h)/2 to keep every
 * projection inside the image; and normalisation by the maximum variance followed by a
 * square root, which exists only to fit the values into bytes.
 */
#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

float ph_get_pixel_bilinear(const uint8_t *img, int w, int h, float x, float y) {
    if (x < 0.0f || x >= (float)(w - 1) || y < 0.0f || y >= (float)(h - 1))
        return -1.0f;

    int x1 = (int)x;
    int y1 = (int)y;
    int x2 = x1 + 1;
    int y2 = y1 + 1;

    float dx = x - (float)x1;
    float dy = y - (float)y1;

    float p1 = (float)img[y1 * w + x1];
    float p2 = (float)img[y1 * w + x2];
    float p3 = (float)img[y2 * w + x1];
    float p4 = (float)img[y2 * w + x2];

    return p1 * (1.0f - dx) * (1.0f - dy) + p2 * dx * (1.0f - dy) + p3 * (1.0f - dx) * dy +
           p4 * dx * dy;
}

double ph_projection_variance(const uint8_t *img, int w, int h, double cx, double cy,
                              double max_radius, float cos_t, float sin_t, int samples) {
    double sum = 0.0;
    double sum_sq = 0.0;
    int count = 0;

    float f_cx = (float)cx;
    float f_cy = (float)cy;
    float f_max_radius = (float)max_radius;
    float f_samples_half = (float)samples / 2.0f;
    float scale = f_max_radius / f_samples_half;

    for (int r = 0; r < samples; r++) {
        float f_r = (float)r - f_samples_half;
        float dist = f_r * scale;
        float px = f_cx + dist * cos_t;
        float py = f_cy + dist * sin_t;

        float val = ph_get_pixel_bilinear(img, w, h, px, py);
        if (val >= 0.0f) {
            sum += (double)val;
            sum_sq += (double)(val * val);
            count++;
        }
    }

    if (count > 0) {
        double mean = sum / (double)count;
        double var = (sum_sq / (double)count) - (mean * mean);
        if (var < 0)
            var = 0;
        return var;
    }
    return 0.0;
}

PH_API ph_error_t ph_compute_radial_hash(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->image.is_loaded || !out_digest)
        return PH_ERR_INVALID_ARGUMENT;

    int projections = ctx->config.radial_projections;
    int samples = ctx->config.radial_samples;
    if (projections <= 0 || samples <= 0)
        return PH_ERR_INVALID_ARGUMENT;

    /* projections * sizeof(double) does not overflow size_t on a 64-bit target, but it
     * does on a 32-bit one. Since 2.0.0 ph_context_set_radial_params() caps projections
     * at PH_DIGEST_MAX_BYTES, so this cannot trigger through the public API either;
     * kept as defence in depth. Refuse rather than wrap (R03/H6). */
    if ((size_t)projections > SIZE_MAX / sizeof(double))
        return PH_ERR_ALLOCATION_FAILED;

    memset(out_digest, 0, sizeof(ph_digest_t));
    /* Clamp size to the max supported by ph_digest_t.
     * Unreachable through the public API since 2.0.0: the setter rejects
     * projections > PH_DIGEST_MAX_BYTES, precisely because this clamp used to hand back
     * PH_SUCCESS with a digest quietly shorter than the caller had configured. Kept for a
     * config field written by some other route (tests do exactly that). */
    out_digest->size =
        (uint8_t)(projections > PH_DIGEST_MAX_BYTES ? PH_DIGEST_MAX_BYTES : projections);

    size_t img_size = (size_t)ctx->image.width * (size_t)ctx->image.height;

    uint8_t *gray = ph_get_gray(ctx);
    if (!gray)
        return PH_ERR_ALLOCATION_FAILED;

    // `blurred` has to survive the call into ph_apply_gaussian_blur() below, which
    // itself grabs its own scratchpad region — the arena may grow (reallocating and
    // freeing its backing buffer) to satisfy that nested request, which would
    // invalidate any pointer of ours already sitting in the arena. So this one stays
    // a plain heap allocation; only projection_variances (arena-allocated further
    // down, after every arena-using call has already happened) gets the scratchpad.
    uint8_t *blurred = (uint8_t *)malloc(img_size);
    if (!blurred)
        return PH_ERR_ALLOCATION_FAILED;

    ph_apply_gaussian_blur(ctx, gray, ctx->image.width, ctx->image.height, blurred);
    ph_apply_gamma(ctx, blurred, ctx->image.width, ctx->image.height);

    size_t saved_offset = ctx->arena.offset;
    double *projection_variances =
        (double *)ph_get_scratchpad(ctx, (size_t)projections * sizeof(double));
    if (!projection_variances) {
        free(blurred);
        return PH_ERR_ALLOCATION_FAILED;
    }

    double centerX = (double)ctx->image.width / 2.0;
    double centerY = (double)ctx->image.height / 2.0;
    double min_side = (ctx->image.width < ctx->image.height) ? (double)ctx->image.width
                                                             : (double)ctx->image.height;
    double max_radius = min_side / 2.0;
    double max_variance = 0.0;

    for (int i = 0; i < projections; i++) {
        double theta = (i * M_PI) / (double)projections;
        float cos_t = (float)cos(theta);
        float sin_t = (float)sin(theta);

        projection_variances[i] =
            ph_projection_variance(blurred, ctx->image.width, ctx->image.height, centerX, centerY,
                                   max_radius, cos_t, sin_t, samples);

        if (projection_variances[i] > max_variance)
            max_variance = projection_variances[i];
    }

    /* Normalize and write to digest */
    for (int i = 0; i < out_digest->size; i++) {
        if (max_variance > 0.001) {
            out_digest->data[i] = (uint8_t)(sqrt(projection_variances[i] / max_variance) * 255.0);
        } else {
            out_digest->data[i] = 0;
        }
    }

    ctx->arena.offset = saved_offset;
    free(blurred);

    return PH_SUCCESS;
}
