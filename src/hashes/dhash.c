#include "../internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_dhash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    uint8_t *gray_full = ph_get_gray(ctx);
    if (!gray_full) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    uint8_t tiny[72];
    ph_resize_bilinear(gray_full, ctx->width, ctx->height, tiny, 9, 8);

    uint64_t hash = 0;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (tiny[row * 9 + col] < tiny[row * 9 + col + 1]) {
                hash |= (1ULL << (row * 8 + col));
            }
        }
    }

    *out_hash = hash;
    return PH_SUCCESS;
}
