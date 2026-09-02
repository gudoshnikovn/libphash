/* dHash -- Difference Hash.
 *
 * Proposed by David Oftedal as a comment on Krawetz's 2011 post; named, described and
 * evaluated by Neal Krawetz in "Kind of Like That", The Hacker Factor Blog,
 * 21 January 2013.
 * https://www.hackerfactor.com/blog/index.php?/archives/529-Kind-of-Like-That.html
 *
 * A blog post rather than a paper, and the only description by the people responsible.
 * It prescribes a 9x8 reduction, grayscale, and 64 bits from the 8 horizontal
 * differences of each of the 8 rows, with "a '1' to indicate that P[x] < P[x+1]" and
 * the bits set "from left to right, top to bottom using big-endian". This code follows
 * that exactly, including the direction of the comparison. The resampling filter is not
 * specified by the source; see ahash.c on ph_resize_lanczos() being misnamed.
 *
 * See docs/algorithm-provenance.md and docs/references.md.
 */
#include "internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_dhash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->image.is_loaded || !out_hash) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    uint8_t *gray_input = ph_get_gray(ctx);
    if (!gray_input) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    uint8_t hash_input[(PH_CORE_HASH_SIZE + 1) * PH_CORE_HASH_SIZE];

    ph_resize_lanczos(gray_input, ctx->image.width, ctx->image.height, hash_input,
                      PH_CORE_HASH_SIZE + 1, PH_CORE_HASH_SIZE);

    uint64_t hash = 0;
    for (int row = 0; row < PH_CORE_HASH_SIZE; row++) {
        for (int col = 0; col < PH_CORE_HASH_SIZE; col++) {
            if (hash_input[row * (PH_CORE_HASH_SIZE + 1) + col] <
                hash_input[row * (PH_CORE_HASH_SIZE + 1) + col + 1]) {
                hash |= (1ULL << (63 - (row * PH_CORE_HASH_SIZE + col)));
            }
        }
    }

    *out_hash = hash;
    return PH_SUCCESS;
}
