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
 * The algorithm is two steps, and both are the source's:
 *
 *   1. The radial variance vector, definition 3.6 exactly, for a = 0..179 -- 180 angles,
 *      because the Radon transform is symmetric and 180 therefore covers the circle:
 *        R[a] = E[I^2] - (E[I])^2 over the pixels on the projection line at angle a.
 *   2. A 1D DCT of that vector, of which the first 40 coefficients are the hash. Zauner
 *      3.1.3: "the perceptual image hash function was further improved by applying the
 *      DCT to the radial variance vector. The first 40 coefficients of the transformed
 *      radial variance vector form the so-called radial hash vector in the end. This
 *      omits redundant components of the radial variance vector and efficiently
 *      decorrelates it."
 *
 * Before 2.0.0 step 2 was missing and the 40 sat on the angle count instead: 40 angles,
 * no transform. That is a different algorithm -- 4.5x coarser angularly, and correlated
 * across neighbouring elements, which is the redundancy the DCT exists to remove.
 *
 * Quantisation follows pHash's own ph_dct(): the 40 coefficients are mapped affinely
 * onto 0..255 by their own minimum and maximum. That is what keeps the sign -- the most
 * negative coefficient is 0, not a wrapped byte -- and it makes the digest invariant to
 * a positive rescaling of the whole variance vector, which is what a contrast change
 * mostly does to it. The pre-DCT "divide by the maximum variance, then take the square
 * root" of earlier versions is gone: it existed only to fit variances into bytes, it is
 * not in the source, and a square root before a transform is not a scaling but a
 * different signal.
 *
 * A flat image is handled before any of that: if no projection has a variance above
 * PH_RADIAL_FLAT_VARIANCE the digest is all zeroes. Without that gate the min-max stretch
 * would scale the floating-point residue of a blank image up to the full byte range and
 * return a hash made of rounding noise.
 *
 * Observation, not a divergence: coefficient 0 is the sum of the variances over sqrt(N)
 * and every variance is non-negative, so it is in practice always the largest of the 40
 * and therefore always quantises to 255. One of the 40 bytes thus carries no
 * information. The source keeps it, so this code keeps it.
 *
 * KNOWN DIVERGENCES FROM THE SOURCE, tracked as defects in
 * docs/algorithm-provenance.md:
 *
 *   1. The source compares two hashes by the peak of cross-correlation, which is what
 *      turns a rotation -- a cyclic shift of the radial vector -- into a match. This
 *      library compares digests element-wise, so no rotation invariance is delivered.
 *   2. pHash's authors suggest sigma = 1 and gamma = 1; PH_DEFAULT_GAMMA is 2.2, so the
 *      reference and this code see different pixels before the variance is computed.
 *
 * Deliberate differences: a fixed sample count per projection with bilinear
 * interpolation, rather than summing the pixels of a one-pixel-wide strip whose length
 * varies with angle and resolution; and a radius capped at min(w,h)/2 to keep every
 * projection inside the image.
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

/* DCT-II, orthonormally scaled, first `coeffs` coefficients only:
 *
 *   X[k] = c(k) * sum_{n=0}^{N-1} x[n] * cos(pi * (2n + 1) * k / (2N))
 *   c(0) = 1/sqrt(N), c(k>0) = sqrt(2/N)
 *
 * Computed straight from the definition rather than through a fast transform: the
 * partial output (40 of 180 coefficients) is what the algorithm wants, N is small, and
 * the projection sampling above it costs several times more. */
ph_error_t ph_dct1d_partial(const double *in, int n, int coeffs, double *out) {
    if (!in || !out || n < 1 || coeffs < 1 || coeffs > n)
        return PH_ERR_INVALID_ARGUMENT;

    const double scale = M_PI / (2.0 * (double)n);
    const double c0 = 1.0 / sqrt((double)n);
    const double ck = sqrt(2.0 / (double)n);

    for (int k = 0; k < coeffs; k++) {
        double sum = 0.0;
        for (int i = 0; i < n; i++)
            sum += in[i] * cos(scale * (double)(2 * i + 1) * (double)k);
        out[k] = sum * (k == 0 ? c0 : ck);
    }
    return PH_SUCCESS;
}

PH_API ph_error_t ph_compute_radial_hash(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->image.is_loaded || !out_digest)
        return PH_ERR_INVALID_ARGUMENT;

    int projections = ctx->config.radial_projections;
    int samples = ctx->config.radial_samples;
    /* Fewer angles than coefficients is not a coarser hash, it is no hash: a DCT of an
     * n-element vector has n coefficients. The setter rejects it; refuse here too rather
     * than hand back a digest quietly shorter than the caller configured, which is what
     * the old clamp against PH_DIGEST_MAX_BYTES did. */
    if (projections < PH_RADIAL_COEFFS || samples <= 0)
        return PH_ERR_INVALID_ARGUMENT;

    /* projections * sizeof(double) does not overflow size_t on a 64-bit target, but it
     * does on a 32-bit one. Since 2.0.0 ph_context_set_radial_params() caps projections
     * at PH_RADIAL_MAX_PROJECTIONS, so this cannot trigger through the public API either;
     * kept as defence in depth. Refuse rather than wrap (R03/H6). */
    if ((size_t)projections > SIZE_MAX / sizeof(double))
        return PH_ERR_ALLOCATION_FAILED;

    memset(out_digest, 0, sizeof(ph_digest_t));
    /* The digest is the DCT coefficients, so its width no longer follows the angle
     * count: it is PH_RADIAL_COEFFS whatever the configuration. */
    out_digest->size = (uint8_t)PH_RADIAL_COEFFS;

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

    /* A flat image has no radial structure to describe, and what is left in the variance
     * vector at that point is floating-point residue. The quantisation below is a
     * per-image min-max stretch, so it would amplify that residue to the full 0..255
     * range and hand back a hash decided by rounding noise. Answer with zeroes instead --
     * the same threshold, and the same answer, as before the DCT was introduced. */
    if (max_variance <= PH_RADIAL_FLAT_VARIANCE) {
        ctx->arena.offset = saved_offset;
        free(blurred);
        return PH_SUCCESS;
    }

    /* The transform, and the whole point of it: it decorrelates neighbouring angles and
     * compresses 180 numbers into the 40 that carry the shape of the profile. */
    double coefficients[PH_RADIAL_COEFFS];
    ph_error_t err =
        ph_dct1d_partial(projection_variances, projections, PH_RADIAL_COEFFS, coefficients);

    ctx->arena.offset = saved_offset;
    free(blurred);

    if (err != PH_SUCCESS)
        return err;

    /* Quantise as pHash does: affine map from [min, max] over the 40 coefficients onto
     * 0..255. */
    double min_c = coefficients[0];
    double max_c = coefficients[0];
    for (int k = 1; k < PH_RADIAL_COEFFS; k++) {
        if (coefficients[k] < min_c)
            min_c = coefficients[k];
        if (coefficients[k] > max_c)
            max_c = coefficients[k];
    }

    double span = max_c - min_c;
    if (span <= 0.0) {
        /* Unreachable for a non-flat image -- coefficient 0 is the sum of the variances
         * and the rest are differences -- but the map is undefined here, so say so
         * rather than divide by zero. */
        memset(out_digest->data, 0, PH_RADIAL_COEFFS);
        return PH_SUCCESS;
    }
    for (int k = 0; k < PH_RADIAL_COEFFS; k++) {
        double q = 255.0 * (coefficients[k] - min_c) / span;
        out_digest->data[k] = (uint8_t)(q < 0.0 ? 0.0 : q > 255.0 ? 255.0 : q);
    }

    return PH_SUCCESS;
}
