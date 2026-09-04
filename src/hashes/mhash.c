/* mHash -- Marr-Hildreth hash.
 *
 * Two sources, and they cover different halves of it.
 *
 * The OPERATOR is the Laplacian of Gaussian of D. Marr and E. Hildreth, "Theory of edge
 * detection", Proc. R. Soc. Lond. B 207:187-217, 1980. The kernel built below is the
 * sampled Mexican hat: (2 - A) * exp(-A/2), A being the squared distance from the centre
 * in units of the scale.
 *
 * The HASH built on top of it has no paper. It is pHash's ph_mh_imagehash(), by Evan
 * Klinger and David Starkweather, and Zauner (Diplomarbeit, FH Hagenberg 2010, section
 * 3.2.2) says plainly that the construction "has not been proposed previously". So for the
 * construction pHash's implementation is the primary source, not a reference
 * implementation of something else, and the steps and constants below are taken from it as
 * facts about the algorithm: blur at sigma 1, resize to 512x512, equalise over 256 levels,
 * correlate with the LoG kernel at alpha = 2 and level = 1, normalise the response, sum it
 * over 16x16 blocks into a 31x31 grid, and emit nine bits per 3x3 window of that grid at
 * stride 4 -- 64 windows, 576 bits, 72 bytes.
 *
 * Values are NOT bit-identical to pHash's and are not meant to be. Matching them would
 * mean reproducing CImg's recursive Deriche blur, its quintic resize and its equaliser
 * exactly; that is a lot of fragile work for a comparison nothing here can check, since
 * there is no pHash build to check against. What is reproduced is the construction and
 * every parameter of it. Where this code differs in kind:
 *
 *   - the blur is a truncated separable Gaussian at sigma 1, not Deriche's recursive
 *     approximation of one;
 *   - the resize to 512x512 goes through ph_resize_lanczos(), i.e. stb's default filter,
 *     where pHash asks CImg for quintic interpolation.
 *
 * Note what this is not, despite the name: Marr and Hildreth detect edges as the
 * ZERO-CROSSINGS of the filtered image, and neither pHash nor this code looks for one.
 * The response is block-summed and thresholded against a local mean. The operator is
 * theirs; the edge detector is not being implemented.
 *
 * Before 2.0.0 this function computed something else entirely -- the sign of a
 * four-neighbour discrete Laplacian on a stride-2 grid of an 18x18 image, 64 bits, no
 * Gaussian, no scale, no zero-crossings -- under the same name.
 */
#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int ph_mh_kernel(float alpha, float level, float *out, int max_side) {
    if (!out || !(alpha > 0.0f))
        return 0;

    /* sigma here is the kernel's half-width in samples, not the width of a Gaussian:
     * pHash names it that and computes it as an integer, which is what fixes the kernel
     * at 17x17 for the defaults. */
    int sigma = (int)(4.0f * powf(alpha, level));
    if (sigma < 1)
        return 0;
    int side = 2 * sigma + 1;
    if (side > max_side)
        return 0;

    float scale = powf(alpha, -level);
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            float xpos = scale * (float)(x - sigma);
            float ypos = scale * (float)(y - sigma);
            float a = xpos * xpos + ypos * ypos;
            out[y * side + x] = (2.0f - a) * expf(-a / 2.0f);
        }
    }
    return side;
}

/* Block sums of the Marr-Hildreth response, without ever materialising the response.
 *
 * The definition is: correlate the image with the kernel, then sum the response over each
 * 16x16 block. Written out, the sum over one block is
 *
 *     B(by,bx) = SUM_{ky,kx} K[ky][kx] * SUM_{(y,x) in block} I(y+ky-half, x+kx-half)
 *
 * -- the kernel tap comes out of the inner sum, because it does not depend on the pixel.
 * What is left inside is a 16x16 box over the edge-replicated image, which an integral
 * image answers in four lookups. That turns 496*496*289 multiply-adds into 31*31*289,
 * some fifty times less work.
 *
 * It is also the more accurate of the two, which matters more than the speed. The LoG
 * kernel sums to nearly zero, so evaluating it per pixel in float is a sum of large
 * products that almost entirely cancel -- catastrophic cancellation, and the error
 * survives into the block sum. Here the inner sums are exact integers and only 289 terms
 * are accumulated, in double. Measured on the property corpus, the difference between the
 * two is not academic: separability 1.81 evaluating the definition directly in float,
 * 2.49 this way.
 *
 * `scratch` must hold ph_mh_block_sums_scratch(n, half) bytes. Returns nothing; `out` is
 * PH_MH_GRID * PH_MH_GRID floats. */
size_t ph_mh_block_sums_scratch(int n, int half) {
    size_t pad_n = (size_t)n + 2 * (size_t)half;
    return (pad_n + 1) * (pad_n + 1) * sizeof(int64_t) + pad_n * pad_n;
}

void ph_mh_block_sums(const uint8_t *img, int n, int block, const float *kernel, int side,
                      uint8_t *scratch, float *out) {
    int half = side / 2;
    const int pad_n = n + 2 * half;
    /* The int64_t array goes first: the scratchpad is suitably aligned, and putting the
     * byte buffer first would leave it aligned only when pad_n * pad_n happens to be a
     * multiple of eight. */
    int64_t *integral = (int64_t *)scratch;
    uint8_t *padded = scratch + (size_t)(pad_n + 1) * (size_t)(pad_n + 1) * sizeof(int64_t);

    for (int y = 0; y < pad_n; y++) {
        int sy = y - half;
        if (sy < 0)
            sy = 0;
        if (sy >= n)
            sy = n - 1;
        const uint8_t *srow = &img[(size_t)sy * n];
        uint8_t *drow = &padded[(size_t)y * pad_n];
        for (int x = 0; x < pad_n; x++) {
            int sx = x - half;
            if (sx < 0)
                sx = 0;
            if (sx >= n)
                sx = n - 1;
            drow[x] = srow[sx];
        }
    }

    /* integral[y * istride + x] is the sum of padded[0..y-1][0..x-1]. */
    const int istride = pad_n + 1;
    memset(integral, 0, (size_t)istride * sizeof(int64_t));
    for (int y = 0; y < pad_n; y++) {
        int64_t row_sum = 0;
        integral[(size_t)(y + 1) * istride] = 0;
        for (int x = 0; x < pad_n; x++) {
            row_sum += padded[(size_t)y * pad_n + x];
            integral[(size_t)(y + 1) * istride + (x + 1)] =
                integral[(size_t)y * istride + (x + 1)] + row_sum;
        }
    }

    for (int by = 0; by < PH_MH_GRID; by++) {
        for (int bx = 0; bx < PH_MH_GRID; bx++) {
            int y0 = by * block;
            int x0 = bx * block;
            double sum = 0.0;
            for (int ky = 0; ky < side; ky++) {
                const float *krow = &kernel[ky * side];
                int ry = y0 + ky;
                const int64_t *top = &integral[(size_t)ry * istride];
                const int64_t *bot = &integral[(size_t)(ry + block) * istride];
                for (int kx = 0; kx < side; kx++) {
                    int rx = x0 + kx;
                    int64_t box = bot[rx + block] - bot[rx] - top[rx + block] + top[rx];
                    sum += (double)krow[kx] * (double)box;
                }
            }
            out[by * PH_MH_GRID + bx] = (float)sum;
        }
    }
}

PH_API ph_error_t ph_compute_mhash(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->image.is_loaded || !out_digest)
        return PH_ERR_INVALID_ARGUMENT;

    const int n = ctx->config.mhash_size;
    if (n < PH_MH_MIN_IMAGE_SIZE || n > PH_MH_MAX_IMAGE_SIZE)
        return PH_ERR_INVALID_ARGUMENT;
    /* The grid is always 31x31, so the digest is always 576 bits whatever the preset;
     * the block size follows from the preset instead. */
    const int block = n / PH_MH_GRID;
    const size_t npix = (size_t)n * (size_t)n;

    memset(out_digest, 0, sizeof(ph_digest_t));
    out_digest->size = (uint8_t)PH_MH_BYTES;
    out_digest->kind = (uint8_t)PH_DIGEST_KIND_BITS;

    uint8_t *gray = ph_get_gray(ctx);
    if (!gray)
        return PH_ERR_ALLOCATION_FAILED;

    /* The blur runs at full resolution, before the resize, as in the source. Both the
     * blurred copy and the float scratch it needs are plain heap allocations rather than
     * arena ones: the image can be far larger than 512x512, and the arena is about to be
     * used for the fixed-size buffers below. */
    size_t src_pixels = (size_t)ctx->image.width * (size_t)ctx->image.height;
    uint8_t *blurred = (uint8_t *)malloc(src_pixels);
    float *blur_scratch = (float *)malloc(src_pixels * sizeof(float));
    if (!blurred || !blur_scratch) {
        free(blurred);
        free(blur_scratch);
        return PH_ERR_ALLOCATION_FAILED;
    }
    ph_gaussian_blur_sigma(gray, ctx->image.width, ctx->image.height, PH_MH_BLUR_SIGMA,
                           blur_scratch, blurred);
    free(blur_scratch);

    /* One arena request for everything. Two would not do: ph_get_scratchpad() may grow
     * the arena, which reallocates its backing buffer and invalidates any pointer already
     * handed out from it -- `norm` in particular. The block-sum scratch comes first so
     * that its int64_t array is aligned whatever the image size. */
    const int kernel_half = (int)(4.0f * powf(ctx->config.mhash_alpha, ctx->config.mhash_level));
    size_t work_bytes = ph_mh_block_sums_scratch(n, kernel_half);
    size_t saved_offset = ctx->arena.offset;
    uint8_t *arena = ph_get_scratchpad(ctx, work_bytes + npix);
    if (!arena) {
        free(blurred);
        return PH_ERR_ALLOCATION_FAILED;
    }
    uint8_t *work = arena;
    uint8_t *norm = arena + work_bytes;

    ph_resize_lanczos(blurred, ctx->image.width, ctx->image.height, norm, n, n);
    free(blurred);
    ph_equalize_histogram(norm, npix, PH_MH_EQUALIZE_LEVELS);

    /* 17x17 at the fixed alpha and level; the bound is generous so that a future
     * parameterisation cannot overrun it silently. */
    float kernel[PH_MH_MAX_KERNEL_SIDE * PH_MH_MAX_KERNEL_SIDE];
    int side = ph_mh_kernel(ctx->config.mhash_alpha, ctx->config.mhash_level, kernel,
                            PH_MH_MAX_KERNEL_SIDE);
    if (side <= 0) {
        ctx->arena.offset = saved_offset;
        return PH_ERR_INVALID_ARGUMENT;
    }
    int half = side / 2;

    /* The source normalises the response to [0,1] before the block sums. That step is
     * dropped: the block sums are affine in the response, the window mean is affine in the
     * block sums, and `value > mean` is invariant under an affine map with positive scale,
     * so it cannot change a single bit. */
    /* The scratch was sized from the kernel half-width computed above; the kernel the
     * builder actually produced must agree, or the block sums would run off the end. */
    if (half != kernel_half) {
        ctx->arena.offset = saved_offset;
        return PH_ERR_INVALID_ARGUMENT;
    }
    float blocks[PH_MH_GRID * PH_MH_GRID];
    ph_mh_block_sums(norm, n, block, kernel, side, work, blocks);

    /* Nine bits per 3x3 window of the block grid, thresholded against the window's own
     * mean, packed most-significant bit first as the source packs them. */
    int bit = 0;
    for (int wy = 0; wy < PH_MH_WINDOWS_PER_AXIS; wy++) {
        for (int wx = 0; wx < PH_MH_WINDOWS_PER_AXIS; wx++) {
            int oy = wy * PH_MH_WINDOW_STRIDE;
            int ox = wx * PH_MH_WINDOW_STRIDE;
            float window[PH_MH_WINDOW * PH_MH_WINDOW];
            float sum = 0.0f;
            for (int y = 0; y < PH_MH_WINDOW; y++)
                for (int x = 0; x < PH_MH_WINDOW; x++) {
                    float v = blocks[(oy + y) * PH_MH_GRID + (ox + x)];
                    window[y * PH_MH_WINDOW + x] = v;
                    sum += v;
                }
            float mean = sum / (float)(PH_MH_WINDOW * PH_MH_WINDOW);
            for (int i = 0; i < PH_MH_WINDOW * PH_MH_WINDOW; i++) {
                if (window[i] > mean)
                    out_digest->data[bit / 8] |= (uint8_t)(0x80u >> (bit % 8));
                bit++;
            }
        }
    }

    ctx->arena.offset = saved_offset;
    return PH_SUCCESS;
}
