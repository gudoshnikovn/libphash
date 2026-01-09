#include "internal.h"
#include <math.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/stb_image.h"

PH_API const char *ph_version(void) { return "1.2.0"; }

PH_API void ph_context_set_gamma(ph_context_t *ctx, float gamma) {
    if (!ctx || gamma <= 0.001f)
        return;

    // Precompute LUT for O(1) access during processing
    for (int i = 0; i < 256; i++) {
        double val = i / 255.0;
        // Standard gamma correction: value^(1/gamma)
        double res = pow(val, 1.0 / (double)gamma) * 255.0;
        ctx->gamma_lut[i] = (uint8_t)(res > 255.0 ? 255.0 : res);
    }
}

PH_API ph_error_t ph_create(ph_context_t **out_ctx) {
    if (!out_ctx)
        return PH_ERR_INVALID_ARGUMENT;

    ph_context_t *ctx = (ph_context_t *)calloc(1, sizeof(ph_context_t));
    if (!ctx)
        return PH_ERR_ALLOCATION_FAILED;

    init_dct_matrix();

    ctx->data = NULL;
    ctx->gray_data = NULL;
    ctx->width = 0;
    ctx->height = 0;
    ctx->channels = 0;
    ctx->is_loaded = 0;

    ph_context_set_gamma(ctx, 2.2f);

    *out_ctx = ctx;
    return PH_SUCCESS;
}
PH_API void ph_free(ph_context_t *ctx) {
    if (ctx) {
        if (ctx->data)
            stbi_image_free(ctx->data);
        if (ctx->gray_data)
            free(ctx->gray_data);
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

PH_API ph_error_t ph_load_from_memory(ph_context_t *ctx, const uint8_t *buffer, size_t length) {
    if (!ctx || !buffer || length == 0)
        return PH_ERR_INVALID_ARGUMENT;
    if (ctx->data)
        stbi_image_free(ctx->data);

    ctx->data =
        stbi_load_from_memory(buffer, (int)length, &ctx->width, &ctx->height, &ctx->channels, 0);
    if (!ctx->data)
        return PH_ERR_DECODE_FAILED;

    ctx->is_loaded = 1;
    return PH_SUCCESS;
}
