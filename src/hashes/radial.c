#include "../internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RADIAL_PROJECTIONS 40
#define SAMPLES_PER_LINE 128 /* Increased for better precision */

/**
 * Helper: Bilinear Interpolation
 */
static double get_pixel_bilinear(const uint8_t *img, int w, int h, double x,
                                 double y) {
  if (x < 0 || x >= w - 1 || y < 0 || y >= h - 1)
    return 0.0;

  int x1 = (int)x;
  int y1 = (int)y;
  int x2 = x1 + 1;
  int y2 = y1 + 1;

  double dx = x - x1;
  double dy = y - y1;

  double p1 = img[y1 * w + x1];
  double p2 = img[y1 * w + x2];
  double p3 = img[y2 * w + x1];
  double p4 = img[y2 * w + x2];

  return p1 * (1.0 - dx) * (1.0 - dy) + p2 * dx * (1.0 - dy) +
         p3 * (1.0 - dx) * dy + p4 * dx * dy;
}

PH_API ph_error_t ph_compute_radial_hash(ph_context_t *ctx,
                                         ph_digest_t *out_digest) {
  if (!ctx || !ctx->is_loaded || !out_digest || out_digest->bits < 320)
    return PH_ERR_INVALID_ARGUMENT;

  size_t img_size = ctx->width * ctx->height;
  uint8_t *gray = malloc(img_size);
  uint8_t *blurred = malloc(img_size);

  if (!gray || !blurred) {
    free(gray);
    free(blurred);
    return PH_ERR_ALLOCATION_FAILED;
  }

  /* 1. Preprocessing Pipeline (as per Algorithm) */
  /* Convert to Grayscale */
  ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, gray);

  /* Apply Gaussian Blur (Noise suppression) */
  ph_apply_gaussian_blur(gray, ctx->width, ctx->height, blurred);

  /* Apply Gamma Correction (Lighting normalization) */
  ph_apply_gamma(blurred, ctx->width, ctx->height);

  /* 2. Compute Radial Variance */
  double centerX = ctx->width / 2.0;
  double centerY = ctx->height / 2.0;
  double min_side = (ctx->width < ctx->height) ? ctx->width : ctx->height;
  double maxRadius = min_side / 2.0; /* Full radius */

  double variances[RADIAL_PROJECTIONS];
  double max_var = 0.0;

  /*
   * Scan 0 to 180 degrees (PI).
   * The Radon transform is symmetric, so 0-180 covers all unique line
   * orientations.
   */
  for (int i = 0; i < RADIAL_PROJECTIONS; i++) {
    double theta = (i * M_PI) / RADIAL_PROJECTIONS;
    double cos_t = cos(theta);
    double sin_t = sin(theta);

    double sum = 0.0;
    double sumSq = 0.0;
    int count = 0;

    /* Integrate along the full line (Diameter), from -R to +R */
    for (int r = -SAMPLES_PER_LINE / 2; r < SAMPLES_PER_LINE / 2; r++) {
      double dist = (r * maxRadius) / (SAMPLES_PER_LINE / 2.0);

      double px = centerX + dist * cos_t;
      double py = centerY + dist * sin_t;

      double val = get_pixel_bilinear(blurred, ctx->width, ctx->height, px, py);

      /* Only count pixels strictly inside the image circle */
      if (val > 0.0) {
        sum += val;
        sumSq += val * val;
        count++;
      }
    }

    if (count > 0) {
      double mean = sum / count;
      variances[i] = (sumSq / count) - (mean * mean);
    } else {
      variances[i] = 0.0;
    }

    if (variances[i] > max_var)
      max_var = variances[i];
  }

  /* 3. Normalize and Pack */
  /* We map the variance (0..max_var) to (0..255) */
  for (int i = 0; i < RADIAL_PROJECTIONS; i++) {
    if (max_var > 0.001) {
      /* Sqrt helps to compress the dynamic range of variance */
      out_digest->data[i] = (uint8_t)(sqrt(variances[i] / max_var) * 255.0);
    } else {
      out_digest->data[i] = 0;
    }
  }

  free(gray);
  free(blurred);
  return PH_SUCCESS;
}
