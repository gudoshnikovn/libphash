#include "internal.h"
#include <stdint.h>

void ph_to_grayscale(const uint8_t *src, int w, int h, int channels,
                     uint8_t *dst) {
  for (int i = 0; i < w * h; i++) {
    uint8_t r = src[i * channels];
    uint8_t g = src[i * channels + 1];
    uint8_t b = src[i * channels + 2];
    dst[i] = (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
  }
}

void ph_resize_grayscale(const uint8_t *src, int sw, int sh, uint8_t *dst,
                         int dw, int dh) {
  double x_ratio = (double)sw / dw;
  double y_ratio = (double)sh / dh;
  for (int dy = 0; dy < dh; dy++) {
    for (int dx = 0; dx < dw; dx++) {
      int sx_start = (int)(dx * x_ratio), sy_start = (int)(dy * y_ratio);
      int sx_end = (int)((dx + 1) * x_ratio),
          sy_end = (int)((dy + 1) * y_ratio);
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
