#include "../internal.h"
#include <stdint.h>
#include <string.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE4_1__)
#include <smmintrin.h>
#endif

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

    size_t saved_offset = ctx->arena.offset;
    uint8_t *temp = ph_get_scratchpad(ctx, sw * sh);
    if (!temp) {
        ph_resize_box(src, sw, sh, dst, dw, dh);
        ctx->arena.offset = saved_offset;
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
    ctx->arena.offset = saved_offset;
}
