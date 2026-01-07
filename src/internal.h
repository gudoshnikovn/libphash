#ifndef INTERNAL_H
#define INTERNAL_H

#include "../include/libphash.h"
#include <stdint.h>

/*
 * Internal Image Processing Helpers
 */

/* Converts RGB/RGBA to Grayscale */
void ph_to_grayscale(const uint8_t *src, int w, int h, int channels,
                     uint8_t *dst);

/* Resizes a grayscale image */
void ph_resize_grayscale(const uint8_t *src, int sw, int sh, uint8_t *dst,
                         int dw, int dh);

/*
 * New Helpers for Radial Hash
 */

/* Applies a 3x3 Gaussian Blur to reduce noise */
void ph_apply_gaussian_blur(const uint8_t *src, int w, int h, uint8_t *dst);

/* Applies Gamma Correction (gamma=2.2) to normalize brightness */
void ph_apply_gamma(uint8_t *data, int w, int h);

/* Internal Context Structure */
struct ph_context {
  uint8_t *data; /* Raw image data (stb_image) */
  int width;
  int height;
  int channels;
  int is_loaded;
};

#endif /* INTERNAL_H */
