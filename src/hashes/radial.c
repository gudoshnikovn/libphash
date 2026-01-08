#include "../internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RADIAL_PROJECTIONS 40
#define SAMPLES_PER_LINE 128

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
    out_digest->size = 40;

    size_t img_size = ctx->width * ctx->height;
    uint8_t *gray = malloc(img_size);
    uint8_t *blurred = malloc(img_size);

    if (!gray || !blurred) {
        free(gray);
        free(blurred);
        return PH_ERR_ALLOCATION_FAILED;
    }

    ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, gray);
    ph_apply_gaussian_blur(gray, ctx->width, ctx->height, blurred);

    ph_apply_gamma(ctx, blurred, ctx->width, ctx->height);

    double centerX = ctx->width / 2.0;
    double centerY = ctx->height / 2.0;
    double min_side = (ctx->width < ctx->height) ? ctx->width : ctx->height;
    double maxRadius = min_side / 2.0;
    double variances[RADIAL_PROJECTIONS];
    double max_var = 0.0;

    for (int i = 0; i < RADIAL_PROJECTIONS; i++) {
        double theta = (i * M_PI) / RADIAL_PROJECTIONS;
        double cos_t = cos(theta);
        double sin_t = sin(theta);
        double sum = 0.0;
        double sumSq = 0.0;
        int count = 0;

        for (int r = -SAMPLES_PER_LINE / 2; r < SAMPLES_PER_LINE / 2; r++) {
            double dist = (r * maxRadius) / (SAMPLES_PER_LINE / 2.0);
            double px = centerX + dist * cos_t;
            double py = centerY + dist * sin_t;
            double val = get_pixel_bilinear(blurred, ctx->width, ctx->height, px, py);
            if (val > 0.0) {
                sum += val;
                sumSq += val * val;
                count++;
            }
        }

        if (count > 0) {
            double mean = sum / count;
            variances[i] = (sumSq / count) - (mean * mean);
        } else {
            variances[i] = 0.0;
        }
        if (variances[i] > max_var)
            max_var = variances[i];
    }

    for (int i = 0; i < RADIAL_PROJECTIONS; i++) {
        if (max_var > 0.001) {
            out_digest->data[i] = (uint8_t)(sqrt(variances[i] / max_var) * 255.0);
        } else {
            out_digest->data[i] = 0;
        }
    }

    free(gray);
    free(blurred);
    return PH_SUCCESS;
}
