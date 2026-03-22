#include "../internal.h"
#include <stdlib.h>
#include <string.h>

PH_API ph_error_t ph_compute_bmh(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->is_loaded || !out_digest) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    int block_size = ctx->block_size;
    int total_pixels = block_size * block_size;

    memset(out_digest, 0, sizeof(ph_digest_t));
    int req_bytes = (total_pixels + 7) / 8;
    if (req_bytes > PH_DIGEST_MAX_BYTES) {
        out_digest->size = PH_DIGEST_MAX_BYTES;
    } else {
        out_digest->size = (uint8_t)req_bytes;
    }

    uint8_t *full_gray = ph_get_gray(ctx);
    if (!full_gray)
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t *block_data = ph_get_scratchpad(ctx, total_pixels);
    if (!block_data)
        return PH_ERR_ALLOCATION_FAILED;

    ph_resize_box(full_gray, ctx->width, ctx->height, block_data, block_size, block_size);

    uint64_t total_sum = 0;
    for (int i = 0; i < total_pixels; i++) {
        total_sum += block_data[i];
    }
    uint8_t avg = (uint8_t)(total_sum / total_pixels);

    int max_bits = out_digest->size * 8;
    for (int i = 0; i < total_pixels && i < max_bits; i++) {
        if (block_data[i] >= avg) {
            out_digest->data[i / 8] |= (1 << (i % 8));
        }
    }

    return PH_SUCCESS;
}
