#include "../internal.h"
#include <stdint.h>
#include <string.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE4_1__)
#include <smmintrin.h>
#endif

void ph_apply_gaussian_blur(ph_context_t *ctx, uint8_t *src, int w, int h, uint8_t *dst) {
    if (!ctx || w < 3 || h < 3) {
        if (dst != src)
            memcpy(dst, src, w * h);
        return;
    }

    size_t saved_offset = ctx->arena.offset;
    uint8_t *temp = ph_get_scratchpad(ctx, w * h);
    if (!temp) {
        if (dst != src)
            memcpy(dst, src, w * h);
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
