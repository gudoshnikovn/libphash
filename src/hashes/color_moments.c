#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

PH_API ph_error_t ph_compute_color_moments_hash(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->image.is_loaded || !out_digest) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    memset(out_digest, 0, sizeof(ph_digest_t));
    out_digest->size = PH_COLOR_CHANNELS * PH_COLOR_MOMENTS;

    int num_pixels = ctx->image.width * ctx->image.height;

    for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
        ph_channel_moments_t m =
            ph_compute_moments(ctx->image.raw_rgb, num_pixels, ctx->image.channels, c);

        /* Write to digest. Mapping to 0-255 range and stored as bytes. */
        out_digest->data[c * PH_COLOR_MOMENTS + 0] = (uint8_t)m.mean;
        out_digest->data[c * PH_COLOR_MOMENTS + 1] = (uint8_t)fmin(255.0, m.std_dev);
        out_digest->data[c * PH_COLOR_MOMENTS + 2] = (uint8_t)fmin(255.0, fabs(m.skew));
    }

    return PH_SUCCESS;
}

ph_channel_moments_t ph_compute_moments(const uint8_t *data, int num_pixels, int channels,
                                        int channel_index) {
    ph_channel_moments_t m = {0, 0, 0};
    if (num_pixels <= 0)
        return m;

    /* Step 1: Calculate the Arithmetic Mean */
    for (int i = 0; i < num_pixels; i++) {
        uint8_t val = (channels >= 3) ? data[i * channels + channel_index] : data[i * channels];
        m.mean += val;
    }
    m.mean /= num_pixels;

    /* Step 2: Calculate Standard Deviation (2nd moment) and Skewness (3rd moment) */
    for (int i = 0; i < num_pixels; i++) {
        uint8_t val = (channels >= 3) ? data[i * channels + channel_index] : data[i * channels];
        double diff = val - m.mean;
        m.std_dev += diff * diff;
        m.skew += diff * diff * diff;
    }

    m.std_dev = sqrt(m.std_dev / num_pixels);
    m.skew = cbrt(m.skew / num_pixels);

    return m;
}
