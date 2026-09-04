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
 * paper relies on. Until 2.0.0 this code used the arithmetic mean, under which a dark
 * image with a few bright blocks yields a lopsided hash.
 *
 * Note that this puts the library at odds with OpenCV's BlockMeanHash, the other
 * implementation of this paper in wide use. It resizes to 256x256 and then thresholds on
 * `double const median = cv::mean(grayImg_)[0]` -- the arithmetic mean, stored in a
 * variable called median. The name says the intent and the code says the mistake; the
 * paper is followed here, not OpenCV. pHash carries no block-mean hash at all today,
 * although Zauner says he contributed one, so there is no reference implementation by the
 * source's own author to check against.
 *
 * Remaining divergence: the source normalises the image to a preset size and then
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

    /* The median of the block values, by counting sort: they are bytes, so 256 buckets
     * settle it in one pass over the data instead of sorting up to 484 values.
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
    for (size_t i = 0; i < total_pixels; i++)
        histogram[block_data[i]]++;

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

    size_t max_bits = (size_t)out_digest->size * 8;
    for (size_t i = 0; i < total_pixels && i < max_bits; i++) {
        if (block_data[i] >= median) {
            out_digest->data[i / 8] |= (1 << (i % 8));
        }
    }

    ctx->arena.offset = saved_offset;
    return PH_SUCCESS;
}
