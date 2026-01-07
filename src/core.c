#include "internal.h"
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/stb_image.h"

PH_API ph_error_t ph_create(ph_context_t **out_ctx) {
  if (!out_ctx)
    return PH_ERR_INVALID_ARGUMENT;
  ph_context_t *ctx = (ph_context_t *)calloc(1, sizeof(ph_context_t));
  if (!ctx)
    return PH_ERR_ALLOCATION_FAILED;
  *out_ctx = ctx;
  return PH_SUCCESS;
}

PH_API void ph_free(ph_context_t *ctx) {
  if (ctx) {
    if (ctx->data)
      stbi_image_free(ctx->data);
    free(ctx);
  }
}

PH_API ph_error_t ph_load_from_file(ph_context_t *ctx, const char *filepath) {
  if (!ctx || !filepath)
    return PH_ERR_INVALID_ARGUMENT;
  if (ctx->data)
    stbi_image_free(ctx->data);
  ctx->data = stbi_load(filepath, &ctx->width, &ctx->height, &ctx->channels, 0);
  if (!ctx->data)
    return PH_ERR_DECODE_FAILED;
  ctx->is_loaded = 1;
  return PH_SUCCESS;
}

PH_API ph_error_t ph_load_from_memory(ph_context_t *ctx, const uint8_t *buffer,
                                      size_t length) {
  if (!ctx || !buffer || length == 0)
    return PH_ERR_INVALID_ARGUMENT;
  if (ctx->data)
    stbi_image_free(ctx->data);
  ctx->data = stbi_load_from_memory(buffer, (int)length, &ctx->width,
                                    &ctx->height, &ctx->channels, 0);
  if (!ctx->data)
    return PH_ERR_DECODE_FAILED;
  ctx->is_loaded = 1;
  return PH_SUCCESS;
}
