#include "../internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_dhash(ph_context_t *ctx, uint64_t *out_hash) {
  if (!ctx || !ctx->is_loaded || !out_hash)
    return PH_ERR_INVALID_ARGUMENT;
  uint8_t gray[72]; // 9x8
  uint8_t *full_gray = malloc(ctx->width * ctx->height);
  if (!full_gray)
    return PH_ERR_ALLOCATION_FAILED;

  ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, full_gray);
  ph_resize_grayscale(full_gray, ctx->width, ctx->height, gray, 9, 8);
  free(full_gray);

  uint64_t hash = 0;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      if (gray[row * 9 + col] < gray[row * 9 + col + 1])
        hash |= (1ULL << (row * 8 + col));
    }
  }
  *out_hash = hash;
  return PH_SUCCESS;
}
