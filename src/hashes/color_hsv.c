#include "internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MIN3(a, b, c) ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))
#define MAX3(a, b, c) ((a) > (b) ? ((a) > (c) ? (a) : (c)) : ((b) > (c) ? (b) : (c)))

PH_API ph_error_t ph_compute_color_hash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->image.is_loaded || !out_hash) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    int total_pixels = ctx->image.width * ctx->image.height;
    int count_black = 0;
    int count_gray = 0;
    int faint_counts[6] = {0};
    int bright_counts[6] = {0};
    int count_colors = 0;

    const uint8_t *src = ctx->image.raw_rgb;
    int channels = ctx->image.channels;

    for (int i = 0; i < total_pixels; i++) {
        float r = src[i * channels];
        float g = (channels >= 3) ? src[i * channels + 1] : r;
        float b = (channels >= 3) ? src[i * channels + 2] : r;

        ph_hsv_result_t res = ph_hsv_classify_pixel(r, g, b);

        switch (res.category) {
            case PH_HSV_BLACK:
                count_black++;
                break;
            case PH_HSV_GRAY:
                count_gray++;
                break;
            case PH_HSV_FAINT:
                count_colors++;
                faint_counts[res.hue_bin]++;
                break;
            case PH_HSV_BRIGHT:
                count_colors++;
                bright_counts[res.hue_bin]++;
                break;
        }
    }

    double c_denom = (count_colors > 0) ? count_colors : 1.0;
    double values[14];
    values[0] = (double)count_black / total_pixels;
    values[1] = (double)count_gray / total_pixels;

    for (int i = 0; i < 6; i++) {
        values[2 + i] = (double)faint_counts[i] / c_denom;
        values[8 + i] = (double)bright_counts[i] / c_denom;
    }

    *out_hash = ph_pack_3bit_values(values, 14);
    return PH_SUCCESS;
}

ph_hsv_result_t ph_hsv_classify_pixel(float r, float g, float b) {
    ph_hsv_result_t res = {PH_HSV_BLACK, 0};

    // Intensity (PIL L conversion)
    float intensity = (r * 299.0f + g * 587.0f + b * 114.0f) / 1000.0f;

    if (intensity < 32.0f) {
        res.category = PH_HSV_BLACK;
        return res;
    }

    float maxc = MAX3(r, g, b);
    float minc = MIN3(r, g, b);
    float s = 0.0f;
    float h = 0.0f;

    if (maxc != minc) {
        float d = maxc - minc;
        s = 255.0f * d / maxc;
        if (r == maxc) {
            h = (g - b) / d;
        } else if (g == maxc) {
            h = 2.0f + (b - r) / d;
        } else {
            h = 4.0f + (r - g) / d;
        }
        h *= 42.5f;
        if (h < 0.0f) {
            h += 255.0f;
        }
    }

    if (s < 85.0f) {
        res.category = PH_HSV_GRAY;
        return res;
    }

    int hue_bin = (int)(h / 42.5f);
    if (hue_bin > 5)
        hue_bin = 5;
    if (hue_bin < 0)
        hue_bin = 0;
    res.hue_bin = hue_bin;

    if (s < 170.0f) {
        res.category = PH_HSV_FAINT;
    } else {
        res.category = PH_HSV_BRIGHT;
    }

    return res;
}

uint64_t ph_pack_3bit_values(const double *values, int n) {
    uint64_t hash = 0;
    int binbits = 3;
    int maxvalue = 1 << binbits; // 8
    int shift = 0;

    for (int i = 0; i < n; i++) {
        int v = (int)(values[i] * maxvalue);
        if (v > maxvalue - 1)
            v = maxvalue - 1;
        if (v < 0)
            v = 0;

        for (int b = 0; b < binbits; b++) {
            int m = binbits - b - 1;
            int bit = (v >> m) & 1;
            if (bit) {
                // MSB is the first bit added (i=0, b=0)
                int bit_idx = 41 - shift;
                if (bit_idx >= 0) {
                    hash |= (1ULL << bit_idx);
                }
            }
            shift++;
        }
    }
    return hash;
}
