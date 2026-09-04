#include "internal.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE4_1__)
#include <smmintrin.h>
#endif

void ph_apply_gaussian_blur(ph_context_t *ctx, uint8_t *src, int w, int h, uint8_t *dst) {
    /* size_t, not int: w * h overflows int above ~46340x46340, which would both
     * truncate the memcpy() length and mis-size the scratchpad (R03/H6). */
    size_t nbytes = (w > 0 && h > 0) ? (size_t)w * (size_t)h : 0;

    if (!ctx || !src || !dst || w < 3 || h < 3) {
        if (dst && src && dst != src && nbytes > 0)
            memcpy(dst, src, nbytes);
        return;
    }

    size_t saved_offset = ctx->arena.offset;
    uint8_t *temp = ph_get_scratchpad(ctx, nbytes);
    if (!temp) {
        if (dst != src)
            memcpy(dst, src, nbytes);
        ctx->arena.offset = saved_offset;
        return;
    }

#if defined(__ARM_NEON)
    // --- NEON Implementation ---
    // Kernel: [1, 2, 1] / 4

    // Horizontal Pass: src -> temp
    for (int y = 0; y < h; y++) {
        const uint8_t *row_src = &src[y * w];
        uint8_t *row_dst = &temp[y * w];

        // Edges (Scalar)
        row_dst[0] = row_src[0];
        row_dst[w - 1] = row_src[w - 1];

        int x = 1;
        // Process 16 pixels at a time
        for (; x <= w - 1 - 16; x += 16) {
            uint8x16_t p_left = vld1q_u8(&row_src[x - 1]);
            uint8x16_t p_curr = vld1q_u8(&row_src[x]);
            uint8x16_t p_right = vld1q_u8(&row_src[x + 1]);

            // val = left + 2*curr + right
            // We need 16-bit intermediate to avoid overflow before shift (max 255*4 = 1020)
            uint16x8_t low_l = vmovl_u8(vget_low_u8(p_left));
            uint16x8_t low_c = vmovl_u8(vget_low_u8(p_curr));
            uint16x8_t low_r = vmovl_u8(vget_low_u8(p_right));

            uint16x8_t high_l = vmovl_u8(vget_high_u8(p_left));
            uint16x8_t high_c = vmovl_u8(vget_high_u8(p_curr));
            uint16x8_t high_r = vmovl_u8(vget_high_u8(p_right));

            // Calculate Sum
            // Horizontal: left + 2*center + right
            uint16x8_t sum_low = vaddq_u16(low_l, low_r);
            sum_low = vmlaq_n_u16(sum_low, low_c, 2);

            uint16x8_t sum_high = vaddq_u16(high_l, high_r);
            sum_high = vmlaq_n_u16(sum_high, high_c, 2);

            // Shift right by 2 (divide by 4) and narrow back to 8-bit
            // vshrn_n_u16 essentially does: (val >> 2) & 0xFF
            uint8x8_t res_low = vshrn_n_u16(sum_low, 2);
            uint8x8_t res_high = vshrn_n_u16(sum_high, 2);

            vst1q_u8(&row_dst[x], vcombine_u8(res_low, res_high));
        }

        // Cleanup tail (Scalar)
        for (; x < w - 1; x++) {
            uint32_t val = row_src[x - 1] + (row_src[x] << 1) + row_src[x + 1];
            row_dst[x] = (uint8_t)(val >> 2);
        }
    }

    // Vertical Pass: temp -> dst
    // Kernel [1, 2, 1] / 4 across rows
    // To vectorize, we load vectors from row-1, row, row+1

    // Top Edge (copy first row)
    memcpy(dst, temp, w);

    for (int y = 1; y < h - 1; y++) {
        const uint8_t *row_prev = &temp[(y - 1) * w];
        const uint8_t *row_curr = &temp[y * w];
        const uint8_t *row_next = &temp[(y + 1) * w];
        uint8_t *row_dst = &dst[y * w];

        int x = 0;
        for (; x <= w - 16; x += 16) {
            uint8x16_t p_prev = vld1q_u8(&row_prev[x]);
            uint8x16_t p_curr = vld1q_u8(&row_curr[x]);
            uint8x16_t p_next = vld1q_u8(&row_next[x]);

            uint16x8_t low_p = vmovl_u8(vget_low_u8(p_prev));
            uint16x8_t low_c = vmovl_u8(vget_low_u8(p_curr));
            uint16x8_t low_n = vmovl_u8(vget_low_u8(p_next));

            uint16x8_t high_p = vmovl_u8(vget_high_u8(p_prev));
            uint16x8_t high_c = vmovl_u8(vget_high_u8(p_curr));
            uint16x8_t high_n = vmovl_u8(vget_high_u8(p_next));

            uint16x8_t sum_low = vaddq_u16(low_p, low_n);
            sum_low = vmlaq_n_u16(sum_low, low_c, 2);

            uint16x8_t sum_high = vaddq_u16(high_p, high_n);
            sum_high = vmlaq_n_u16(sum_high, high_c, 2);

            uint8x8_t res_low = vshrn_n_u16(sum_low, 2);
            uint8x8_t res_high = vshrn_n_u16(sum_high, 2);

            vst1q_u8(&row_dst[x], vcombine_u8(res_low, res_high));
        }

        // Cleanup tail
        for (; x < w; x++) {
            uint32_t val = row_prev[x] + (row_curr[x] << 1) + row_next[x];
            row_dst[x] = (uint8_t)(val >> 2);
        }
    }

    // Bottom Edge (copy last row)
    memcpy(&dst[(h - 1) * w], &temp[(h - 1) * w], w);

#else
    // --- Scalar Implementation (Original Fallback) ---

    /* Horizontal pass: Kernel [1 2 1], divide by 4 */
    for (int y = 0; y < h; y++) {
        temp[y * w] = src[y * w];
        temp[y * w + w - 1] = src[y * w + w - 1];
        for (int x = 1; x < w - 1; x++) {
            uint32_t val = src[y * w + (x - 1)] + (src[y * w + x] << 1) + src[y * w + (x + 1)];
            temp[y * w + x] = (uint8_t)(val >> 2);
        }
    }

    /* Vertical pass: Kernel [1 2 1], divide by 4 */
    for (int x = 0; x < w; x++) {
        dst[x] = temp[x];
        dst[(h - 1) * w + x] = temp[(h - 1) * w + x];
        for (int y = 1; y < h - 1; y++) {
            uint32_t val = temp[(y - 1) * w + x] + (temp[y * w + x] << 1) + temp[(y + 1) * w + x];
            dst[y * w + x] = (uint8_t)(val >> 2);
        }
    }
#endif
    ctx->arena.offset = saved_offset;
}

void ph_apply_laplacian_3x3(const uint8_t *src, int w, int h, uint8_t *dst) {
    if (!src || !dst || w <= 0 || h <= 0)
        return;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
                dst[y * w + x] = src[y * w + x];
            } else {
                int val = 5 * src[y * w + x] - src[(y - 1) * w + x] - src[(y + 1) * w + x] -
                          src[y * w + (x - 1)] - src[y * w + (x + 1)];
                if (val < 0)
                    val = 0;
                if (val > 255)
                    val = 255;
                dst[y * w + x] = (uint8_t)val;
            }
        }
    }
}

/* Separable Gaussian blur at an arbitrary sigma, 8-bit in and out.
 *
 * ph_apply_gaussian_blur() above is a fixed 3x3 kernel and cannot express a sigma; the
 * Marr-Hildreth hash needs the sigma its source specifies. The kernel is truncated at
 * three standard deviations, where the tail it drops is under 0.3% of the mass, and
 * renormalised so the sum is exactly one and a flat image stays flat. Edges clamp.
 *
 * `scratch` holds w*h floats for the intermediate horizontal pass and is the caller's:
 * this function does no allocation of its own. */
void ph_gaussian_blur_sigma(const uint8_t *src, int w, int h, float sigma, float *scratch,
                            uint8_t *dst) {
    if (!src || !dst || !scratch || w <= 0 || h <= 0 || !(sigma > 0.0f))
        return;

    int radius = (int)ceilf(3.0f * sigma);
    if (radius < 1)
        radius = 1;
    if (radius > 64)
        radius = 64;

    float kernel[129];
    float sum = 0.0f;
    for (int i = -radius; i <= radius; i++) {
        float v = expf(-(float)(i * i) / (2.0f * sigma * sigma));
        kernel[i + radius] = v;
        sum += v;
    }
    for (int i = 0; i <= 2 * radius; i++)
        kernel[i] /= sum;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; k++) {
                int sx = x + k;
                if (sx < 0)
                    sx = 0;
                if (sx >= w)
                    sx = w - 1;
                acc += kernel[k + radius] * (float)src[(size_t)y * w + sx];
            }
            scratch[(size_t)y * w + x] = acc;
        }
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; k++) {
                int sy = y + k;
                if (sy < 0)
                    sy = 0;
                if (sy >= h)
                    sy = h - 1;
                acc += kernel[k + radius] * scratch[(size_t)sy * w + x];
            }
            int v = (int)(acc + 0.5f);
            dst[(size_t)y * w + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }
}

/* Histogram equalisation over `levels` buckets, in place.
 *
 * The textbook transform: build the histogram, walk its cumulative sum, and map each
 * value onto the level its rank falls in. Used by the Marr-Hildreth hash, whose source
 * equalises over 256 levels before filtering so that the response depends on the
 * distribution of tones rather than on the exposure. */
void ph_equalize_histogram(uint8_t *data, size_t n, int levels) {
    if (!data || n == 0 || levels < 2 || levels > 256)
        return;

    size_t histogram[256] = {0};
    for (size_t i = 0; i < n; i++)
        histogram[data[i]]++;

    /* The first non-empty bucket maps to 0, so a low-contrast image is stretched rather
     * than merely shifted. */
    size_t cdf_min = 0;
    for (int v = 0; v < 256; v++) {
        if (histogram[v] != 0) {
            cdf_min = histogram[v];
            break;
        }
    }

    uint8_t map[256];
    size_t cdf = 0;
    double denom = (double)(n - cdf_min);
    for (int v = 0; v < 256; v++) {
        cdf += histogram[v];
        double t = denom > 0.0 ? ((double)cdf - (double)cdf_min) / denom : 0.0;
        if (t < 0.0)
            t = 0.0;
        int mapped = (int)(t * (double)(levels - 1) + 0.5);
        if (mapped < 0)
            mapped = 0;
        if (mapped > levels - 1)
            mapped = levels - 1;
        /* Spread the `levels` buckets back over the full byte range. */
        map[v] = (uint8_t)((mapped * 255) / (levels - 1));
    }

    for (size_t i = 0; i < n; i++)
        data[i] = map[data[i]];
}
