#include "../internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_ahash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    uint8_t *gray_full = ph_get_gray(ctx);
    if (!gray_full) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    uint8_t tiny[64];
    ph_resize_bilinear(gray_full, ctx->width, ctx->height, tiny, 8, 8);

    uint64_t total_sum = 0;
    for (int i = 0; i < 64; i++) {
        total_sum += tiny[i];
    }
    uint8_t avg = (uint8_t)(total_sum / 64);

    uint64_t hash = 0;
    for (int i = 0; i < 64; i++) {
        if (tiny[i] >= avg) {
            hash |= (1ULL << i);
        }
    }

    *out_hash = hash;
    return PH_SUCCESS;
}
