#ifndef PH_INTERNAL_H
#define PH_INTERNAL_H

#include "../include/libphash.h"
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct ph_context {
  uint8_t *data;
  int width, height, channels;
  int is_loaded;
};

// Internal image processing
void ph_to_grayscale(const uint8_t *src, int w, int h, int channels,
                     uint8_t *dst);
void ph_resize_grayscale(const uint8_t *src, int sw, int sh, uint8_t *dst,
                         int dw, int dh);

#endif
