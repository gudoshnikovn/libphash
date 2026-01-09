#ifndef INTERNAL_H
#define INTERNAL_H

#include "../include/libphash.h"
#include <stdint.h>

/*
 * Internal Image Processing Helpers
 */

/* Converts RGB/RGBA to Grayscale */
void ph_to_grayscale(const uint8_t *src, int w, int h, int channels, uint8_t *dst);

/* Resizes a grayscale image */
void ph_resize_grayscale(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

/* Applies a 3x3 Gaussian Blur to reduce noise */
void ph_apply_gaussian_blur(const uint8_t *src, int w, int h, uint8_t *dst);

/* Applies Gamma Correction (gamma=2.2) to normalize brightness */
void ph_apply_gamma(const ph_context_t *ctx, uint8_t *data, int w, int h);

void ph_resize_bilinear(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

uint8_t *ph_get_gray(ph_context_t *ctx);

/* Internal Context Structure */
struct ph_context {
    uint8_t *data;
    uint8_t *gray_data;
    int width;
    int height;
    int channels;
    int is_loaded;

    uint8_t gamma_lut[256];
};

#endif /* INTERNAL_H */
