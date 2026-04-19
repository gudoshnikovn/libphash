#include "internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_ahash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->image.is_loaded || !out_hash) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    uint8_t *gray_input = ph_get_gray(ctx);
    if (!gray_input) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    uint8_t hash_input[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];

    ph_resize_lanczos(gray_input, ctx->image.width, ctx->image.height, hash_input,
                      PH_CORE_HASH_SIZE, PH_CORE_HASH_SIZE);

    uint64_t total_sum = 0;
    int num_pixels = PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE;
    for (int i = 0; i < num_pixels; i++) {
        total_sum += hash_input[i];
    }
    uint8_t avg = (uint8_t)(total_sum / num_pixels);

    uint64_t hash = 0;
    for (int i = 0; i < num_pixels; i++) {
        if (hash_input[i] > avg) {
            hash |= (1ULL << (63 - i));
        }
    }

    *out_hash = hash;
    return PH_SUCCESS;
}
