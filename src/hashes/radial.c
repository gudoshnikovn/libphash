#include "../internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * Helper: Bilinear Interpolation
 */
static double get_pixel_bilinear(const uint8_t *img, int w, int h, double x, double y) {
    if (x < 0 || x >= w - 1 || y < 0 || y >= h - 1)
        return 0.0;

    int x1 = (int)x;
    int y1 = (int)y;
    int x2 = x1 + 1;
    int y2 = y1 + 1;

    double dx = x - x1;
    double dy = y - y1;

    double p1 = img[y1 * w + x1];
    double p2 = img[y1 * w + x2];
    double p3 = img[y2 * w + x1];
    double p4 = img[y2 * w + x2];

    return p1 * (1.0 - dx) * (1.0 - dy) + p2 * dx * (1.0 - dy) + p3 * (1.0 - dx) * dy +
           p4 * dx * dy;
}

PH_API ph_error_t ph_compute_radial_hash(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->is_loaded || !out_digest)
        return PH_ERR_INVALID_ARGUMENT;

    memset(out_digest, 0, sizeof(ph_digest_t));
    out_digest->size = PH_RADIAL_PROJECTIONS;

    size_t img_size = (size_t)ctx->width * ctx->height;
    if (!PH_SAFE_ALLOC_SIZE(ctx->width, ctx->height))
        return PH_ERR_ALLOCATION_FAILED;

    /* Use scratchpad for both gray and blurred temporary buffers.
     * We need 2 * img_size bytes. */
    uint8_t *scratch = ph_get_scratchpad(ctx, img_size * 2);
    if (!scratch)
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t *gray = scratch;
    uint8_t *blurred = scratch + img_size;

    ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, gray);
    ph_apply_gaussian_blur(ctx, gray, ctx->width, ctx->height, blurred);

    ph_apply_gamma(ctx, blurred, ctx->width, ctx->height);

    double centerX = ctx->width / 2.0;
    double centerY = ctx->height / 2.0;
    double min_side = (ctx->width < ctx->height) ? ctx->width : ctx->height;
    double max_radius = min_side / 2.0;
    double projection_variances[PH_RADIAL_PROJECTIONS];
    double max_variance = 0.0;

    for (int i = 0; i < PH_RADIAL_PROJECTIONS; i++) {
        double theta = (i * M_PI) / PH_RADIAL_PROJECTIONS;
        double cos_t = cos(theta);
        double sin_t = sin(theta);
        double sum = 0.0;
        double sum_sq = 0.0;
        int count = 0;

        for (int r = -PH_RADIAL_SAMPLES / 2; r < PH_RADIAL_SAMPLES / 2; r++) {
            double dist = (r * max_radius) / (PH_RADIAL_SAMPLES / 2.0);
            double px = centerX + dist * cos_t;
            double py = centerY + dist * sin_t;
            double val = get_pixel_bilinear(blurred, ctx->width, ctx->height, px, py);
            if (val > 0.0) {
                sum += val;
                sum_sq += val * val;
                count++;
            }
        }

        if (count > 0) {
            double mean = sum / count;
            projection_variances[i] = (sum_sq / count) - (mean * mean);
        } else {
            projection_variances[i] = 0.0;
        }
        if (projection_variances[i] > max_variance)
            max_variance = projection_variances[i];
    }

    /* Normalize and write to digest */
    for (int i = 0; i < PH_RADIAL_PROJECTIONS; i++) {
        if (max_variance > 0.001) {
            out_digest->data[i] = (uint8_t)(sqrt(projection_variances[i] / max_variance) * 255.0);
        } else {
            out_digest->data[i] = 0;
        }
    }

    return PH_SUCCESS;
}
