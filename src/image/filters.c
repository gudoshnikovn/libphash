#include "image/image.h"
#include "safety.h"

#include <math.h>
#include <stdint.h>

/* Separable Gaussian blur at an arbitrary sigma, 8-bit in and out.
 *
 * mHash and Radial blur at the sigma their sources specify. The kernel is truncated at
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
#define PH_BLUR_STRIP      256
#define PH_BLUR_LANES      32

static float blur_clamped_tap_sum(const uint8_t *row, int w, int x, const float *kernel,
                                  int radius) {
    float acc = 0.0f;
    for (int k = -radius; k <= radius; k++) {
        int sx = x + k;
        if (sx < 0) {
            sx = 0;
        }
        if (sx >= w) {
            sx = w - 1;
        }
        acc += kernel[k + radius] * (float)row[sx];
    }
    return acc;
}

/* out[i] = sum over t of kernel[t] * src[t * stride + i], for i in [0, n), t in order.
 * PH_BLUR_LANES outputs at a time stay in registers across all taps; the tail that does
 * not fill a block takes the same sum one output at a time. */
static void blur_taps(const float *restrict src, size_t stride, const float *kernel, size_t taps,
                      size_t n, float *restrict out) {
    size_t i = 0;
    for (; i + PH_BLUR_LANES <= n; i += PH_BLUR_LANES) {
        float acc[PH_BLUR_LANES] = {0.0f};
        for (size_t t = 0; t < taps; t++) {
            const float kt = kernel[t];
            const float *restrict s = src + t * stride + i;
            for (size_t j = 0; j < PH_BLUR_LANES; j++) {
                acc[j] += kt * s[j];
            }
        }
        for (size_t j = 0; j < PH_BLUR_LANES; j++) {
            out[i + j] = acc[j];
        }
    }
    for (; i < n; i++) {
        float acc = 0.0f;
        for (size_t t = 0; t < taps; t++) {
            acc += kernel[t] * src[t * stride + i];
        }
        out[i] = acc;
    }
}

static void blur_row_horizontal(const uint8_t *restrict row, int w, const float *kernel, int radius,
                                float *restrict out) {
    /* Columns [lo, hi) never reach past either end of the row. */
    int lo = radius < w ? radius : w;
    int hi = w - radius > lo ? w - radius : lo;

    for (int x = 0; x < lo; x++) {
        out[x] = blur_clamped_tap_sum(row, w, x, kernel, radius);
    }
    for (int x = hi; x < w; x++) {
        out[x] = blur_clamped_tap_sum(row, w, x, kernel, radius);
    }

    /* The interior in strips: each strip's source span is widened to float once, rather
     * than converting every sample once per tap. */
    float in[PH_BLUR_STRIP + 2 * PH_BLUR_MAX_RADIUS];
    for (int x0 = lo; x0 < hi; x0 += PH_BLUR_STRIP) {
        int n = hi - x0 < PH_BLUR_STRIP ? hi - x0 : PH_BLUR_STRIP;
        const uint8_t *s = row + x0 - radius;
        for (int i = 0; i < n + 2 * radius; i++) {
            in[i] = (float)s[i];
        }
        /* Stride 1: tap t reads in[t + i]. */
        blur_taps(in, 1, kernel, ph_size(2 * radius + 1), ph_size(n), out + x0);
    }
}

void ph_gaussian_blur_sigma(const uint8_t *src, int w, int h, float sigma, float *scratch,
                            uint8_t *dst) {
    if (!src || !dst || !scratch || w <= 0 || h <= 0 || !(sigma > 0.0f)) {
        return;
    }

    int radius = (int)ceilf(3.0f * sigma);
    if (radius < 1) {
        radius = 1;
    }
    if (radius > PH_BLUR_MAX_RADIUS) {
        radius = PH_BLUR_MAX_RADIUS;
    }

    float kernel[2 * PH_BLUR_MAX_RADIUS + 1];
    float sum = 0.0f;
    for (int i = -radius; i <= radius; i++) {
        float v = expf(-(float)(i * i) / (2.0f * sigma * sigma));
        kernel[i + radius] = v;
        sum += v;
    }
    for (int i = 0; i <= 2 * radius; i++) {
        kernel[i] /= sum;
    }

    const size_t width = ph_size(w);
    for (int y = 0; y < h; y++) {
        blur_row_horizontal(src + ph_size(y) * width, w, kernel, radius,
                            scratch + ph_size(y) * width);
    }

    /* Rows [radius, h - radius) read 2*radius+1 consecutive rows of `scratch` with no
     * clamping; the rows within `radius` of either edge gather their clamped rows into
     * `rows` first. */
    const float *rows[2 * PH_BLUR_MAX_RADIUS + 1];
    float acc[PH_BLUR_STRIP];
    for (int x0 = 0; x0 < w; x0 += PH_BLUR_STRIP) {
        int n = w - x0 < PH_BLUR_STRIP ? w - x0 : PH_BLUR_STRIP;
        for (int y = 0; y < h; y++) {
            if (y >= radius && y + radius < h) {
                blur_taps(scratch + ph_size(y - radius) * width + x0, width, kernel,
                          ph_size(2 * radius + 1), ph_size(n), acc);
            } else {
                for (int k = -radius; k <= radius; k++) {
                    int sy = y + k;
                    if (sy < 0) {
                        sy = 0;
                    }
                    if (sy >= h) {
                        sy = h - 1;
                    }
                    rows[k + radius] = scratch + ph_size(sy) * width + x0;
                }
                for (int i = 0; i < n; i++) {
                    float a = 0.0f;
                    for (int t = 0; t <= 2 * radius; t++) {
                        a += kernel[t] * rows[t][i];
                    }
                    acc[i] = a;
                }
            }
            uint8_t *d = dst + ph_size(y) * width + x0;
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
    if (!data || n == 0 || levels < 2 || levels > 256) {
        return;
    }

    size_t histogram[256] = {0};
    for (size_t i = 0; i < n; i++) {
        histogram[data[i]]++;
    }

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
        if (t < 0.0) {
            t = 0.0;
        }
        int mapped = (int)(t * (double)(levels - 1) + 0.5);
        if (mapped < 0) {
            mapped = 0;
        }
        if (mapped > levels - 1) {
            mapped = levels - 1;
        }
        /* Spread the `levels` buckets back over the full byte range. */
        map[v] = (uint8_t)((mapped * 255) / (levels - 1));
    }

    for (size_t i = 0; i < n; i++) {
        data[i] = map[data[i]];
    }
}
