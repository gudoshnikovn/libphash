#include "internal.h"
#include <stdlib.h>
#include <string.h>

PH_API ph_error_t ph_compute_bmh(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->image.is_loaded || !out_digest) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    int block_size = ctx->config.block_size;
    if (block_size <= 0)
        return PH_ERR_INVALID_ARGUMENT;
    /* size_t, not int: block_size = 46341 already overflows the int product (R03/H6).
     * The upper bound on block_size itself lives in the setter (R04); here we only
     * guarantee the arithmetic is well-defined and the allocation is honestly sized. */
    size_t total_pixels = (size_t)block_size * (size_t)block_size;

    memset(out_digest, 0, sizeof(ph_digest_t));
    size_t req_bytes = (total_pixels + 7) / 8;
    if (req_bytes > PH_DIGEST_MAX_BYTES) {
        /* Unreachable through the public API since 2.0.0: ph_context_set_block_params()
         * rejects block_size > PH_BLOCK_MAX_SIZE (22), and 22*22 bits = 61 bytes is the
         * largest grid that fits a ph_digest_t. Kept as defence in depth for a config
         * field written by some other route (tests do exactly that).
         *
         * Note what this branch does, and why the setter bound matters: it truncates the
         * reported digest size to 64 bytes but keeps hashing all `total_pixels` blocks,
         * so the caller received PH_SUCCESS together with a silently partial hash -- the
         * same anti-pattern as H5. It is not turned into an error here because the size
         * is the only thing wrong and the setter now makes the situation impossible. */
        out_digest->size = PH_DIGEST_MAX_BYTES;
    } else {
        out_digest->size = (uint8_t)req_bytes;
    }

    uint8_t *full_gray = ph_get_gray(ctx);
    if (!full_gray)
        return PH_ERR_ALLOCATION_FAILED;

    size_t saved_offset = ctx->arena.offset;
    uint8_t *block_data = ph_get_scratchpad(ctx, total_pixels);
    if (!block_data)
        return PH_ERR_ALLOCATION_FAILED;

    ph_resize_box(full_gray, ctx->image.width, ctx->image.height, block_data, block_size,
                  block_size);

    uint64_t total_sum = 0;
    for (size_t i = 0; i < total_pixels; i++) {
        total_sum += block_data[i];
    }
    uint8_t avg = (uint8_t)(total_sum / total_pixels);

    size_t max_bits = (size_t)out_digest->size * 8;
    for (size_t i = 0; i < total_pixels && i < max_bits; i++) {
        if (block_data[i] >= avg) {
            out_digest->data[i / 8] |= (1 << (i % 8));
        }
    }

    ctx->arena.offset = saved_offset;
    return PH_SUCCESS;
}
