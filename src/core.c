#include "internal.h"
#include <math.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/stb_image.h"

PH_API const char *ph_version(void) { return "1.7.0"; }

PH_API void ph_context_set_gamma(ph_context_t *ctx, float gamma) {
    if (!ctx || gamma <= PH_GAMMA_EPSILON)
        return;

    ctx->gamma = gamma;
    // Precompute LUT for O(1) access during processing
    for (int i = 0; i < 256; i++) {
        double val = i / 255.0;
        // Standard gamma correction: value^(1/gamma)
        double res = pow(val, 1.0 / (double)gamma) * 255.0;
        ctx->gamma_lut[i] = (uint8_t)(res > 255.0 ? 255.0 : res);
    }
}

PH_API void ph_context_set_gray_weights(ph_context_t *ctx, int r, int g, int b) {
    if (!ctx)
        return;

    int sum = r + g + b;
    if (sum <= 0) {
        // Fallback to defaults if invalid
        ctx->gray_r = PH_GRAY_R;
        ctx->gray_g = PH_GRAY_G;
        ctx->gray_b = PH_GRAY_B;
        return;
    }

    // Normalize to sum 128 for the >> 7 shift
    ctx->gray_r = (r * 128) / sum;
    ctx->gray_g = (g * 128) / sum;
    ctx->gray_b = 128 - ctx->gray_r - ctx->gray_g;
}

PH_API void ph_context_set_phash_params(ph_context_t *ctx, int dct_size, int reduction_size) {
    if (!ctx || dct_size <= 0 || reduction_size <= 0 || reduction_size > dct_size)
        return;
    ctx->phash_dct_size = dct_size;
    ctx->phash_reduction_size = reduction_size;
}

PH_API void ph_context_set_radial_params(ph_context_t *ctx, int projections, int samples) {
    if (!ctx || projections <= 0 || samples <= 0)
        return;
    ctx->radial_projections = projections;
    ctx->radial_samples = samples;
}

PH_API void ph_context_set_block_params(ph_context_t *ctx, int block_size) {
    if (!ctx || block_size <= 0)
        return;
    ctx->block_size = block_size;
}

PH_API void ph_context_set_load_grayscale(ph_context_t *ctx, int enable) {
    if (ctx) {
        ctx->load_grayscale = enable;
    }
}

PH_API ph_error_t ph_create(ph_context_t **out_ctx) {
    if (!out_ctx)
        return PH_ERR_INVALID_ARGUMENT;

    ph_context_t *ctx = (ph_context_t *)calloc(1, sizeof(ph_context_t));
    if (!ctx)
        return PH_ERR_ALLOCATION_FAILED;

    ctx->data = NULL;
    ctx->gray_data = NULL;
    ctx->width = 0;
    ctx->height = 0;
    ctx->channels = 0;
    ctx->is_loaded = 0;

    /* Defaults */
    ctx->gray_r = PH_GRAY_R;
    ctx->gray_g = PH_GRAY_G;
    ctx->gray_b = PH_GRAY_B;
    ctx->phash_dct_size = PH_DCT_SIZE;
    ctx->phash_reduction_size = PH_DCT_REDUCTION_SIZE;
    ctx->radial_projections = PH_RADIAL_PROJECTIONS;
    ctx->radial_samples = PH_RADIAL_SAMPLES;
    ctx->block_size = PH_BLOCK_SIZE;

    /* Optimization Defaults: Disabled by default for compatibility */
    ctx->load_grayscale = 0;

    ph_context_set_gamma(ctx, PH_DEFAULT_GAMMA);

    *out_ctx = ctx;
    return PH_SUCCESS;
}
PH_API void ph_free(ph_context_t *ctx) {
    if (ctx) {
        if (ctx->data)
            stbi_image_free(ctx->data);
        if (ctx->gray_data)
            free(ctx->gray_data);
        if (ctx->scratchpad)
            free(ctx->scratchpad);
        free(ctx);
    }
}

uint8_t *ph_get_scratchpad(ph_context_t *ctx, size_t size) {
    if (!ctx || size == 0)
        return NULL;

    if (ctx->scratchpad_size < size) {
        uint8_t *new_ptr = (uint8_t *)realloc(ctx->scratchpad, size);
        if (!new_ptr)
            return NULL;
        ctx->scratchpad = new_ptr;
        ctx->scratchpad_size = size;
    }
    return ctx->scratchpad;
}

PH_API ph_error_t ph_load_from_file(ph_context_t *ctx, const char *filepath) {
    if (!ctx || !filepath)
        return PH_ERR_INVALID_ARGUMENT;
    if (ctx->data)
        stbi_image_free(ctx->data);
    if (ctx->gray_data) {
        free(ctx->gray_data);
        ctx->gray_data = NULL;
    }

    int req_comp = ctx->load_grayscale ? 1 : 0;
    ctx->data = stbi_load(filepath, &ctx->width, &ctx->height, &ctx->channels, req_comp);
    if (!ctx->data)
        return PH_ERR_DECODE_FAILED;

    // If we requested specific channels, update the struct to reflect that
    if (req_comp != 0) {
        ctx->channels = req_comp;
    }

    ctx->is_loaded = 1;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_load_from_memory(ph_context_t *ctx, const uint8_t *buffer, size_t length) {
    if (!ctx || !buffer || length == 0)
        return PH_ERR_INVALID_ARGUMENT;
    if (ctx->data)
        stbi_image_free(ctx->data);
    if (ctx->gray_data) {
        free(ctx->gray_data);
        ctx->gray_data = NULL;
    }

    int req_comp = ctx->load_grayscale ? 1 : 0;
    ctx->data =
        stbi_load_from_memory(buffer, (int)length, &ctx->width, &ctx->height, &ctx->channels, req_comp);
    if (!ctx->data)
        return PH_ERR_DECODE_FAILED;

    if (req_comp != 0) {
        ctx->channels = req_comp;
    }

    ctx->is_loaded = 1;
    return PH_SUCCESS;
}
