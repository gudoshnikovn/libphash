/* aHash -- Average Hash.
 *
 * Neal Krawetz, "Looks Like It", The Hacker Factor Blog, 26 May 2011.
 * https://www.hackerfactor.com/blog/index.php?/archives/432-Looks-Like-It.html
 *
 * The source is a blog post, not a paper: there is no academic publication of aHash,
 * and this is the author's own description. It prescribes an 8x8 reduction, grayscale,
 * the mean of the 64 values, and one bit per pixel for "above or below the mean". The
 * bit order is explicitly left free ("as long as you are consistent"); the order used
 * here -- MSB first, left to right, top to bottom -- is the one the post itself uses.
 *
 * The resampling filter, the grayscale coefficients and the handling of a pixel exactly
 * equal to the mean are not specified by the source. The reduction is an area average
 * (ph_area_downscale()): each of the 64 values is the mean of the part of the image it
 * covers, which is what "shrink" computes when nothing else is said, and it is shared
 * with pHash, wHash and BMH. See docs/algorithm-provenance.md for the full comparison and
 * docs/references.md for the citation.
 */
#include "context.h"
#include "hashes/hashes.h"
#include "image/image.h"

#include <stdlib.h>

PH_API ph_error_t ph_compute_ahash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !out_hash) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    if (!ctx->image.is_loaded) {
        return PH_ERR_EMPTY_IMAGE;
    }

    uint8_t hash_input[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];
    if (!ph_area_downscale(ctx, PH_CORE_HASH_SIZE, PH_CORE_HASH_SIZE, hash_input)) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    uint64_t total_sum = 0;
    const size_t num_pixels = PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE;
    for (size_t i = 0; i < num_pixels; i++) {
        total_sum += hash_input[i];
    }

    /* pixel >= sum / n, compared exactly as pixel * n >= sum. A mean truncated to an
     * integer would turn every pixel equal to floor(mean) -- below a fractional mean --
     * into a tie and set its bit. Only a pixel exactly equal to the mean is a tie, and it
     * is set, the same rule as BMH. */
    uint64_t hash = 0;
    for (size_t i = 0; i < num_pixels; i++) {
        if ((uint64_t)hash_input[i] * num_pixels >= total_sum) {
            hash |= (1ULL << (63 - i));
        }
    }

    *out_hash = hash;
    return PH_SUCCESS;
}
