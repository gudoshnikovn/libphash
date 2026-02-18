#include "../internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_mhash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;

    int block_size = ctx->block_size;
    size_t gray_size = (size_t)ctx->width * ctx->height;
    if (!PH_SAFE_ALLOC_SIZE(ctx->width, ctx->height))
        return PH_ERR_ALLOCATION_FAILED;

    /* Use scratchpad for:
     * 1. full_gray: img_size
     * 2. block_data: block_size * block_size
     */
    uint8_t *scratch = ph_get_scratchpad(ctx, gray_size + (size_t)block_size * block_size);
    if (!scratch)
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t *full_gray = scratch;
    uint8_t *block_data = scratch + gray_size;

    ph_to_grayscale(ctx, ctx->data, ctx->width, ctx->height, ctx->channels, full_gray);
    ph_resize_box(full_gray, ctx->width, ctx->height, block_data, block_size, block_size);

    // 2. Simple 3x3 Laplacian Kernel for edge detection
    uint64_t hash = 0;
    int bit_idx = 0;
    
    /* We sample the edges. For 16x16 block, we had a 7x7 grid (step=2).
     * To fit into uint64_t, we need at most 64 bits (8x8).
     */
    int step = (block_size > 8) ? 2 : 1;
    for (int y = 1; y < block_size - 1 && bit_idx < 64; y += step) {
        for (int x = 1; x < block_size - 1 && bit_idx < 64; x += step) {
            int center = block_data[y * block_size + x] * 4;
            int neighbors = block_data[(y - 1) * block_size + x] + block_data[(y + 1) * block_size + x] +
                            block_data[y * block_size + (x - 1)] + block_data[y * block_size + (x + 1)];
            if (center - neighbors > 0)
                hash |= (1ULL << bit_idx);
            bit_idx++;
        }
    }
    *out_hash = hash;
    return PH_SUCCESS;
}
