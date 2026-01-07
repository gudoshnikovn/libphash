#include "../internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_ahash(ph_context_t *ctx, uint64_t *out_hash) {
  if (!ctx || !ctx->is_loaded || !out_hash)
    return PH_ERR_INVALID_ARGUMENT;
  uint8_t gray[64];
  uint8_t *full_gray = malloc(ctx->width * ctx->height);
  if (!full_gray)
    return PH_ERR_ALLOCATION_FAILED;

  ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, full_gray);
  ph_resize_grayscale(full_gray, ctx->width, ctx->height, gray, 8, 8);
  free(full_gray);

  uint64_t avg = 0;
  for (int i = 0; i < 64; i++)
    avg += gray[i];
  avg /= 64;

  uint64_t hash = 0;
  for (int i = 0; i < 64; i++) {
    if (gray[i] >= avg)
      hash |= (1ULL << i);
  }
  *out_hash = hash;
  return PH_SUCCESS;
}
