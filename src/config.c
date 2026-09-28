#include "context.h"
#include "hashes/hashes.h"
#include "image/image.h"
#include "safety.h"
#include <math.h>
#include <string.h>

/* Every ph_context_set_* below returns ph_error_t. Contract, uniform across all of
 * them: valid input -> PH_SUCCESS; invalid input -> PH_ERR_INVALID_ARGUMENT with the
 * configuration left exactly as it was. No partial application, no clamping and no
 * silent fallback to defaults -- a caller that cannot see its argument was refused ends
 * up hashing with a configuration it did not ask for, and an out-of-range parameter
 * would reach the hash computation and touch memory never sized for it.
 *
 * Deliberately NOT marked PH_NODISCARD, unlike the ph_compute_ and ph_load_ family. These
 * setters are routinely called for their effect in sequences where the arguments are
 * compile-time constants known to be valid (see ph_create() below, the benchmark harness
 * and most tests); requiring every such call site to consume the result would produce a
 * large number of warnings that carry no information. Callers passing runtime values are
 * expected to check the return; callers passing literals are not forced to. */
PH_API ph_error_t ph_context_set_gamma(ph_context_t *ctx, float gamma) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;

    /* isfinite() has to come first: every comparison against NaN is false, so
     * `gamma <= PH_GAMMA_EPSILON` alone would accept NAN (and INFINITY, which is above
     * the epsilon). The bounds keep the exponent applied to a pixel meaningful (gamma
     * is applied per image, normalised by the buffer's own maximum; see ph_apply_gamma()
     * in src/image/color.c). The upper bound keeps it inside a range symmetric about
     * 1.0; see PH_GAMMA_MAX. */
    if (!isfinite((double)gamma) || gamma <= PH_GAMMA_EPSILON || gamma > PH_GAMMA_MAX)
        return PH_ERR_INVALID_ARGUMENT;

    ctx->config.gamma = gamma;
    return PH_SUCCESS;
}

PH_API void ph_context_get_dimensions(const ph_context_t *ctx, int *width, int *height,
                                      int *channels) {
    if (!ctx)
        return;
    if (width)
        *width = ctx->image.width;
    if (height)
        *height = ctx->image.height;
    if (channels)
        *channels = ctx->image.channels;
}

PH_API int ph_is_loaded(const ph_context_t *ctx) { return (ctx && ctx->image.raw_rgb) ? 1 : 0; }

PH_API ph_error_t ph_context_get_gray_weights(const ph_context_t *ctx, int *r, int *g, int *b) {
    if (!ctx || !r || !g || !b)
        return PH_ERR_INVALID_ARGUMENT;
    *r = ctx->config.gray_r;
    *g = ctx->config.gray_g;
    *b = ctx->config.gray_b;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_gray_weights(ph_context_t *ctx, int r, int g, int b) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;

    /* A negative weight is not a "dark" channel, it is a channel that subtracts
     * luminance -- the >> 7 grayscale path assumes non-negative weights summing to 128
     * and would produce out-of-range intermediate values. Rejected rather than
     * interpreted. The sum is accumulated in long long because three int weights can
     * overflow int even when each of them is individually valid. */
    if (r < 0 || g < 0 || b < 0)
        return PH_ERR_INVALID_ARGUMENT;

    long long sum = (long long)r + (long long)g + (long long)b;
    /* sum == 0 is an error, not a silent reset to the BT.601 defaults: "0, 0, 0" must not
     * install a configuration the caller never asked for. */
    if (sum <= 0 || sum > PH_GRAY_WEIGHT_MAX_SUM)
        return PH_ERR_INVALID_ARGUMENT;

    // Normalize to sum 128 for the >> 7 shift
    ctx->config.gray_r = (int)(((long long)r * 128) / sum);
    ctx->config.gray_g = (int)(((long long)g * 128) / sum);
    ctx->config.gray_b = 128 - ctx->config.gray_r - ctx->config.gray_g;

    /* The cached conversion was made with the old weights. */
    ph_drop_gray_cache(ctx);
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_phash_params(ph_context_t *ctx, int dct_size, int reduction_size) {
    /* Upper bounds are hard limits of the pHash implementation:
     * dct_size <= PH_DCT_MAX_SIZE and reduction_size <= PH_DCT_MAX_REDUCTION_SIZE
     * (the hash must fit into 64 bits). Out-of-range input is rejected without
     * touching the configuration; it is never clamped.
     * Lower bound on reduction_size is PH_DCT_MIN_REDUCTION_SIZE, not 1: since the DC
     * coefficient is excluded from the hash, reduction_size == 1 leaves zero AC
     * coefficients and yields the fixed digest 0 for every image. */
    if (!ctx || dct_size <= 0 || dct_size > PH_DCT_MAX_SIZE ||
        reduction_size < PH_DCT_MIN_REDUCTION_SIZE || reduction_size > PH_DCT_MAX_REDUCTION_SIZE ||
        reduction_size > dct_size)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.phash_dct_size = dct_size;
    ctx->config.phash_reduction_size = reduction_size;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_radial_params(ph_context_t *ctx, int projections, int samples,
                                               float sigma) {
    /* projections: the number of angles. At least PH_RADIAL_COEFFS of them, because the
     * hash is that many DCT coefficients of the vector they form; at most as many as the
     * angular resolution of the largest supported image can distinguish.
     * samples: bounded by the diagonal of the largest image the library will process, and
     * at least PH_RADIAL_MIN_SAMPLES, because a single sample per projection has zero
     * variance by definition and yields the all-zero digest for every image.
     * sigma: the Gaussian blur applied before the projections are taken. Must be
     * finite and strictly positive -- ph_gaussian_blur_sigma() leaves its output
     * unwritten otherwise -- and at most PH_RADIAL_MAX_SIGMA, above which its kernel
     * radius would be silently narrower than requested.
     * Derivations are next to PH_RADIAL_MIN_PROJECTIONS / PH_RADIAL_MAX_PROJECTIONS /
     * PH_RADIAL_MIN_SAMPLES / PH_RADIAL_MAX_SAMPLES / PH_RADIAL_MAX_SIGMA. */
    if (!ctx || projections < PH_RADIAL_MIN_PROJECTIONS ||
        projections > PH_RADIAL_MAX_PROJECTIONS || samples < PH_RADIAL_MIN_SAMPLES ||
        samples > PH_RADIAL_MAX_SAMPLES || !isfinite((double)sigma) || !(sigma > 0.0f) ||
        sigma > PH_RADIAL_MAX_SIGMA)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.radial_projections = projections;
    ctx->config.radial_samples = samples;
    ctx->config.radial_sigma = sigma;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_mhash_params(ph_context_t *ctx, float alpha, float level,
                                              int size) {
    /* alpha and level are pHash's own two parameters and set the kernel's scale through
     * sigma = 4 * alpha^level; the kernel is 2*sigma+1 on a side and must stay inside the
     * fixed buffer, which is what caps them. size is the preset the image is normalised
     * to, at least two pixels per block of the 31x31 grid and at most 4096 -- above that
     * the correlation stops being worth its cost long before it stops being meaningful.
     * Rejected values leave the configuration untouched, as everywhere else. */
    if (!ctx || !(alpha > 1.0f) || !(level >= 0.0f) || !isfinite(alpha) || !isfinite(level) ||
        size < PH_MH_MIN_IMAGE_SIZE || size > PH_MH_MAX_IMAGE_SIZE)
        return PH_ERR_INVALID_ARGUMENT;

    double sigma = 4.0 * pow((double)alpha, (double)level);
    if (!(sigma >= 1.0) || 2.0 * sigma + 1.0 > (double)PH_MH_MAX_KERNEL_SIDE)
        return PH_ERR_INVALID_ARGUMENT;

    ctx->config.mhash_alpha = alpha;
    ctx->config.mhash_level = level;
    ctx->config.mhash_size = size;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_block_params(ph_context_t *ctx, int block_size) {
    /* block_size^2 bits have to fit into a ph_digest_t; see PH_BLOCK_MAX_SIZE. Lower bound
     * is PH_BLOCK_MIN_SIZE, not 1: a single block's mean equals itself, the median-of-one
     * always compares >= true, and the digest is the fixed 0x01 for every image. */
    if (!ctx || block_size < PH_BLOCK_MIN_SIZE || block_size > PH_BLOCK_MAX_SIZE)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.block_size = block_size;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_load_grayscale(ph_context_t *ctx, int enable) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;
    /* A boolean flag: any int is a valid argument, normalized to 0/1. There is nothing to
     * reject, so this never fails for a non-NULL context. */
    ctx->config.load_grayscale = enable ? 1 : 0;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_auto_orient(ph_context_t *ctx, int enable) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.auto_orient = enable ? 1 : 0;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_whash_mode(ph_context_t *ctx, ph_whash_mode_t mode) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;
    /* An enum argument is not a guarantee: in C any int value can be passed through an
     * enum parameter, and FFI callers routinely do. Only the declared enumerators are
     * accepted -- ph_compute_whash() dispatches on this field, so an unknown value would
     * silently pick whichever branch the comparison happened to take. */
    if (mode != PH_WHASH_FAST && mode != PH_WHASH_FULL)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.whash_mode = mode;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_whash_remove_max_haar_ll(ph_context_t *ctx, int enable) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.whash_remove_max_haar_ll = enable ? 1 : 0;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_max_pixels(ph_context_t *ctx, uint64_t max_pixels) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;
    /* Every uint64_t is a valid request, 0 included ("no limit of my own"). No upper
     * bound is enforced here on purpose: the implementation ceiling
     * PH_MAX_SUPPORTED_PIXELS is applied where the limit is used, by
     * ph_exceeds_pixel_limit(), so a caller can ask for more than the library
     * supports and simply gets PH_ERR_IMAGE_TOO_LARGE at load time. Rejecting it here
     * would duplicate that policy in two places. */
    ctx->config.max_pixels = max_pixels;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_decode_scale(ph_context_t *ctx, ph_decode_scale_t scale) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;
    /* Same rule as ph_context_set_whash_mode(): an enum parameter is not a guarantee in
     * C, and this value is dispatched on directly by the JPEG backend. */
    if (scale != PH_DECODE_SCALE_FULL && scale != PH_DECODE_SCALE_HALF &&
        scale != PH_DECODE_SCALE_QUARTER && scale != PH_DECODE_SCALE_EIGHTH)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.decode_scale = scale;
    return PH_SUCCESS;
}

/* The configuration of a freshly created context. Shared with ph_digest_info(), which
 * answers for "the defaults" without creating a context. */
void ph_config_init_defaults(struct ph_context_config *config) {
    memset(config, 0, sizeof(*config));
    config->gray_r = PH_GRAY_R;
    config->gray_g = PH_GRAY_G;
    config->gray_b = PH_GRAY_B;
    config->phash_dct_size = PH_DCT_SIZE;
    config->phash_reduction_size = PH_DCT_REDUCTION_SIZE;
    config->mhash_alpha = PH_MH_ALPHA;
    config->mhash_level = PH_MH_LEVEL;
    config->mhash_size = PH_MH_IMAGE_SIZE;
    config->radial_projections = PH_RADIAL_PROJECTIONS;
    config->radial_samples = PH_RADIAL_SAMPLES;
    config->radial_sigma = PH_RADIAL_DEFAULT_SIGMA;
    config->block_size = PH_BLOCK_SIZE;
    config->whash_mode = PH_WHASH_FAST;
    config->whash_remove_max_haar_ll = 0;
    config->max_pixels = PH_DEFAULT_MAX_PIXELS;
    config->decode_scale = PH_DECODE_SCALE_FULL;

    /* Optimization Default: disabled by default for compatibility with
     * ColorHash and custom weights. */
    config->load_grayscale = 0;
    /* Applying EXIF/WebP-metadata orientation defaults to on: an image hashed
     * "as the sensor stored it" instead of "as it displays" is a correctness
     * bug, not a neutral choice. See ph_context_set_auto_orient(). */
    config->auto_orient = 1;
    config->gamma = PH_DEFAULT_GAMMA;
}
