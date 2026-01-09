#include "internal.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint8_t *ph_get_gray(ph_context_t *ctx) {
    if (!ctx->gray_data && ctx->data) {
        ctx->gray_data = malloc(ctx->width * ctx->height);
        if (ctx->gray_data) {
            ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, ctx->gray_data);
        }
    }
    return ctx->gray_data;
}

void ph_to_grayscale(const uint8_t *src, int w, int h, int channels, uint8_t *dst) {
    for (int i = 0; i < w * h; i++) {
        uint32_t r = src[i * channels];
        uint32_t g = src[i * channels + 1];
        uint32_t b = src[i * channels + 2];
        dst[i] = (uint8_t)((r * 38 + g * 75 + b * 15) >> 7);
    }
}
void ph_resize_bilinear(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    double x_ratio = (dw > 1) ? (double)(sw - 1) / (dw - 1) : 0;
    double y_ratio = (dh > 1) ? (double)(sh - 1) / (dh - 1) : 0;

    for (int i = 0; i < dh; i++) {
        for (int j = 0; j < dw; j++) {
            double x_pos = x_ratio * j;
            double y_pos = y_ratio * i;
            int x = (int)x_pos;
            int y = (int)y_pos;

            double x_diff = x_pos - x;
            double y_diff = y_pos - y;

            int index = y * sw + x;

            int next_x = (x < sw - 1) ? 1 : 0;
            int next_y = (y < sh - 1) ? sw : 0;

            uint8_t a = src[index];
            uint8_t b = src[index + next_x];
            uint8_t c = src[index + next_y];
            uint8_t d = src[index + next_y + next_x];

            dst[i * dw + j] =
                (uint8_t)(a * (1 - x_diff) * (1 - y_diff) + b * (x_diff) * (1 - y_diff) +
                          c * (y_diff) * (1 - x_diff) + d * (x_diff * y_diff));
        }
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
