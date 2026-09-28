#include "internal.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE4_1__)
#include <smmintrin.h>
#endif

/* 3x3 Gaussian blur ([1,2,1]/4 separable kernel), scalar reference implementation.
 * Shared by ph_apply_gaussian_blur()'s non-NEON build and by
 * ph_apply_gaussian_blur_scalar(), which forces this path even when NEON is available
 * so tests/src/test_simd_equivalence.c can compare the two byte for byte. */
static void gaussian_blur_scalar_impl(const uint8_t *src, int w, int h, uint8_t *temp,
                                      uint8_t *dst) {
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
}

#if defined(__ARM_NEON)
/* Same contract as gaussian_blur_scalar_impl(), NEON fast path. */
static void gaussian_blur_neon_impl(const uint8_t *src, int w, int h, uint8_t *temp, uint8_t *dst) {
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
}
#endif

/* Shared body of ph_apply_gaussian_blur() and ph_apply_gaussian_blur_scalar(): validation,
 * scratchpad allocation, then dispatch to the scalar or (when available and not forced off)
 * NEON kernel. */
static int gaussian_blur_impl(ph_context_t *ctx, uint8_t *src, int w, int h, uint8_t *dst,
                              bool force_scalar) {
    /* size_t, not int: w * h overflows int above ~46340x46340, which would both
     * truncate the memcpy() length and mis-size the scratchpad. */
    size_t nbytes = (w > 0 && h > 0) ? (size_t)w * (size_t)h : 0;

    if (!ctx || !src || !dst || w < 3 || h < 3) {
        if (dst && src && dst != src && nbytes > 0)
            memcpy(dst, src, nbytes);
        return 1;
    }

    ph_arena_mark_t arena_mark = ph_arena_mark(ctx);
    uint8_t *temp = ph_get_scratchpad(ctx, nbytes);
    if (!temp) {
        /* Allocation failure, not the legitimate small-image passthrough above: the
         * caller must be told rather than silently getting the unblurred image back. */
        ph_arena_release(ctx, arena_mark);
        return 0;
    }

#if defined(__ARM_NEON)
    if (force_scalar)
        gaussian_blur_scalar_impl(src, w, h, temp, dst);
    else
        gaussian_blur_neon_impl(src, w, h, temp, dst);
#else
    (void)force_scalar;
    gaussian_blur_scalar_impl(src, w, h, temp, dst);
#endif

    ph_arena_release(ctx, arena_mark);
    return 1;
}

int ph_apply_gaussian_blur(ph_context_t *ctx, uint8_t *src, int w, int h, uint8_t *dst) {
    return gaussian_blur_impl(ctx, src, w, h, dst, false);
}

int ph_apply_gaussian_blur_scalar(ph_context_t *ctx, uint8_t *src, int w, int h, uint8_t *dst) {
    return gaussian_blur_impl(ctx, src, w, h, dst, true);
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
 * this function does no allocation of its own.
 *
 * Every output sample is the sum kernel[0]*s[-r] + kernel[1]*s[-r+1] + ... taken in that
 * order, whichever loop computes it. Both passes keep that order and vectorise across
 * neighbouring outputs instead of across taps, so the result is bit-for-bit the one the
 * direct clamped loop gives; summing the taps in any other order would move the last bit
 * of some samples and, through rounding to 8 bits, the hashes built on them.
 *
 * Neither pass clamps inside its interior. The horizontal pass clamps only the `radius`
 * columns at each end of a row; the vertical pass clamps only the `radius` rows at the
 * top and bottom, and does it once per row rather than once per sample. Everything else
 * is a straight run of multiply-adds over contiguous memory, PH_BLUR_LANES outputs at a
 * time so that many independent sums hide the latency of each floating-point add. The
 * vertical pass walks the image in column strips of PH_BLUR_STRIP floats, so the
 * 2*radius+1 rows one output row reads stay in cache for the next. */
#define PH_BLUR_MAX_RADIUS 64
#define PH_BLUR_STRIP 256
#define PH_BLUR_LANES 32

static float blur_clamped_tap_sum(const uint8_t *row, int w, int x, const float *kernel,
                                  int radius) {
    float acc = 0.0f;
    for (int k = -radius; k <= radius; k++) {
        int sx = x + k;
        if (sx < 0)
            sx = 0;
        if (sx >= w)
            sx = w - 1;
        acc += kernel[k + radius] * (float)row[sx];
    }
    return acc;
}

/* out[i] = sum over t of kernel[t] * src[t * stride + i], for i in [0, n), t in order.
 * PH_BLUR_LANES outputs at a time stay in registers across all taps; the tail that does
 * not fill a block takes the same sum one output at a time. */
static void blur_taps(const float *restrict src, size_t stride, const float *kernel, int taps,
                      int n, float *restrict out) {
    int i = 0;
    for (; i + PH_BLUR_LANES <= n; i += PH_BLUR_LANES) {
        float acc[PH_BLUR_LANES] = {0.0f};
        for (int t = 0; t < taps; t++) {
            const float kt = kernel[t];
            const float *restrict s = src + (size_t)t * stride + i;
            for (int j = 0; j < PH_BLUR_LANES; j++)
                acc[j] += kt * s[j];
        }
        for (int j = 0; j < PH_BLUR_LANES; j++)
            out[i + j] = acc[j];
    }
    for (; i < n; i++) {
        float acc = 0.0f;
        for (int t = 0; t < taps; t++)
            acc += kernel[t] * src[(size_t)t * stride + i];
        out[i] = acc;
    }
}

static void blur_row_horizontal(const uint8_t *restrict row, int w, const float *kernel, int radius,
                                float *restrict out) {
    /* Columns [lo, hi) never reach past either end of the row. */
    int lo = radius < w ? radius : w;
    int hi = w - radius > lo ? w - radius : lo;

    for (int x = 0; x < lo; x++)
        out[x] = blur_clamped_tap_sum(row, w, x, kernel, radius);
    for (int x = hi; x < w; x++)
        out[x] = blur_clamped_tap_sum(row, w, x, kernel, radius);

    /* The interior in strips: each strip's source span is widened to float once, rather
     * than converting every sample once per tap. */
    float in[PH_BLUR_STRIP + 2 * PH_BLUR_MAX_RADIUS];
    for (int x0 = lo; x0 < hi; x0 += PH_BLUR_STRIP) {
        int n = hi - x0 < PH_BLUR_STRIP ? hi - x0 : PH_BLUR_STRIP;
        const uint8_t *s = row + x0 - radius;
        for (int i = 0; i < n + 2 * radius; i++)
            in[i] = (float)s[i];
        /* Stride 1: tap t reads in[t + i]. */
        blur_taps(in, 1, kernel, 2 * radius + 1, n, out + x0);
    }
}

void ph_gaussian_blur_sigma(const uint8_t *src, int w, int h, float sigma, float *scratch,
                            uint8_t *dst) {
    if (!src || !dst || !scratch || w <= 0 || h <= 0 || !(sigma > 0.0f))
        return;

    int radius = (int)ceilf(3.0f * sigma);
    if (radius < 1)
        radius = 1;
    if (radius > PH_BLUR_MAX_RADIUS)
        radius = PH_BLUR_MAX_RADIUS;

    float kernel[2 * PH_BLUR_MAX_RADIUS + 1];
    float sum = 0.0f;
    for (int i = -radius; i <= radius; i++) {
        float v = expf(-(float)(i * i) / (2.0f * sigma * sigma));
        kernel[i + radius] = v;
        sum += v;
    }
    for (int i = 0; i <= 2 * radius; i++)
        kernel[i] /= sum;

    for (int y = 0; y < h; y++)
        blur_row_horizontal(src + (size_t)y * w, w, kernel, radius, scratch + (size_t)y * w);

    /* Rows [radius, h - radius) read 2*radius+1 consecutive rows of `scratch` with no
     * clamping; the rows within `radius` of either edge gather their clamped rows into
     * `rows` first. */
    const float *rows[2 * PH_BLUR_MAX_RADIUS + 1];
    float acc[PH_BLUR_STRIP];
    for (int x0 = 0; x0 < w; x0 += PH_BLUR_STRIP) {
        int n = w - x0 < PH_BLUR_STRIP ? w - x0 : PH_BLUR_STRIP;
        for (int y = 0; y < h; y++) {
            if (y >= radius && y + radius < h) {
                blur_taps(scratch + (size_t)(y - radius) * w + x0, (size_t)w, kernel,
                          2 * radius + 1, n, acc);
            } else {
                for (int k = -radius; k <= radius; k++) {
                    int sy = y + k;
                    if (sy < 0)
                        sy = 0;
                    if (sy >= h)
                        sy = h - 1;
                    rows[k + radius] = scratch + (size_t)sy * w + x0;
                }
                for (int i = 0; i < n; i++) {
                    float a = 0.0f;
                    for (int t = 0; t <= 2 * radius; t++)
                        a += kernel[t] * rows[t][i];
                    acc[i] = a;
                }
            }
            uint8_t *d = dst + (size_t)y * w + x0;
            for (int i = 0; i < n; i++) {
                int v = (int)(acc[i] + 0.5f);
                d[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
            }
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
