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
 * The threshold is the median, which is what makes the bit distribution balanced by
 * construction -- half ones, whatever the image -- and that balance is the property the
 * paper relies on. Thresholding at the arithmetic mean instead would give a lopsided hash
 * for a dark image with a few bright blocks.
 *
 * Note that this puts the library at odds with OpenCV's BlockMeanHash, the other
 * implementation of this paper in wide use. It resizes to 256x256 and then thresholds
 * against the arithmetic mean of the image -- which it stores in a variable it names
 * `median`. The name says the intent and the value says the slip; the paper is followed
 * here, not OpenCV. pHash carries no block-mean hash at all today,
 * although Zauner says he contributed one, so there is no reference implementation by the
 * source's own author to check against.
 *
 * Remaining divergence: the source normalises the image to a preset size and then
 * averages blocks of it. Box-resampling straight to the block grid equals that only
 * when the source dimensions are a multiple of the grid; otherwise source pixels are
 * weighted across block boundaries.
 *
 * Bit order: `data[i/8] |= 1 << (i%8)`, LSB first within each byte, blocks in raster
 * order. The paper defines a bit sequence (equation 3.9), not a byte layout, so there
 * is nothing to conform to here either -- a choice, recorded rather than left silent.
 */
#include "context.h"
#include "hashes/hashes.h"
#include "image/image.h"
#include "safety.h"

#include <stdlib.h>
#include <string.h>

PH_API ph_error_t ph_compute_bmh(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !out_digest) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    if (!ctx->image.is_loaded) {
        return PH_ERR_EMPTY_IMAGE;
    }

    int block_size = ctx->config.block_size;
    if (block_size <= 0) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    /* Casting to size_t before multiplying does not help where size_t is 32 bits: the
     * product wraps like a plain int product (block_size = 1<<30 wraps to 0 and INT_MAX
     * to 1), ph_get_scratchpad() below would hand back a tiny (or NULL) buffer, and
     * ph_area_downscale() would address it as block_size^2 bytes -- a huge out-of-bounds
     * write, not a clean allocation failure. ph_safe_image_alloc_size() does the check
     * width-independently (uint64_t arithmetic, checked against SIZE_MAX). */
    size_t total_pixels;
    if (!ph_safe_image_alloc_size(ph_size(block_size), ph_size(block_size), 1, &total_pixels)) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    /* One bit per block. The size is capped at PH_DIGEST_MAX_BYTES inside
     * ph_digest_shape(): unreachable through the public API, because
     * ph_context_set_block_params() rejects block_size > PH_BLOCK_MAX_SIZE (32, whose
     * 32*32 bits = 128 bytes exactly fill a digest). Kept as defence in depth for a
     * config field written by some other route (tests do exactly that). Note what the cap
     * does and why the setter bound matters: the reported size is truncated while all
     * `total_pixels` blocks are still hashed, so the caller would get PH_SUCCESS with a
     * silently partial hash -- the anti-pattern the setter's rejection prevents. */
    ph_digest_begin(out_digest, ctx, PH_ALGO_BMH);

    ph_arena_mark_t arena_mark = ph_arena_mark(ctx);
    uint8_t *block_data = ph_get_scratchpad(ctx, total_pixels);
    if (!block_data) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    /* Each block's value is the mean of the pixels it covers -- the paper's "block mean
     * value" -- by area averaging, shared with aHash, pHash and wHash. */
    if (!ph_area_downscale(ctx, block_size, block_size, block_data)) {
        ph_arena_release(ctx, arena_mark);
        return PH_ERR_ALLOCATION_FAILED;
    }

    /* The median of the block values, by counting sort: they are bytes, so 256 buckets
     * settle it in one pass over the data instead of sorting up to 1024 values.
     *
     * "The median" for an even count is the n/2-th order statistic, zero-indexed -- the
     * upper of the two central values. The paper does not say which to take, and this is
     * the choice that keeps its property: with the >= of equation 3.9, exactly half the
     * blocks clear the upper central value, so the hash has as many ones as zeroes.
     * Averaging the two central values would select the same blocks whenever they differ,
     * so this is the cheaper way to say the same thing. Equal block values are the one
     * thing that can still tip the balance, and nothing can be done about that: they are
     * bytes, and ties are common on flat images. */
    size_t histogram[256] = {0};
    for (size_t i = 0; i < total_pixels; i++) {
        histogram[block_data[i]]++;
    }

    size_t median_rank = total_pixels / 2;
    size_t seen = 0;
    uint8_t median = 255;
    for (int v = 0; v < 256; v++) {
        seen += histogram[v];
        if (seen > median_rank) {
            median = (uint8_t)v;
            break;
        }
    }

    size_t max_bits = out_digest->size * 8u;
    for (size_t i = 0; i < total_pixels && i < max_bits; i++) {
        if (block_data[i] >= median) {
            out_digest->data[i / 8] |= (1 << (i % 8));
        }
    }

    ph_arena_release(ctx, arena_mark);
    return PH_SUCCESS;
}
