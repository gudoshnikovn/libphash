#include "internal.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE4_1__)
#include <smmintrin.h>
#endif

uint8_t *ph_get_gray(ph_context_t *ctx) {
    if (ctx->channels == 1) {
        return (uint8_t *)ctx->data;
    }
    if (!ctx->gray_data && ctx->data) {
        if (!PH_SAFE_ALLOC_SIZE(ctx->width, ctx->height)) {
            return NULL;
        }
        ctx->gray_data = malloc(ctx->width * ctx->height);
        if (ctx->gray_data) {
            ph_to_grayscale(ctx, ctx->data, ctx->width, ctx->height, ctx->channels, ctx->gray_data);
        }
    }
    return ctx->gray_data;
}

void ph_to_grayscale(const ph_context_t *ctx, const uint8_t *src, int w, int h, int channels,
                     uint8_t *dst) {
    int num_pixels = w * h;
    const uint8_t *s = src;
    uint8_t *d = dst;
    int i = 0;

    int r_w = ctx ? ctx->gray_r : PH_GRAY_R;
    int g_w = ctx ? ctx->gray_g : PH_GRAY_G;
    int b_w = ctx ? ctx->gray_b : PH_GRAY_B;

    // Check for 1-channel input (already grayscale)
    if (channels == 1) {
        memcpy(dst, src, num_pixels);
        return;
    }

#if defined(__ARM_NEON)
    if (channels == 3) {
        uint8x8_t r_weight = vdup_n_u8((uint8_t)r_w);
        uint8x8_t g_weight = vdup_n_u8((uint8_t)g_w);
        uint8x8_t b_weight = vdup_n_u8((uint8_t)b_w);

        for (; i <= num_pixels - 8; i += 8) {
            uint8x8x3_t rgb = vld3_u8(s);
            uint16x8_t gray = vmull_u8(rgb.val[0], r_weight);
            gray = vmlal_u8(gray, rgb.val[1], g_weight);
            gray = vmlal_u8(gray, rgb.val[2], b_weight);
            uint8x8_t res = vshrn_n_u16(gray, 7);
            vst1_u8(d, res);
            s += 3 * 8;
            d += 8;
        }
    } else if (channels == 4) {
        uint8x8_t r_weight = vdup_n_u8((uint8_t)r_w);
        uint8x8_t g_weight = vdup_n_u8((uint8_t)g_w);
        uint8x8_t b_weight = vdup_n_u8((uint8_t)b_w);

        for (; i <= num_pixels - 8; i += 8) {
            uint8x8x4_t rgba = vld4_u8(s);
            uint16x8_t gray = vmull_u8(rgba.val[0], r_weight);
            gray = vmlal_u8(gray, rgba.val[1], g_weight);
            gray = vmlal_u8(gray, rgba.val[2], b_weight);
            uint8x8_t res = vshrn_n_u16(gray, 7);
            vst1_u8(d, res);
            s += 4 * 8;
            d += 8;
        }
    }
#endif

    /* Fallback for remaining pixels or other architectures */
    for (; i < num_pixels; i++) {
        uint32_t r = s[0];
        uint32_t g = s[1];
        uint32_t b = s[2];
        *d++ = (uint8_t)((r * r_w + g * g_w + b * b_w) >> 7);
        s += channels;
    }
}
void ph_resize_bilinear(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    if (dw <= 0 || dh <= 0)
        return;

    /* 16.16 fixed point for ratios and positions */
    uint32_t x_ratio = (dw > 1) ? ((uint32_t)(sw - 1) << 16) / (dw - 1) : 0;
    uint32_t y_ratio = (dh > 1) ? ((uint32_t)(sh - 1) << 16) / (dh - 1) : 0;

    for (int i = 0; i < dh; i++) {
        uint32_t y_pos = i * y_ratio;
        uint16_t y = (uint16_t)(y_pos >> 16);
        uint16_t y_diff = (y_pos >> 8) & 0xFF; // 8-bit fraction
        uint16_t y_inv = 256 - y_diff;

        for (int j = 0; j < dw; j++) {
            uint32_t x_pos = j * x_ratio;
            uint16_t x = (uint16_t)(x_pos >> 16);
            uint16_t x_diff = (x_pos >> 8) & 0xFF; // 8-bit fraction
            uint16_t x_inv = 256 - x_diff;

            int index = y * sw + x;
            int next_x = (x < sw - 1) ? 1 : 0;
            int next_y = (y < sh - 1) ? sw : 0;

            uint8_t a = src[index];
            uint8_t b = src[index + next_x];
            uint8_t c = src[index + next_y];
            uint8_t d = src[index + next_y + next_x];

            /* (a * (1-x_d)(1-y_d) + b * x_d(1-y_d) + c * y_d(1-x_d) + d * x_d*y_d) */
            uint32_t val = (uint32_t)a * x_inv * y_inv + (uint32_t)b * x_diff * y_inv +
                           (uint32_t)c * y_diff * x_inv + (uint32_t)d * x_diff * y_diff;

            dst[i * dw + j] = (uint8_t)(val >> 16);
        }
    }
}
void ph_resize_box(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;

    /* 16.16 fixed point for ratios */
    uint32_t x_ratio = ((uint32_t)sw << 16) / dw;
    uint32_t y_ratio = ((uint32_t)sh << 16) / dh;

    for (int dy = 0; dy < dh; dy++) {
        uint32_t sy_start = (dy * y_ratio) >> 16;
        uint32_t sy_end = ((dy + 1) * y_ratio) >> 16;
        if (sy_end > (uint32_t)sh)
            sy_end = sh;
        if (sy_start >= sy_end && sy_start < (uint32_t)sh)
            sy_end = sy_start + 1;

        for (int dx = 0; dx < dw; dx++) {
            uint32_t sx_start = (dx * x_ratio) >> 16;
            uint32_t sx_end = ((dx + 1) * x_ratio) >> 16;
            if (sx_end > (uint32_t)sw)
                sx_end = sw;
            if (sx_start >= sx_end && sx_start < (uint32_t)sw)
                sx_end = sx_start + 1;

            uint32_t sum = 0;
            uint32_t count = (sy_end - sy_start) * (sx_end - sx_start);

            if (count == 0) {
                dst[dy * dw + dx] = 0;
                continue;
            }

            for (uint32_t y = sy_start; y < sy_end; y++) {
                const uint8_t *row = &src[y * sw];
                uint32_t x = sx_start;

#if defined(__ARM_NEON)
                uint32x4_t v_sum = vdupq_n_u32(0);

                // Process 16 pixels at a time
                for (; x + 16 <= sx_end; x += 16) {
                    uint8x16_t val = vld1q_u8(&row[x]);

                    // u8 -> u16
                    uint16x8_t low = vmovl_u8(vget_low_u8(val));
                    uint16x8_t high = vmovl_u8(vget_high_u8(val));

                    // u16 -> u32 accumulator
                    v_sum = vaddw_u16(v_sum, vget_low_u16(low));
                    v_sum = vaddw_u16(v_sum, vget_high_u16(low));
                    v_sum = vaddw_u16(v_sum, vget_low_u16(high));
                    v_sum = vaddw_u16(v_sum, vget_high_u16(high));
                }

                // Horizontal reduce
                sum += vaddvq_u32(v_sum);
#endif

                // Scalar fallback / tail
                for (; x < sx_end; x++) {
                    sum += row[x];
                }
            }
            dst[dy * dw + dx] = (uint8_t)(sum / count);
        }
    }
}

void ph_resize_mipmap(ph_context_t *ctx, const uint8_t *src, int sw, int sh, uint8_t *dst, int dw,
                      int dh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;

    if (sw <= dw * 2 || sh <= dh * 2) {
        ph_resize_bilinear(src, sw, sh, dst, dw, dh);
        return;
    }

    uint8_t *temp = ph_get_scratchpad(ctx, sw * sh);
    if (!temp) {
        ph_resize_box(src, sw, sh, dst, dw, dh);
        return;
    }

    if (src != temp)
        memcpy(temp, src, sw * sh);

    int cur_w = sw;
    int cur_h = sh;

    while (cur_w > dw * 2 && cur_h > dh * 2) {
        int next_w = cur_w / 2;
        int next_h = cur_h / 2;

        for (int y = 0; y < next_h; y++) {
            for (int x = 0; x < next_w; x++) {
                int p1 = temp[(y * 2) * cur_w + (x * 2)];
                int p2 = temp[(y * 2) * cur_w + (x * 2 + 1)];
                int p3 = temp[(y * 2 + 1) * cur_w + (x * 2)];
                int p4 = temp[(y * 2 + 1) * cur_w + (x * 2 + 1)];
                temp[y * next_w + x] = (p1 + p2 + p3 + p4) / 4;
            }
        }
        cur_w = next_w;
        cur_h = next_h;
    }

    ph_resize_bilinear(temp, cur_w, cur_h, dst, dw, dh);
}

void ph_apply_gaussian_blur(ph_context_t *ctx, uint8_t *src, int w, int h, uint8_t *dst) {
    if (!ctx || w < 3 || h < 3) {
        if (dst != src)
            memcpy(dst, src, w * h);
        return;
    }

    uint8_t *temp = ph_get_scratchpad(ctx, w * h);
    if (!temp) {
        if (dst != src)
            memcpy(dst, src, w * h);
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
}

void ph_apply_gamma(const ph_context_t *ctx, uint8_t *data, int w, int h) {
    if (!ctx || !data)
        return;
    // Use the thread-local precomputed LUT
    for (int i = 0; i < w * h; i++) {
        data[i] = ctx->gamma_lut[data[i]];
    }
}
