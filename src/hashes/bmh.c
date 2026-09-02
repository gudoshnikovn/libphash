/* BMH -- Block Mean Value based image perceptual hash.
 *
 * Bian Yang, Fan Gu, Xiamu Niu, "Block Mean Value Based Image Perceptual Hashing",
 * IIH-MSP 2006, pp. 167-172, doi:10.1109/IIH-MSP.2006.265125. The paper is paywalled;
 * the steps followed here are Zauner's reproduction of its method 1 (Diplomarbeit,
 * FH Hagenberg 2010, section 3.1.4), whose author implemented that method into pHash.
 *
 * Method 1: grayscale, normalise to a preset size, divide into N non-overlapping
 * blocks, take the mean of each, and threshold against the MEDIAN of the mean sequence
 * (equation 3.9: h(i) = 1 for M_i >= M_d, 0 otherwise). Step (c) of the paper, which
 * permutes the block order under a secret key, is omitted here as it is in pHash: it is
 * a security property, not a perceptual one, and the paper names no cipher.
 *
 * KNOWN DIVERGENCE FROM THE SOURCE: this code thresholds against the arithmetic MEAN of
 * the block values, not their median. The median is what makes the bit distribution
 * balanced by construction -- exactly half ones -- which is the property the paper
 * relies on; with the mean, a dark image with a few bright blocks yields a lopsided
 * hash. Tracked as a defect in docs/algorithm-provenance.md.
 *
 * Second, smaller divergence: the source normalises the image to a preset size and then
 * averages blocks of it. Box-resampling straight to the block grid equals that only
 * when the source dimensions are a multiple of the grid; otherwise source pixels are
 * weighted across block boundaries.
 */
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
