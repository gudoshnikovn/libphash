#include "../internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

double ph_get_pixel_bilinear(const uint8_t *img, int w, int h, double x, double y) {
    if (x < 0 || x >= w - 1 || y < 0 || y >= h - 1)
        return -1.0;

    int x1 = (int)x;
    int y1 = (int)y;
    int x2 = x1 + 1;
    int y2 = y1 + 1;

    double dx = x - (double)x1;
    double dy = y - (double)y1;

    double p1 = (double)img[y1 * w + x1];
    double p2 = (double)img[y1 * w + x2];
    double p3 = (double)img[y2 * w + x1];
    double p4 = (double)img[y2 * w + x2];

    return p1 * (1.0 - dx) * (1.0 - dy) + p2 * dx * (1.0 - dy) + p3 * (1.0 - dx) * dy +
           p4 * dx * dy;
}

double ph_projection_variance(const uint8_t *img, int w, int h, double cx, double cy,
                              double max_radius, double cos_t, double sin_t, int samples) {
    double sum = 0.0;
    double sum_sq = 0.0;
    int count = 0;

    for (int r = 0; r < samples; r++) {
        double real_r = (double)r - (samples / 2.0);
        double dist = (real_r * max_radius) / (samples / 2.0);
        double px = cx + dist * cos_t;
        double py = cy + dist * sin_t;

        double val = ph_get_pixel_bilinear(img, w, h, px, py);
        if (val >= 0.0) {
            sum += val;
            sum_sq += val * val;
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

    memset(out_digest, 0, sizeof(ph_digest_t));
    /* Clamp size to the max supported by ph_digest_t */
    out_digest->size =
        (uint8_t)(projections > PH_DIGEST_MAX_BYTES ? PH_DIGEST_MAX_BYTES : projections);

    size_t img_size = (size_t)ctx->image.width * (size_t)ctx->image.height;

    uint8_t *gray = ph_get_gray(ctx);
    if (!gray)
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t *blurred = (uint8_t *)malloc(img_size);
    if (!blurred) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    double *projection_variances = (double *)malloc((size_t)projections * sizeof(double));
    if (!projection_variances) {
        free(blurred);
        return PH_ERR_ALLOCATION_FAILED;
    }

    ph_apply_gaussian_blur(ctx, gray, ctx->image.width, ctx->image.height, blurred);
    ph_apply_gamma(ctx, blurred, ctx->image.width, ctx->image.height);

    double centerX = (double)ctx->image.width / 2.0;
    double centerY = (double)ctx->image.height / 2.0;
    double min_side = (ctx->image.width < ctx->image.height) ? (double)ctx->image.width
                                                             : (double)ctx->image.height;
    double max_radius = min_side / 2.0;
    double max_variance = 0.0;

    for (int i = 0; i < projections; i++) {
        double theta = (i * M_PI) / (double)projections;
        double cos_t = cos(theta);
        double sin_t = sin(theta);

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

    free(blurred);
    free(projection_variances);

    return PH_SUCCESS;
}
