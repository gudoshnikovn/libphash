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
        /* One byte per pixel. The size must be computed in size_t: an int product
         * wraps above ~46340x46340 and would hand malloc() a bogus (often negative,
         * i.e. huge after conversion) size while ph_to_grayscale() still writes
         * w * h bytes -- a heap overflow (R03/H6). */
        size_t gray_size;
        if (!ph_safe_image_alloc_size((uint64_t)ctx->image.width, (uint64_t)ctx->image.height, 1,
                                      &gray_size)) {
            return NULL;
        }
        ctx->image.gray_cache = malloc(gray_size);
        if (ctx->image.gray_cache) {
            ph_to_grayscale(ctx, ctx->image.raw_rgb, ctx->image.width, ctx->image.height,
                            ctx->image.channels, ctx->image.gray_cache);
        }
    }
    return ctx->image.gray_cache;
}

void ph_to_grayscale(const ph_context_t *ctx, const uint8_t *src, int w, int h, int channels,
                     uint8_t *dst) {
    if (w <= 0 || h <= 0)
        return;
    /* size_t, not int: w * h overflows int above ~46340x46340 (R03/H6). */
    size_t num_pixels = (size_t)w * (size_t)h;
    const uint8_t *s = src;
    uint8_t *d = dst;
    size_t i = 0;

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

        for (; i + 8 <= num_pixels; i += 8) {
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

        for (; i + 8 <= num_pixels; i += 8) {
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
    if (!ctx || !data || w <= 0 || h <= 0)
        return;
    // Use the thread-local precomputed LUT. size_t: w * h overflows int (R03/H6).
    size_t num_pixels = (size_t)w * (size_t)h;
    for (size_t i = 0; i < num_pixels; i++) {
        data[i] = ctx->config.gamma_lut[data[i]];
    }
}
