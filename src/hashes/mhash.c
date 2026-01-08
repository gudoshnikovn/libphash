#include "../internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_mhash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;

    // 1. Resize to 16x16 to capture structural edges
    uint8_t tiny[256];
    uint8_t *full_gray = malloc(ctx->width * ctx->height);
    ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, full_gray);
    ph_resize_grayscale(full_gray, ctx->width, ctx->height, tiny, 16, 16);
    free(full_gray);

    // 2. Simple 3x3 Laplacian Kernel for edge detection
    //  0 -1  0
    // -1  4 -1
    //  0 -1  0
    uint64_t hash = 0;
    int bit_idx = 0;
    for (int y = 1; y < 15 && bit_idx < 64; y += 2) {
        for (int x = 1; x < 15 && bit_idx < 64; x += 2) {
            int center = tiny[y * 16 + x] * 4;
            int neighbors = tiny[(y - 1) * 16 + x] + tiny[(y + 1) * 16 + x] +
                            tiny[y * 16 + (x - 1)] + tiny[y * 16 + (x + 1)];
            if (center - neighbors > 0)
                hash |= (1ULL << bit_idx);
            bit_idx++;
        }
    }
    *out_hash = hash;
    return PH_SUCCESS;
}
