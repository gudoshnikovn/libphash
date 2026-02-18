#include "../internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_mhash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;

    // 1. Resize to 16x16 to capture structural edges
    uint8_t block_data[PH_BLOCK_SIZE * PH_BLOCK_SIZE];
    if (!PH_SAFE_ALLOC_SIZE(ctx->width, ctx->height))
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t *full_gray = malloc(ctx->width * ctx->height);
    if (!full_gray)
        return PH_ERR_ALLOCATION_FAILED;
    ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, full_gray);
    ph_resize_box(full_gray, ctx->width, ctx->height, block_data, PH_BLOCK_SIZE, PH_BLOCK_SIZE);
    free(full_gray);

    // 2. Simple 3x3 Laplacian Kernel for edge detection
    //  0 -1  0
    // -1  4 -1
    //  0 -1  0
    uint64_t hash = 0;
    int bit_idx = 0;
    for (int y = 1; y < PH_BLOCK_SIZE - 1 && bit_idx < 64; y += 2) {
        for (int x = 1; x < PH_BLOCK_SIZE - 1 && bit_idx < 64; x += 2) {
            int center = block_data[y * PH_BLOCK_SIZE + x] * 4;
            int neighbors = block_data[(y - 1) * PH_BLOCK_SIZE + x] + block_data[(y + 1) * PH_BLOCK_SIZE + x] +
                            block_data[y * PH_BLOCK_SIZE + (x - 1)] + block_data[y * PH_BLOCK_SIZE + (x + 1)];
            if (center - neighbors > 0)
                hash |= (1ULL << bit_idx);
            bit_idx++;
        }
    }
    *out_hash = hash;
    return PH_SUCCESS;
}
