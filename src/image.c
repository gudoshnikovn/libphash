#include "internal.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void ph_to_grayscale(const uint8_t *src, int w, int h, int channels, uint8_t *dst) {
    for (int i = 0; i < w * h; i++) {
        uint8_t r = src[i * channels];
        uint8_t g = src[i * channels + 1];
        uint8_t b = src[i * channels + 2];
        dst[i] = (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
    }
}

void ph_resize_grayscale(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    double x_ratio = (double)sw / dw;
    double y_ratio = (double)sh / dh;

    for (int dy = 0; dy < dh; dy++) {
        for (int dx = 0; dx < dw; dx++) {
            int sx_start = (int)(dx * x_ratio);
            int sy_start = (int)(dy * y_ratio);
            int sx_end = (int)((dx + 1) * x_ratio);
            int sy_end = (int)((dy + 1) * y_ratio);

            uint32_t sum = 0;
            int count = 0;

            for (int y = sy_start; y < sy_end && y < sh; y++) {
                for (int x = sx_start; x < sx_end && x < sw; x++) {
                    sum += src[y * sw + x];
                    count++;
                }
            }
            dst[dy * dw + dx] = (count > 0) ? (uint8_t)(sum / count) : 0;
        }
    }
}

void ph_apply_gaussian_blur(const uint8_t *src, int w, int h, uint8_t *dst) {
    int kernel[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    memcpy(dst, src, w * h);

    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int sum = 0;
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    int px = src[(y + ky) * w + (x + kx)];
                    sum += px * kernel[ky + 1][kx + 1];
                }
            }
            dst[y * w + x] = (uint8_t)(sum >> 4);
        }
    }
}

void ph_apply_gamma(const ph_context_t *ctx, uint8_t *data, int w, int h) {
    if (!ctx || !data)
        return;
    // Use the thread-local precomputed LUT
    for (int i = 0; i < w * h; i++) {
        data[i] = ctx->gamma_lut[data[i]];
    }
}
