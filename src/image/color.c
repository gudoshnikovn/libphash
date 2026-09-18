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

/* Scalar tail shared by ph_to_grayscale() (after its SIMD prefix, if any) and
 * ph_to_grayscale_scalar() (the whole buffer, for the SIMD-equivalence test in
 * tests/src/test_simd_equivalence.c). */
static void grayscale_scalar_range(const uint8_t *s, uint8_t *d, size_t count, int channels,
                                   int r_w, int g_w, int b_w) {
    for (size_t i = 0; i < count; i++) {
        uint32_t r = s[0];
        uint32_t g = s[1];
        uint32_t b = s[2];
        *d++ = (uint8_t)((r * r_w + g * g_w + b * b_w) >> 7);
        s += channels;
    }
}

void ph_to_grayscale_scalar(const ph_context_t *ctx, const uint8_t *src, int w, int h, int channels,
                            uint8_t *dst) {
    if (w <= 0 || h <= 0)
        return;
    size_t num_pixels = (size_t)w * (size_t)h;

    int r_w = ctx ? ctx->config.gray_r : PH_GRAY_R;
    int g_w = ctx ? ctx->config.gray_g : PH_GRAY_G;
    int b_w = ctx ? ctx->config.gray_b : PH_GRAY_B;

    if (channels == 1) {
        memcpy(dst, src, num_pixels);
        return;
    }

    grayscale_scalar_range(src, dst, num_pixels, channels, r_w, g_w, b_w);
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
    grayscale_scalar_range(s, d, num_pixels - i, channels, r_w, g_w, b_w);
}

/* Applied from exactly one place: ph_compute_radial_hash() (src/hashes/radial.c).
 * The setting lives on the context and reads as general preprocessing, but no other
 * algorithm touches it -- see the warning on ph_context_set_gamma().
 *
 * R52: normalises the buffer by its own maximum before the power step and rescales by
 * that same maximum after -- `out = (in / max)^gamma * max` -- the way pHash's own
 * radial digest does, rather than treating every buffer as if it already spanned the
 * full 0..255 range. This is what makes gamma == 1.0 an exact identity for any image,
 * not only ones that happen to touch 255: (v/max)^1 * max == v algebraically, for any
 * max > 0. It also means the LUT can no longer be precomputed once per context -- it
 * depends on this call's own buffer, not only on gamma -- so it is rebuilt here, once
 * per call over at most 256 entries, not once per pixel. */
void ph_apply_gamma(const ph_context_t *ctx, uint8_t *data, int w, int h) {
    if (!ctx || !data || w <= 0 || h <= 0)
        return;
    // size_t: w * h overflows int (R03/H6).
    size_t num_pixels = (size_t)w * (size_t)h;

    // gamma == 1.0 (the default) is an identity transform regardless of the buffer's
    // content -- skip the scan and the LUT build entirely, the common case by far.
    if (ctx->config.gamma == 1.0f)
        return;

    uint8_t max_val = 0;
    for (size_t i = 0; i < num_pixels; i++)
        if (data[i] > max_val)
            max_val = data[i];
    if (max_val == 0)
        return; // an all-black buffer has nothing to normalise by; already all zero

    uint8_t lut[256];
    double gamma = (double)ctx->config.gamma;
    double max_d = (double)max_val;
    for (int i = 0; i <= (int)max_val; i++) {
        double normalized = (double)i / max_d;
        double res = pow(normalized, gamma) * max_d;
        lut[i] = (uint8_t)(res < 0.0 ? 0.0 : res > 255.0 ? 255.0 : res);
    }
    for (size_t i = 0; i < num_pixels; i++)
        data[i] = lut[data[i]];
}
