#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

PH_API ph_error_t ph_compute_color_moments_hash(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->image.is_loaded || !out_digest) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    if (ctx->image.width <= 0 || ctx->image.height <= 0)
        return PH_ERR_EMPTY_IMAGE;

    /* R08/M13: with fewer than 3 channels every moment would be computed from the same
     * byte, yielding three identical channels under a PH_SUCCESS. Refuse, and do it
     * before touching out_digest so a failed call leaves the caller's buffer alone. */
    if (ctx->image.channels < 3)
        return PH_ERR_REQUIRES_COLOR;

    memset(out_digest, 0, sizeof(ph_digest_t));
    out_digest->size = PH_COLOR_CHANNELS * PH_COLOR_MOMENTS;

    /* size_t, not int: width * height overflows int above ~46340x46340 (R03/H6). */
    size_t num_pixels = (size_t)ctx->image.width * (size_t)ctx->image.height;

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

ph_channel_moments_t ph_compute_moments(const uint8_t *data, size_t num_pixels, int channels,
                                        int channel_index) {
    ph_channel_moments_t m = {0, 0, 0};
    if (!data || num_pixels == 0 || channels <= 0)
        return m;

    /* `i * ch` in size_t: an int index would overflow well before num_pixels does. */
    size_t ch = (size_t)channels;

    /* Step 1: Calculate the Arithmetic Mean */
    for (size_t i = 0; i < num_pixels; i++) {
        uint8_t val = (channels >= 3) ? data[i * ch + (size_t)channel_index] : data[i * ch];
        m.mean += val;
    }
    m.mean /= (double)num_pixels;

    /* Step 2: Calculate Standard Deviation (2nd moment) and Skewness (3rd moment) */
    for (size_t i = 0; i < num_pixels; i++) {
        uint8_t val = (channels >= 3) ? data[i * ch + (size_t)channel_index] : data[i * ch];
        double diff = val - m.mean;
        m.std_dev += diff * diff;
        m.skew += diff * diff * diff;
    }

    m.std_dev = sqrt(m.std_dev / (double)num_pixels);
    m.skew = cbrt(m.skew / (double)num_pixels);

    return m;
}
