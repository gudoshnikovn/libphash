#include "../internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

PH_API ph_error_t ph_compute_color_hash(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->is_loaded || !out_digest) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    /* We calculate 3 color moments for 3 channels (R, G, B) = 9 values total.
     * Each value is stored as 1 byte (9 bytes required).
     */
    memset(out_digest, 0, sizeof(ph_digest_t));
    out_digest->size = 9; // Set the actual size in bytes.

    double mean[3] = {0}, std_dev[3] = {0}, skew[3] = {0};
    int num_pixels = ctx->width * ctx->height;

    /* Step 1: Calculate the Arithmetic Mean */
    for (int i = 0; i < num_pixels; i++) {
        for (int c = 0; c < 3; c++) {
            mean[c] += ctx->data[i * ctx->channels + c];
        }
    }
    for (int c = 0; c < 3; c++) {
        mean[c] /= num_pixels;
    }

    /* Step 2: Calculate Standard Deviation (2nd moment) and Skewness (3rd moment)
     */
    for (int i = 0; i < num_pixels; i++) {
        for (int c = 0; c < 3; c++) {
            double diff = ctx->data[i * ctx->channels + c] - mean[c];
            std_dev[c] += diff * diff;
            skew[c] += diff * diff * diff;
        }
    }

    for (int c = 0; c < 3; c++) {
        /* Standard deviation normalization */
        std_dev[c] = sqrt(std_dev[c] / num_pixels);

        /* Cube root for skewness normalization */
        skew[c] = cbrt(skew[c] / num_pixels);

        /* * Step 3: Write to digest.
         * Moments are mapped to 0-255 range and stored as bytes.
         */
        out_digest->data[c * 3 + 0] = (uint8_t)mean[c];
        out_digest->data[c * 3 + 1] = (uint8_t)fmin(255.0, std_dev[c]);
        out_digest->data[c * 3 + 2] = (uint8_t)fmin(255.0, fabs(skew[c]));
    }

    return PH_SUCCESS;
}
