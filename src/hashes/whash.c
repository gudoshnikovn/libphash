#include "../internal.h"
#include <stdlib.h>

/* Simple Haar Wavelet Transform on a 1D array */
static void haar_1d(double *data, int n) {
  double temp[64]; // For 8x8
  int h = n / 2;
  for (int i = 0; i < h; i++) {
    temp[i] = (data[2 * i] + data[2 * i + 1]) / 1.414;
    temp[i + h] = (data[2 * i] - data[2 * i + 1]) / 1.414;
  }
  for (int i = 0; i < n; i++)
    data[i] = temp[i];
}

PH_API ph_error_t ph_compute_whash(ph_context_t *ctx, uint64_t *out_hash) {
  if (!ctx || !ctx->is_loaded || !out_hash)
    return PH_ERR_INVALID_ARGUMENT;

  uint8_t gray[64];
  uint8_t *full_gray = malloc(ctx->width * ctx->height);
  if (!full_gray)
    return PH_ERR_ALLOCATION_FAILED;

  ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, full_gray);
  ph_resize_grayscale(full_gray, ctx->width, ctx->height, gray, 8, 8);
  free(full_gray);

  double d_data[64];
  for (int i = 0; i < 64; i++)
    d_data[i] = (double)gray[i];

  // 2D Haar Transform
  for (int i = 0; i < 8; i++)
    haar_1d(&d_data[i * 8], 8); // Rows
  for (int j = 0; j < 8; j++) { // Cols
    double col[8];
    for (int i = 0; i < 8; i++)
      col[i] = d_data[i * 8 + j];
    haar_1d(col, 8);
    for (int i = 0; i < 8; i++)
      d_data[i * 8 + j] = col[i];
  }

  // The top-left value is the DC component. We use the average of the
  // LL (Low-Low) sub-band to threshold.
  double sum = 0;
  for (int i = 0; i < 64; i++)
    sum += d_data[i];
  double avg = sum / 64.0;

  uint64_t hash = 0;
  for (int i = 0; i < 64; i++) {
    if (d_data[i] > avg)
      hash |= (1ULL << i);
  }

  *out_hash = hash;
  return PH_SUCCESS;
}
