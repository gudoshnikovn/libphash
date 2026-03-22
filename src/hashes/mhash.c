#include "../internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_mhash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->image.is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;

    int scale_size = 18; // Fixed size to yield exactly 64 bits (8x8) using 3x3 kernel and step=2

    uint8_t *full_gray = ph_get_gray(ctx);
    if (!full_gray)
        return PH_ERR_ALLOCATION_FAILED;

    size_t saved_offset = ctx->arena.offset;
    uint8_t *scratch = ph_get_scratchpad(ctx, (size_t)scale_size * scale_size);
    if (!scratch)
        return PH_ERR_ALLOCATION_FAILED;
    uint8_t *block_data = scratch;

    ph_resize_box(full_gray, ctx->image.width, ctx->image.height, block_data, scale_size,
                  scale_size);

    // 2. Simple 3x3 Laplacian Kernel for edge detection
    uint64_t hash = 0;
    int bit_idx = 0;

    int step = 2;
    for (int y = 1; y < scale_size - 1 && bit_idx < 64; y += step) {
        for (int x = 1; x < scale_size - 1 && bit_idx < 64; x += step) {
            int center = block_data[y * scale_size + x] * 4;
            int neighbors =
                block_data[(y - 1) * scale_size + x] + block_data[(y + 1) * scale_size + x] +
                block_data[y * scale_size + (x - 1)] + block_data[y * scale_size + (x + 1)];
            if (center - neighbors > 0)
                hash |= (1ULL << bit_idx);
            bit_idx++;
        }
    }
    *out_hash = hash;
    ctx->arena.offset = saved_offset;
    return PH_SUCCESS;
}
