#include "../internal.h"
#include <stdlib.h>
#include <string.h>

PH_API ph_error_t ph_digest_create(size_t bits, ph_digest_t **out_digest) {
  if (!out_digest || bits == 0) {
    return PH_ERR_INVALID_ARGUMENT;
  }

  ph_digest_t *d = malloc(sizeof(ph_digest_t));
  if (!d) {
    return PH_ERR_ALLOCATION_FAILED;
  }

  /* Calculate byte size rounded up to the nearest byte */
  size_t byte_size = (bits + 7) / 8;
  d->data = calloc(1, byte_size);
  if (!d->data) {
    free(d);
    return PH_ERR_ALLOCATION_FAILED;
  }

  d->bits = bits;
  *out_digest = d;
  return PH_SUCCESS;
}

PH_API void ph_digest_free(ph_digest_t *digest) {
  if (digest) {
    free(digest->data);
    free(digest);
  }
}

PH_API ph_error_t ph_compute_bmh(ph_context_t *ctx, ph_digest_t *out_digest) {
  if (!ctx || !ctx->is_loaded || !out_digest || out_digest->bits != 256) {
    return PH_ERR_INVALID_ARGUMENT;
  }

  /* BMH for 256 bits requires a 16x16 pixel grid */
  uint8_t pixels[256];
  uint8_t *full_gray = malloc(ctx->width * ctx->height);
  if (!full_gray) {
    return PH_ERR_ALLOCATION_FAILED;
  }

  /* Step 1: Pre-process image to grayscale and resize to 16x16 */
  ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, full_gray);
  ph_resize_grayscale(full_gray, ctx->width, ctx->height, pixels, 16, 16);
  free(full_gray);

  /* Step 2: Calculate the average pixel intensity */
  uint64_t total_sum = 0;
  for (int i = 0; i < 256; i++) {
    total_sum += pixels[i];
  }
  uint8_t avg = (uint8_t)(total_sum / 256);

  /* Step 3: Threshold pixels against the average and pack into the digest */
  for (int i = 0; i < 256; i++) {
    if (pixels[i] >= avg) {
      out_digest->data[i / 8] |= (1 << (i % 8));
    }
  }

  return PH_SUCCESS;
}
