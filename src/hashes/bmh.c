#include "../internal.h"
#include <stdlib.h>
#include <string.h>

PH_API ph_error_t ph_compute_bmh(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->is_loaded || !out_digest) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    // Clear digest and set size (256 bits = 32 bytes)
    memset(out_digest, 0, sizeof(ph_digest_t));
    out_digest->size = 32;

    int total_pixels = PH_BLOCK_SIZE * PH_BLOCK_SIZE;
    uint8_t block_data[PH_BLOCK_SIZE * PH_BLOCK_SIZE];
    if (!PH_SAFE_ALLOC_SIZE(ctx->width, ctx->height))
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t *full_gray = malloc(ctx->width * ctx->height);
    if (!full_gray)
        return PH_ERR_ALLOCATION_FAILED;

    ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, full_gray);
    ph_resize_box(full_gray, ctx->width, ctx->height, block_data, PH_BLOCK_SIZE, PH_BLOCK_SIZE);
    free(full_gray);

    uint64_t total_sum = 0;
    for (int i = 0; i < total_pixels; i++) {
        total_sum += block_data[i];
    }
    uint8_t avg = (uint8_t)(total_sum / total_pixels);

    for (int i = 0; i < total_pixels; i++) {
        if (block_data[i] >= avg) {
            out_digest->data[i / 8] |= (1 << (i % 8));
        }
    }

    return PH_SUCCESS;
}
