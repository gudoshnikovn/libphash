#include "../internal.h"
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// --- Radial Lookup Cache ---
typedef struct {
    double x_factor; // (r / (samples/2)) * cos(theta)
    double y_factor; // (r / (samples/2)) * sin(theta)
} ph_radial_point_t;

static ph_radial_point_t s_radial_cache[PH_RADIAL_PROJECTIONS * PH_RADIAL_SAMPLES];
static atomic_flag s_radial_lock = ATOMIC_FLAG_INIT;
static atomic_bool s_radial_init = false;

static void ensure_radial_initialized(void) {
    if (atomic_load(&s_radial_init))
        return;

    while (atomic_flag_test_and_set(&s_radial_lock)) {
    }

    if (!atomic_load(&s_radial_init)) {
        for (int i = 0; i < PH_RADIAL_PROJECTIONS; i++) {
            double theta = (i * M_PI) / PH_RADIAL_PROJECTIONS;
            double cos_t = cos(theta);
            double sin_t = sin(theta);

            for (int r = 0; r < PH_RADIAL_SAMPLES; r++) {
                // r maps to index in cache. In logic, r goes from -samples/2 to samples/2
                // Logic: real_r = r - samples/2
                double real_r = r - (PH_RADIAL_SAMPLES / 2);
                double dist_factor = real_r / (PH_RADIAL_SAMPLES / 2.0);

                s_radial_cache[i * PH_RADIAL_SAMPLES + r].x_factor = dist_factor * cos_t;
                s_radial_cache[i * PH_RADIAL_SAMPLES + r].y_factor = dist_factor * sin_t;
            }
        }
        atomic_store(&s_radial_init, true);
    }
    atomic_flag_clear(&s_radial_lock);
}

static double get_pixel_bilinear(const uint8_t *img, int w, int h, double x, double y) {
    if (x < 0 || x >= w - 1 || y < 0 || y >= h - 1)
        return -1.0;

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
    if (!ctx || !ctx->image.is_loaded || !out_digest)
        return PH_ERR_INVALID_ARGUMENT;

    int projections = ctx->config.radial_projections;
    int samples = ctx->config.radial_samples;

    memset(out_digest, 0, sizeof(ph_digest_t));
    /* Clamp size to the max supported by ph_digest_t */
    out_digest->size =
        (uint8_t)(projections > PH_DIGEST_MAX_BYTES ? PH_DIGEST_MAX_BYTES : projections);

    size_t img_size = (size_t)ctx->image.width * ctx->image.height;

    uint8_t *gray = ph_get_gray(ctx);
    if (!gray)
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t *blurred = malloc(img_size);
    if (!blurred) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    double *projection_variances = malloc(projections * sizeof(double));
    if (!projection_variances) {
        free(blurred);
        return PH_ERR_ALLOCATION_FAILED;
    }

    ph_apply_gaussian_blur(ctx, gray, ctx->image.width, ctx->image.height, blurred);

    ph_apply_gamma(ctx, blurred, ctx->image.width, ctx->image.height);

    double centerX = ctx->image.width / 2.0;
    double centerY = ctx->image.height / 2.0;
    double min_side = (ctx->image.width < ctx->image.height) ? ctx->image.width : ctx->image.height;
    double max_radius = min_side / 2.0;
    double max_variance = 0.0;

    // Check if we can use cache
    bool use_cache = (projections == PH_RADIAL_PROJECTIONS && samples == PH_RADIAL_SAMPLES);
    if (use_cache) {
        ensure_radial_initialized();
    }

    for (int i = 0; i < projections; i++) {
        double theta = (i * M_PI) / projections;
        double cos_t = cos(theta);
        double sin_t = sin(theta);

        double sum = 0.0;
        double sum_sq = 0.0;
        int count = 0;

        for (int r = 0; r < samples; r++) {
            double px, py;

            if (use_cache) {
                ph_radial_point_t *pt = &s_radial_cache[i * PH_RADIAL_SAMPLES + r];
                px = centerX + max_radius * pt->x_factor;
                py = centerY + max_radius * pt->y_factor;
            } else {
                double real_r = r - (samples / 2);
                double dist = (real_r * max_radius) / (samples / 2.0);
                px = centerX + dist * cos_t;
                py = centerY + dist * sin_t;
            }

            double val = get_pixel_bilinear(blurred, ctx->image.width, ctx->image.height, px, py);
            if (val >= 0.0) {
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
