#include "internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE4_1__)
#include <smmintrin.h>
#endif

uint8_t *ph_get_gray(ph_context_t *ctx) {
    if (ctx->image.channels == 1) {
        return (uint8_t *)ctx->image.raw_rgb;
    }
    if (!ctx->image.gray_cache && ctx->image.raw_rgb) {
        if (!PH_SAFE_ALLOC_SIZE(ctx->image.width, ctx->image.height)) {
            return NULL;
        }
        ctx->image.gray_cache = malloc(ctx->image.width * ctx->image.height);
        if (ctx->image.gray_cache) {
            ph_to_grayscale(ctx, ctx->image.raw_rgb, ctx->image.width, ctx->image.height,
                            ctx->image.channels, ctx->image.gray_cache);
        }
    }
    return ctx->image.gray_cache;
}

void ph_to_grayscale(const ph_context_t *ctx, const uint8_t *src, int w, int h, int channels,
                     uint8_t *dst) {
    int num_pixels = w * h;
    const uint8_t *s = src;
    uint8_t *d = dst;
    int i = 0;

    int r_w = ctx ? ctx->config.gray_r : PH_GRAY_R;
    int g_w = ctx ? ctx->config.gray_g : PH_GRAY_G;
    int b_w = ctx ? ctx->config.gray_b : PH_GRAY_B;

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

void ph_apply_gamma(const ph_context_t *ctx, uint8_t *data, int w, int h) {
    if (!ctx || !data)
        return;
    // Use the thread-local precomputed LUT
    for (int i = 0; i < w * h; i++) {
        data[i] = ctx->config.gamma_lut[data[i]];
    }
}
