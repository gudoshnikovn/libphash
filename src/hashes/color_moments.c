/* ColorMoments -- the first three central moments of each colour channel.
 *
 * Markus Stricker, Markus Orengo, "Similarity of color images", Proc. SPIE 2420,
 * Storage and Retrieval for Image and Video Databases III, 1995, pp. 381-392,
 * doi:10.1117/12.205308.
 *
 * CAVEAT ON THE SOURCE: the paper is paywalled and no rank-1 restatement of it was
 * found. The formulas below come from N. Keen, "Color Moments", University of Edinburgh
 * CVonline course notes, 2005 -- student coursework, the weakest evidence anywhere in
 * this library's documentation. Re-check against the paper before changing anything on
 * the strength of it.
 *
 * The three moments per channel, over N pixels:
 *   mean     E = (1/N) * sum(p)
 *   std dev  s = sqrt( (1/N) * sum((p - E)^2) )
 *   skewness k = cbrt( (1/N) * sum((p - E)^3) )
 * computed below exactly as stated, in double, with cbrt() rather than pow(x, 1.0/3.0)
 * so that a negative third moment is handled correctly.
 *
 * The digest keeps all three as signed 16-bit big-endian fixed point in units of
 * 1/PH_COLOR_MOMENT_SCALE. Two signed bytes per moment, because the skewness carries a
 * sign -- the direction of the asymmetry, half of what the third moment says -- and a
 * single unsigned byte would make mirrored channel distributions identical. At this scale
 * nothing clamps and the resolution is 1/128 of a channel level.
 *
 * One divergence remains, recorded in docs/algorithm-provenance.md: the source computes
 * the moments in HSV where this code uses the raw RGB channels. That is deliberate and
 * separate -- the source's formulas are known here only through a rank-4 restatement, so
 * the colour space is not changed on the strength of it.
 */
#include "context.h"
#include "hashes/hashes.h"
#include "safety.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

PH_API ph_error_t ph_compute_color_moments_hash(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !out_digest) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    if (!ctx->image.is_loaded) {
        return PH_ERR_EMPTY_IMAGE;
    }

    if (ctx->image.width <= 0 || ctx->image.height <= 0) {
        return PH_ERR_EMPTY_IMAGE;
    }

    /* With fewer than 3 channels every moment would be computed from the same
     * byte, yielding three identical channels under a PH_SUCCESS. Refuse, and do it
     * before touching out_digest so a failed call leaves the caller's buffer alone. */
    if (ctx->image.channels < 3) {
        return PH_ERR_REQUIRES_COLOR;
    }

    /* Nine signed 16-bit fixed-point moments: compare with ph_l2_distance(). */
    ph_digest_begin(out_digest, ctx, PH_ALGO_COLOR_MOMENTS);

    /* size_t, not int: width * height overflows int above ~46340x46340. */
    size_t num_pixels = ph_size(ctx->image.width) * ph_size(ctx->image.height);

    ph_channel_moments_t channel_moments[PH_COLOR_CHANNELS];
    ph_compute_moments(ctx->image.raw_rgb, num_pixels, ctx->image.channels, channel_moments);

    for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
        const ph_channel_moments_t m = channel_moments[c];

        /* Signed fixed point, big-endian. The skewness keeps its sign; the static assert
         * on the scale in hashes/hashes.h is what guarantees the clamp below never fires for
         * an 8-bit image, so it is a bound on programmer error rather than on the data. */
        const double moments[PH_COLOR_MOMENTS] = {m.mean, m.std_dev, m.skew};
        for (int k = 0; k < PH_COLOR_MOMENTS; k++) {
            double scaled = round(moments[k] * (double)PH_COLOR_MOMENT_SCALE);
            if (scaled > INT16_MAX) {
                scaled = INT16_MAX;
            } else if (scaled < INT16_MIN) {
                scaled = INT16_MIN;
            }

            /* In int16_t range after the clamp; stored as its two's-complement bits. */
            uint16_t bits = (uint16_t)(int16_t)scaled;
            size_t at = (ph_size(c) * PH_COLOR_MOMENTS + ph_size(k)) * PH_COLOR_MOMENT_BYTES;
            out_digest->data[at + 0] = (uint8_t)(bits >> 8);
            out_digest->data[at + 1] = (uint8_t)(bits & 0xFF);
        }
    }

    return PH_SUCCESS;
}

void ph_compute_moments(const uint8_t *data, size_t num_pixels, int channels,
                        ph_channel_moments_t out[]) {
    memset(out, 0, PH_COLOR_CHANNELS * sizeof(*out));
    if (!data || num_pixels == 0 || channels < PH_COLOR_CHANNELS) {
        return;
    }

    /* `i * ch` in size_t: an int index would overflow well before num_pixels does. */
    const size_t ch = ph_size(channels);
    const double n = (double)num_pixels;

    /* Step 1: the arithmetic mean. The sums are integers: a double accumulating the same
     * bytes one by one would hold every partial sum exactly -- reaching 2^53 takes
     * 2^53 / 255, about 3.5e13 pixels, far past any image that fits in memory -- so it
     * would end on this same value, and the integer sum is cheaper. */
    uint64_t sum[PH_COLOR_CHANNELS] = {0, 0, 0};
    for (size_t i = 0; i < num_pixels; i++) {
        const uint8_t *p = data + i * ch;
        sum[0] += p[0];
        sum[1] += p[1];
        sum[2] += p[2];
    }
    double mean[PH_COLOR_CHANNELS];
    for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
        mean[c] = (double)sum[c] / n;
    }

    /* Step 2: the second and third central moments, all three channels in one pass. Each
     * of the six sums is still added in pixel order, so each is the same double a
     * channel-at-a-time pass produces; the six independent chains just overlap, where one
     * chain per pass would wait out the latency of every addition. */
    double var[PH_COLOR_CHANNELS] = {0, 0, 0}, third[PH_COLOR_CHANNELS] = {0, 0, 0};
    for (size_t i = 0; i < num_pixels; i++) {
        const uint8_t *p = data + i * ch;
        double d0 = p[0] - mean[0];
        double d1 = p[1] - mean[1];
        double d2 = p[2] - mean[2];
        var[0] += d0 * d0;
        third[0] += d0 * d0 * d0;
        var[1] += d1 * d1;
        third[1] += d1 * d1 * d1;
        var[2] += d2 * d2;
        third[2] += d2 * d2 * d2;
    }

    for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
        out[c].mean = mean[c];
        out[c].std_dev = sqrt(var[c] / n);
        out[c].skew = cbrt(third[c] / n);
    }
}
