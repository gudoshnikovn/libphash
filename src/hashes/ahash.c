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
 * equal to the mean are not specified by the source. Note that ph_resize_lanczos() does
 * NOT use Lanczos: it lets stb_image_resize2 pick its default, which for a downscale is
 * Mitchell. The name is wrong; the behaviour violates nothing, but it is not the filter
 * ImageHash uses either. See docs/algorithm-provenance.md
 * for the full comparison and docs/references.md for the citation.
 */
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
