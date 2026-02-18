#include "../internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_dhash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    size_t gray_size = (size_t)ctx->width * ctx->height;
    uint8_t *gray_full = ph_get_scratchpad(ctx, gray_size);
    if (!gray_full) {
        return PH_ERR_ALLOCATION_FAILED;
    }
    ph_to_grayscale(ctx, ctx->data, ctx->width, ctx->height, ctx->channels, gray_full);

    uint8_t hash_input[(PH_CORE_HASH_SIZE + 1) * PH_CORE_HASH_SIZE];
    ph_resize_bilinear(gray_full, ctx->width, ctx->height, hash_input, PH_CORE_HASH_SIZE + 1,
                       PH_CORE_HASH_SIZE);

    uint64_t hash = 0;
    for (int row = 0; row < PH_CORE_HASH_SIZE; row++) {
        for (int col = 0; col < PH_CORE_HASH_SIZE; col++) {
            if (hash_input[row * (PH_CORE_HASH_SIZE + 1) + col] <
                hash_input[row * (PH_CORE_HASH_SIZE + 1) + col + 1]) {
                hash |= (1ULL << (row * PH_CORE_HASH_SIZE + col));
            }
        }
    }

    *out_hash = hash;
    return PH_SUCCESS;
}
