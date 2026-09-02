/* mHash -- sign of a discrete Laplacian, sampled on a grid.
 *
 * NO PRIMARY SOURCE. This algorithm is original to this library.
 *
 * THE NAME IS A MISATTRIBUTION AND IS KEPT ONLY FOR API COMPATIBILITY. It was
 * introduced as a "Marr-Hildreth Hash" and is not one. Marr-Hildreth means the
 * Laplacian of Gaussian: the image is smoothed at a chosen scale sigma, convolved with
 * the sampled LoG kernel, and edges are the ZERO-CROSSINGS of the result (Marr and
 * Hildreth, "Theory of edge detection", Proc. R. Soc. Lond. B 207:187-217, 1980; see
 * also Zauner 2010 sections 3.1.2 and 3.2.2). What this file computes is the sign of a
 * 3x3 four-neighbour discrete Laplacian on a stride-2 grid of an 18x18 box-resized
 * image: no Gaussian, no scale parameter, no zero-crossings.
 *
 * It is also not pHash's ph_mh_imagehash(), which per Zauner 3.2.2 uses a Canny-Deriche
 * blur at sigma 1.0, resizes to 512x512, histogram-equalises over 256 levels, applies a
 * real LoG kernel and emits 576 bits. This produces 64 bits from an 18x18 grid.
 *
 * Cite Marr and Hildreth here only if this is ever changed to actually compute a LoG
 * with zero-crossing detection. Until then it is judged by measurable properties alone.
 * See docs/algorithm-provenance.md and docs/references.md.
 */
#include "internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_mhash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->image.is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;

    int scale_size = 18; // Fixed size to yield exactly 64 bits (8x8) using 3x3 kernel and step=2

    uint8_t *full_gray = ph_get_gray(ctx);
    if (!full_gray)
        return PH_ERR_ALLOCATION_FAILED;

    size_t saved_offset = ctx->arena.offset;
    uint8_t *scratch = ph_get_scratchpad(ctx, (size_t)scale_size * scale_size);
    if (!scratch)
        return PH_ERR_ALLOCATION_FAILED;
    uint8_t *block_data = scratch;

    ph_resize_box(full_gray, ctx->image.width, ctx->image.height, block_data, scale_size,
                  scale_size);

    int step = 2;
    *out_hash = ph_laplacian_scan(block_data, scale_size, step);

    ctx->arena.offset = saved_offset;
    return PH_SUCCESS;
}

uint64_t ph_laplacian_scan(const uint8_t *grid, int size, int step) {
    uint64_t hash = 0;
    int bit_idx = 0;

    for (int y = 1; y < size - 1 && bit_idx < 64; y += step) {
        for (int x = 1; x < size - 1 && bit_idx < 64; x += step) {
            int center = grid[y * size + x] * 4;
            int neighbors = grid[(y - 1) * size + x] + grid[(y + 1) * size + x] +
                            grid[y * size + (x - 1)] + grid[y * size + (x + 1)];
            if (center - neighbors > 0)
                hash |= (1ULL << bit_idx);
            bit_idx++;
        }
    }
    return hash;
}
