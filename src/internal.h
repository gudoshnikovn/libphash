#ifndef INTERNAL_H
#define INTERNAL_H

#include "libphash.h"
#include <limits.h>
#include <stdint.h>

/*
 * Internal Image Processing Helpers
 */

/* Converts RGB/RGBA to Grayscale with custom weights */
void ph_to_grayscale(const ph_context_t *ctx, const uint8_t *src, int w, int h, int channels,
                     uint8_t *dst);

/* Resizes a grayscale image using box sampling (averaging) */
void ph_resize_box(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

void ph_resize_lanczos(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

/* Applies a 3x3 Gaussian Blur to reduce noise */
void ph_apply_gaussian_blur(ph_context_t *ctx, uint8_t *src, int w, int h, uint8_t *dst);

/* Applies Gamma Correction (gamma=2.2) to normalize brightness */
void ph_apply_gamma(const ph_context_t *ctx, uint8_t *data, int w, int h);

/* Applies 3x3 Laplacian sharpening for edge preservation */
void ph_apply_laplacian_3x3(const uint8_t *src, int w, int h, uint8_t *dst);

/* Separable Gaussian blur at an arbitrary sigma, truncated at three standard deviations.
 * `scratch` is w*h floats supplied by the caller; this allocates nothing. */
void ph_gaussian_blur_sigma(const uint8_t *src, int w, int h, float sigma, float *scratch,
                            uint8_t *dst);

/* Histogram equalisation over `levels` buckets (2..256), in place. */
void ph_equalize_histogram(uint8_t *data, size_t n, int levels);

uint8_t *ph_get_gray(ph_context_t *ctx);

/* mHash Laplacian scan helper */
/* Builds the sampled Laplacian-of-Gaussian kernel of Marr & Hildreth, as pHash
 * parameterises it: side 2*sigma+1 with sigma = 4*alpha^level, element
 * (2 - A) * exp(-A/2) where A is the squared distance from the centre scaled by
 * alpha^-level. Writes side*side floats and returns the side, or 0 on bad input. */
int ph_mh_kernel(float alpha, float level, float *out, int max_side);

/* Scratch bytes ph_mh_block_sums() needs for an n x n image and a kernel half-width. */
size_t ph_mh_block_sums_scratch(int n, int half);

/* Sums the correlation of `img` (n x n) with `kernel` (side x side, side odd) over each
 * PH_MH_BLOCK_PIXELS-square block, writing PH_MH_GRID * PH_MH_GRID floats to `out`.
 * Equivalent to correlating and then summing, but computed through an integral image:
 * far cheaper, and far more accurate, since the LoG kernel nearly cancels. Edges
 * replicate. */
void ph_mh_block_sums(const uint8_t *img, int n, int block, const float *kernel, int side,
                      uint8_t *scratch, float *out);

typedef struct {
    double mean;
    double std_dev;
    double skew;
} ph_channel_moments_t;

/* num_pixels is a size_t on purpose: it is width * height, which does not fit an
 * int for images above ~46340x46340 (see R03/H6). */
ph_channel_moments_t ph_compute_moments(const uint8_t *data, size_t num_pixels, int channels,
                                        int channel_index);

/* Partial 2D DCT: computes the top-left reduction_size x reduction_size block.
 *
 * Bounds are hard limits, not hints: dct_size must be in [1, PH_DCT_MAX_SIZE] and
 * reduction_size in [1, PH_DCT_MAX_REDUCTION_SIZE] and <= dct_size. On violation the
 * function writes nothing to `out` and returns PH_ERR_INVALID_ARGUMENT — callers MUST
 * check the result, otherwise `out` stays whatever it was (see R02/H5). */
PH_NODISCARD ph_error_t ph_dct2_partial(const float *dct_mat, const uint8_t *input, int dct_size,
                                        int reduction_size, float *out);

uint64_t ph_median_bitpack(const float *values, int n);

/* As above, but the median is taken over values[median_from..n-1] only, while every one
 * of the n values still gets a bit. pHash thresholds its 8x8 DCT block against the median
 * of its 63 AC coefficients, leaving the DC term out of the decision but not out of the
 * hash; median_from = 1 is that. median_from = 0 is ph_median_bitpack(). */
uint64_t ph_median_bitpack_from(const float *values, int n, int median_from);

/* Structural validity of a caller-supplied ph_digest_t.
 *
 * ph_digest_t is a flat public struct that FFI bindings (Python, C#, Rust) fill in
 * by hand, and `size` is a uint8_t that can hold up to 255 while `data` is only
 * PH_DIGEST_MAX_BYTES long. Every public function that reads a digest must check
 * this first, or a size of 200 reads past the end of the array. */
static inline int ph_digest_is_valid(const ph_digest_t *d) {
    return d != NULL && d->size <= PH_DIGEST_MAX_BYTES;
}

/* As above, plus "carries information". A zero-length digest has no bits to
 * compare, so distance/similarity functions reject it rather than reporting a
 * distance of 0 -- which would read as "identical". */
static inline int ph_digest_is_comparable(const ph_digest_t *d) {
    return ph_digest_is_valid(d) && d->size > 0;
}

/* Whether a digest may be compared with a metric meant for `kind`.
 *
 * PH_DIGEST_KIND_UNSPECIFIED passes everything: it is what a hand-filled struct holds,
 * since it is zero, and an FFI binding that never learned about the tag has to keep
 * working. Anything else must match, so that Hamming distance over quantised DCT
 * coefficients -- a plausible number that means nothing -- is refused rather than
 * returned. The tag never selects a metric; it only rules one out. */
static inline int ph_digest_kind_allows(const ph_digest_t *d, ph_digest_kind_t kind) {
    return d->kind == (uint8_t)PH_DIGEST_KIND_UNSPECIFIED || d->kind == (uint8_t)kind;
}

/* Both digests valid, non-empty, of equal size, and compatible with `kind`. */
static inline int ph_digests_comparable_as(const ph_digest_t *a, const ph_digest_t *b,
                                           ph_digest_kind_t kind) {
    return ph_digest_is_comparable(a) && ph_digest_is_comparable(b) && a->size == b->size &&
           ph_digest_kind_allows(a, kind) && ph_digest_kind_allows(b, kind);
}

/* Which of the colour histogram's bins a pixel falls in. Exposed for the conformance
 * test, which checks the quantisation against the axis definitions by hand. */
int ph_color_histogram_bin(int r, int g, int b);

float ph_get_pixel_bilinear(const uint8_t *img, int w, int h, float x, float y);
double ph_projection_variance(const uint8_t *img, int w, int h, double cx, double cy,
                              double max_radius, float cos_t, float sin_t, int samples);

/* Partial 1D DCT-II: computes the first `coeffs` coefficients of the `n`-element
 * signal `in`, orthonormally scaled -- X[0] = sum / sqrt(n), X[k>0] = sum * sqrt(2/n).
 * This is the transform the radial hash applies to the variance vector.
 *
 * Bounds: n >= 1 and coeffs in [1, n]. On violation nothing is written to `out` and
 * PH_ERR_INVALID_ARGUMENT is returned. */
PH_NODISCARD ph_error_t ph_dct1d_partial(const double *in, int n, int coeffs, double *out);

void ph_haar_1d_float(float *data, int n, float *temp);
void ph_haar_2d_level(float *data, int size, int stride, float *temp_row, float *temp_col);

const float *ph_get_dct_matrix_32(void);

/* Initializes the DCT matrix (thread-safe, idempotent) */
void init_dct_matrix(void);

/* EXIF Orientation (tag 0x0112) support. Only consulted when
 * ph_context_set_auto_orient() is enabled; degrades silently (returns 1, i.e.
 * "no transform needed") on any malformed/absent metadata rather than failing
 * the load — see src/image/orient.c. */
int ph_exif_orientation_from_jpeg(const uint8_t *data, size_t len);
int ph_exif_orientation_from_webp(const uint8_t *data, size_t len);
int ph_exif_orientation_from_png(const uint8_t *data, size_t len);

/* Applies one of the 8 EXIF orientation transforms (rotate/mirror) to a
 * decoded pixel buffer in place, reallocating *data and updating *width/
 * *height as needed (values 5-8 swap the dimensions). orientation 1 (or any
 * value outside 1..8) is a no-op. Leaves the image untouched on allocation
 * failure. */
void ph_apply_exif_orientation(uint8_t **data, int *width, int *height, int channels,
                               int orientation);

/*
 * Constants
 */
#define PH_DCT_SIZE 32
#define PH_DCT_REDUCTION_SIZE 8     // We use the top-left 8x8 coefficients
#define PH_CORE_HASH_SIZE 8         // Standard 8x8 grid for ahash/dhash/phash
#define PH_BLOCK_SIZE 16            // 16x16 grid for BMH (mHash uses a fixed 18x18)
#define PH_HAAR_SCALE 1.41421356237 // sqrt(2) for Haar wavelet normalization
/* Radial: 180 angles over [0, pi) -- the Radon transform is symmetric, so 180 covers the
 * whole circle -- reduced by a 1D DCT to 40 coefficients, which are the hash. Both
 * numbers come from the source (De Roover et al. via Zauner 3.1.3); before 2.0.0 the 40
 * sat on the angle count instead, which is a different algorithm. */
/* Marr-Hildreth. The construction is pHash's ph_mh_imagehash(); the operator it applies
 * is the Laplacian of Gaussian of Marr & Hildreth 1980. Every number here is that
 * implementation's, taken as a fact about the algorithm:
 *
 *   alpha = 2, level = 1  ->  kernel radius sigma = 4 * alpha^level = 8, so a 17x17 kernel
 *   the image is blurred at sigma 1, resized to 512x512 and equalised over 256 levels
 *   the response is summed over 16x16 blocks, giving a 31x31 grid (31 * 16 = 496 <= 512)
 *   3x3 windows of that grid, stride 4, give 8 * 8 = 64 windows of 9 values
 *   64 * 9 = 576 bits = 72 bytes
 */
#define PH_MH_ALPHA 2.0f
#define PH_MH_LEVEL 1.0f
#define PH_MH_BLUR_SIGMA 1.0f
#define PH_MH_EQUALIZE_LEVELS 256
#define PH_MH_GRID 31

/* The size the image is normalised to before filtering, and the resulting block size.
 * pHash fixes this at 512, which makes the blocks 16 pixels; both are tunable here
 * because the ratio between the kernel's scale and the block grid is the one thing in
 * this algorithm that actually decides what it sees, and 512 is not the best value for
 * it. The default is measured, not inherited -- see docs/algorithm-provenance.md. */
#define PH_MH_IMAGE_SIZE 512
#define PH_MH_MIN_IMAGE_SIZE (PH_MH_GRID * 2)
#define PH_MH_MAX_IMAGE_SIZE 4096
#define PH_MH_BLOCK_PIXELS 16
#define PH_MH_MAX_KERNEL_SIDE 65
#define PH_MH_WINDOW 3
#define PH_MH_WINDOW_STRIDE 4
#define PH_MH_WINDOWS_PER_AXIS 8
#define PH_MH_BITS (PH_MH_WINDOWS_PER_AXIS * PH_MH_WINDOWS_PER_AXIS * PH_MH_WINDOW * PH_MH_WINDOW)
#define PH_MH_BYTES (PH_MH_BITS / 8)

#define PH_RADIAL_PROJECTIONS 180
#define PH_RADIAL_COEFFS 40
#define PH_RADIAL_SAMPLES 128

/* Below this spread across the projection variances an image has no radial structure to
 * describe -- it is flat, or radially symmetric -- and the digest is all zeroes rather
 * than a standardisation of floating-point residue. No source specifies the value; it is
 * the one that was already in this code for the same job before 2.0.0, and it is one of
 * the constants R68 pins down. */
#define PH_RADIAL_FLAT_VARIANCE 0.001

/* Hard upper bounds for the pHash DCT: ph_dct2_partial() uses a fixed
 * 32*8 stack scratch buffer, and the resulting hash must fit into 64 bits
 * (reduction_size^2 <= 64). Anything above is rejected, never clamped. */
#define PH_DCT_MAX_SIZE 32
#define PH_DCT_MAX_REDUCTION_SIZE 8

/* R04 -- hard upper bounds for the remaining tunable parameters. Every one of them is
 * derived from a real limit of the implementation, not picked as a round number; the
 * derivation is spelled out next to each constant so a future change to a digest size
 * or a pixel ceiling shows up here as an inconsistency instead of silently widening
 * the accepted range. Out-of-range input is rejected by the setter
 * (PH_ERR_INVALID_ARGUMENT, configuration untouched) and never clamped: clamping is
 * the "silently wrong answer" anti-pattern that H5/M12 are about. */

/* BMH packs one bit per block, i.e. block_size^2 bits, into a ph_digest_t of at most
 * PH_DIGEST_MAX_BYTES bytes. At the 128 bytes of 2.0.0: 32*32 = 1024 bits = 128 bytes
 * fits exactly; 33*33 = 1089 bits = 137 bytes does not. The two _Static_asserts below
 * keep this tied to PH_DIGEST_MAX_BYTES rather than to the literal, which is how the
 * bound moved from 22 to 32 by itself when the digest grew.
 * block_size affects BMH only -- mHash has its own fixed geometry. */
#define PH_BLOCK_MAX_SIZE 32

/* Since 2.0.0 the projection count is the number of ANGLES, and the digest width no
 * longer follows it: the hash is always PH_RADIAL_COEFFS DCT coefficients. So the bound
 * is no longer the digest's capacity but the angular resolution beyond which more angles
 * carry no new information -- two neighbouring projections have to differ by at least one
 * pixel at the far end of the longest one. The largest square image the library will
 * process is 46340 x 46340 (PH_MAX_SUPPORTED_PIXELS), whose projection radius is
 * min(w,h)/2 = 23170 pixels; the finest useful angular step is therefore 1/23170 rad and
 * the useful angle count over [0, pi) is pi * 23170 = 72792. 131072 is the first power of
 * two above that. Same caveat as PH_RADIAL_MAX_SAMPLES: a degenerate strip could in
 * principle want more, and carries no radial structure to want it for.
 *
 * The lower bound is a hard one: a DCT of an n-element vector has n coefficients, so
 * fewer angles than PH_RADIAL_COEFFS cannot produce the hash at all. */
#define PH_RADIAL_MIN_PROJECTIONS PH_RADIAL_COEFFS
#define PH_RADIAL_MAX_PROJECTIONS 131072

/* Radial samples are taken along a straight line across the image, so more samples
 * than the image's diagonal add no information -- they only re-sample pixels already
 * visited. The largest square image the library will process has
 * floor(sqrt(PH_MAX_SUPPORTED_PIXELS)) = 46340 pixels per side (46341^2 exceeds
 * INT_MAX), and its diagonal is 46340 * sqrt(2) = 65534.66, so 65536 is the first
 * power of two at or above every diagonal that can occur for a square image.
 * Caveat, deliberately accepted: a degenerate strip (e.g. INT_MAX x 1) has a longer
 * diagonal while still fitting the pixel ceiling. Such aspect ratios carry no radial
 * structure to sample, so the bound is treated as the practical maximum rather than
 * being raised to INT_MAX for their sake. */
#define PH_RADIAL_MAX_SAMPLES 65536

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert((PH_BLOCK_MAX_SIZE * PH_BLOCK_MAX_SIZE + 7) / 8 <= PH_DIGEST_MAX_BYTES,
               "PH_BLOCK_MAX_SIZE bits must fit into a ph_digest_t");
/* The tag is a uint8_t holding a ph_digest_kind_t; keep the two from drifting apart. */
_Static_assert(PH_DIGEST_KIND_HISTOGRAM <= 255, "digest kinds must fit the tag byte");
_Static_assert(PH_MH_BYTES == 72, "the Marr-Hildreth hash is 576 bits");
_Static_assert(PH_MH_BYTES <= PH_DIGEST_MAX_BYTES, "the Marr-Hildreth hash must fit a digest");
_Static_assert(PH_MH_GRID *PH_MH_BLOCK_PIXELS <= PH_MH_IMAGE_SIZE,
               "the block grid must fit inside the normalised image");
_Static_assert(PH_MH_MIN_IMAGE_SIZE >= PH_MH_GRID,
               "the smallest normalised image must still hold one pixel per block");
_Static_assert((PH_MH_GRID - PH_MH_WINDOW) / PH_MH_WINDOW_STRIDE + 1 == PH_MH_WINDOWS_PER_AXIS,
               "the window count must follow from the grid and the stride");
_Static_assert(((PH_BLOCK_MAX_SIZE + 1) * (PH_BLOCK_MAX_SIZE + 1) + 7) / 8 > PH_DIGEST_MAX_BYTES,
               "PH_BLOCK_MAX_SIZE must be the largest block size that fits, not smaller");
_Static_assert(PH_RADIAL_COEFFS <= PH_DIGEST_MAX_BYTES,
               "the radial DCT coefficients must fit into a ph_digest_t");
_Static_assert(PH_RADIAL_PROJECTIONS >= PH_RADIAL_MIN_PROJECTIONS &&
                   PH_RADIAL_PROJECTIONS <= PH_RADIAL_MAX_PROJECTIONS,
               "the default angle count must be inside the accepted range");
#endif

#define PH_COLOR_MOMENTS 3
#define PH_COLOR_CHANNELS 3

/* ColorHash: the opponent colour axes of Swain & Ballard, quantised. The resolution is
 * this library's, chosen by measurement over sixteen candidates rather than by citation --
 * the paper could not be obtained. See the header of src/hashes/color_histogram.c and
 * docs/algorithm-provenance.md for the table and for why two higher-scoring candidates
 * were rejected. */
#define PH_COLOR_BINS_RG 6
#define PH_COLOR_BINS_BY 6
#define PH_COLOR_BINS_WB 3
#define PH_COLOR_BINS (PH_COLOR_BINS_RG * PH_COLOR_BINS_BY * PH_COLOR_BINS_WB)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(PH_COLOR_BINS <= PH_DIGEST_MAX_BYTES,
               "the colour histogram must fit a digest, one byte per bin");
#endif

#define PH_DEFAULT_GAMMA 2.2f
#define PH_GAMMA_EPSILON 0.001f

/* Upper bound on gamma, chosen as the reciprocal of PH_GAMMA_EPSILON so that the
 * exponent actually used by the LUT, 1.0/gamma, spans a range symmetric about 1.0:
 * [1/1000, 1000]. Measured LUT degeneracy is symmetric too -- the 256-entry LUT holds
 * 78 distinct values at gamma = 0.1 and 79 at gamma = 10, and 3 distinct values at
 * gamma = 0.001 (already accepted before 2.0.0) against 4 at gamma = 1000. So this
 * bound does not make the low and high ends behave differently; its job is to keep
 * 1.0/gamma a meaningful exponent and, together with the isfinite() check in
 * ph_context_set_gamma(), to reject the values that used to poison the whole LUT. */
#define PH_GAMMA_MAX 1000.0f

/* Upper bound on r + g + b in ph_context_set_gray_weights(). The weights are
 * normalized to sum to 128 via (w * 128) / sum, and with sum <= INT_MAX / 255 the
 * product w * 128 (w <= sum) stays well inside int. Expressed against 255 rather than
 * 128 so that the un-normalized weights are also safe to multiply by a full-range
 * 8-bit sample, should any future code path do so. */
#define PH_GRAY_WEIGHT_MAX_SUM ((long long)INT_MAX / 255)

/* Default cap on width*height before decoding a pixel buffer (decompression-bomb
 * protection). Overridable via ph_context_set_max_pixels(); 0 disables it. */
#define PH_DEFAULT_MAX_PIXELS ((uint64_t)256 * 1024 * 1024)

/* Grayscale Weights (standard ITU-R BT.601) scaled by 128 */
#define PH_GRAY_R 38
#define PH_GRAY_G 75
#define PH_GRAY_B 15

/* Gaussian Blur 3x3 Kernel Weights */
#define PH_GAUSS_K00 1
#define PH_GAUSS_K01 2
#define PH_GAUSS_K02 1
#define PH_GAUSS_K10 2
#define PH_GAUSS_K11 4
#define PH_GAUSS_K12 2
#define PH_GAUSS_K20 1
#define PH_GAUSS_K21 2
#define PH_GAUSS_K22 1
#define PH_GAUSS_SHIFT 4 // Divide by 16 (sum of weights)

/*
 * Safety Helpers
 */
/* Computes w * h * channels for an allocation size, refusing to silently wrap.
 * Returns 0 (and leaves *out untouched) if the product would overflow size_t;
 * returns 1 and sets *out to the byte count otherwise. */
static inline int ph_safe_image_alloc_size(uint64_t w, uint64_t h, uint64_t channels, size_t *out) {
    if (w == 0 || h == 0 || channels == 0)
        return 0;
    if (w > (uint64_t)SIZE_MAX / h)
        return 0;
    uint64_t wh = w * h;
    if (wh > (uint64_t)SIZE_MAX / channels)
        return 0;
    *out = (size_t)(wh * channels);
    return 1;
}

/* Hard ceiling on width * height that the implementation can process correctly,
 * independent of any user-configured max_pixels.
 *
 * Pixel *indexing* is still done in `int` across the hot loops (`y * w + x` in
 * image/filters.c, image/orient.c, hashes/radial.c, hashes/whash.c, hashes/phash.c),
 * so an image with more than INT_MAX pixels overflows those index computations --
 * signed overflow, i.e. undefined behaviour. Rather than leaving that reachable in a
 * documented mode, the library refuses such images outright. The default max_pixels
 * (256 MP) is eight times below this ceiling, so the ceiling only ever comes into play
 * when a caller deliberately raises or disables the limit.
 *
 * Raising this ceiling means converting that index arithmetic to size_t everywhere,
 * SIMD paths included -- see tasks/review/R48. */
#define PH_MAX_SUPPORTED_PIXELS ((uint64_t)INT_MAX)

/* Upper bound on a single image dimension, applied by every decode path.
 *
 * max_pixels bounds the *area*, which on its own permits an absurd aspect ratio: a
 * 268435456 x 1 image passes the default area limit exactly, yet makes a decoder size
 * a single row buffer of ~800 MB. libpng's own default per-dimension limit is 1000000,
 * so this matches it -- the point being that libphash must only ever *lower* that
 * limit, never raise it, which is what passing max_pixels straight into
 * png_set_user_limits() did.
 *
 * The cap is deliberately *not* derived from max_pixels: raising or disabling the area
 * limit must not raise the aspect-ratio ceiling with it. It lives here, next to the
 * area check, so that the verdict and the error code (PH_ERR_IMAGE_TOO_LARGE) are the
 * same for every format and in every build configuration -- before, it existed only
 * inside the native PNG backend, so a stb_image-only build had no dimension cap at all
 * and answered the same input with a different error. */
#define PH_MAX_IMAGE_DIMENSION 1000000u

/* Returns 1 if either dimension is beyond what the library will decode. Same contract
 * as ph_exceeds_pixel_limit(): `w` and `h` must each fit in 32 bits. */
static inline int ph_exceeds_dimension_limit(uint64_t w, uint64_t h) {
    return w > PH_MAX_IMAGE_DIMENSION || h > PH_MAX_IMAGE_DIMENSION;
}

/* Returns 1 if decoding a w x h image is disallowed.
 *
 * Two limits apply, and the stricter one wins:
 *   - the caller's max_pixels, where 0 means "no limit of my own";
 *   - PH_MAX_SUPPORTED_PIXELS, which always applies -- including when max_pixels is 0
 *     or set above it. `0` therefore means "the implementation's limit", not "no limit".
 *
 * Contract (L4): `w` and `h` must each fit in 32 bits. Every caller feeds it either an
 * `int` dimension (already made non-negative -- see ph_abs_dim()) or a `png_uint_32`,
 * so `w * h` is at most 2^64 - 2^33 + 1 and cannot wrap the uint64_t product. Do NOT
 * call this with values wider than 32 bits without adding an overflow check first. */
static inline int ph_exceeds_pixel_limit(uint64_t w, uint64_t h, uint64_t max_pixels) {
    uint64_t pixels = w * h;
    if (pixels > PH_MAX_SUPPORTED_PIXELS)
        return 1;
    if (max_pixels == 0)
        return 0;
    return pixels > max_pixels;
}

/* Truncating copy into a fixed-size diagnostic buffer (err_msg may be NULL/zero-size,
 * meaning the caller isn't collecting a message). Never allocates. */
static inline void ph_set_err_msg(char *err_msg, size_t err_msg_cap, const char *msg) {
    if (!err_msg || err_msg_cap == 0 || !msg)
        return;
    size_t i = 0;
    for (; i + 1 < err_msg_cap && msg[i] != '\0'; i++)
        err_msg[i] = msg[i];
    err_msg[i] = '\0';
}

/* Max length (including NUL) of the diagnostic message stashed by a failed load. */
#define PH_LAST_ERROR_MAX 160

/* Internal Context Structure */
struct ph_context {
    // Uploaded image data
    struct {
        uint8_t *raw_rgb;
        uint8_t *gray_cache;
        int width;
        int height;
        int channels;
        int is_loaded;
    } image;

    // Diagnostic message for the most recent failed load; empty string if none.
    char last_error[PH_LAST_ERROR_MAX];

    // User-defined configuration parameters
    struct {
        float gamma;
        uint8_t gamma_lut[256];
        int gray_r, gray_g, gray_b;
        int load_grayscale;
        int auto_orient; // Off by default: see ph_context_set_auto_orient().

        // Various tunings for hashes
        float mhash_alpha;
        float mhash_level;
        int mhash_size;
        int phash_dct_size;
        int phash_reduction_size;
        int radial_projections;
        int radial_samples;
        int block_size;
        ph_whash_mode_t whash_mode;

        // 0 = no caller limit; PH_MAX_SUPPORTED_PIXELS still applies. Otherwise the max
        // allowed width*height before decoding a pixel buffer.
        uint64_t max_pixels;
    } config;

    // System data of the allocator (Arena)
    struct {
        uint8_t *buffer;
        size_t capacity;
        size_t offset;
    } arena;
};

/* Ensures the context's scratchpad is at least 'size' bytes.
 * Returns NULL on failure, pointer to buffer on success. */
uint8_t *ph_get_scratchpad(ph_context_t *ctx, size_t size);

#endif /* INTERNAL_H */
