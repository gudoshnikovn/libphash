#include "../internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

PH_API ph_error_t ph_compute_color_moments_hash(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->is_loaded || !out_digest) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    /* We calculate color moments for RGB channels.
     * Moments are stored as bytes: [MeanR, StdDevR, SkewR, MeanG, ...]
     */
    memset(out_digest, 0, sizeof(ph_digest_t));
    out_digest->size = PH_COLOR_CHANNELS * PH_COLOR_MOMENTS;

    double mean[PH_COLOR_CHANNELS] = {0}, std_dev[PH_COLOR_CHANNELS] = {0},
           skew[PH_COLOR_CHANNELS] = {0};
    int num_pixels = ctx->width * ctx->height;

    /* Step 1: Calculate the Arithmetic Mean */
    for (int i = 0; i < num_pixels; i++) {
        for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
            uint8_t val = (ctx->channels >= 3) ? ctx->data[i * ctx->channels + c] : ctx->data[i * ctx->channels];
            mean[c] += val;
        }
    }
    for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
        mean[c] /= num_pixels;
    }

    /* Step 2: Calculate Standard Deviation (2nd moment) and Skewness (3rd moment) */
    for (int i = 0; i < num_pixels; i++) {
        for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
            uint8_t val = (ctx->channels >= 3) ? ctx->data[i * ctx->channels + c] : ctx->data[i * ctx->channels];
            double diff = val - mean[c];
            std_dev[c] += diff * diff;
            skew[c] += diff * diff * diff;
        }
    }

    for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
        /* Standard deviation normalization */
        std_dev[c] = sqrt(std_dev[c] / num_pixels);

        /* Cube root for skewness normalization */
        skew[c] = cbrt(skew[c] / num_pixels);

        /* Step 3: Write to digest.
         * Moments are mapped to 0-255 range and stored as bytes.
         */
        out_digest->data[c * PH_COLOR_MOMENTS + 0] = (uint8_t)mean[c];
        out_digest->data[c * PH_COLOR_MOMENTS + 1] = (uint8_t)fmin(255.0, std_dev[c]);
        out_digest->data[c * PH_COLOR_MOMENTS + 2] = (uint8_t)fmin(255.0, fabs(skew[c]));
    }

    return PH_SUCCESS;
}
