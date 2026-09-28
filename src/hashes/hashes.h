#ifndef PH_HASHES_HASHES_H
#define PH_HASHES_HASHES_H

/* What the algorithms in src/hashes/ share: the digest shape, the transforms and
 * statistics more than one of them (or a test) calls, and every algorithm's constants
 * and parameter bounds. The constants live in one header rather than one per algorithm
 * because several of the static assertions below relate two algorithms' values. */

#include "context.h"
#include "libphash.h"
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* M_PI is not ISO C -- it comes from POSIX/XSI, and the project compiles as strict
 * ISO (CMAKE_C_EXTENSIONS OFF => -std=c17, not -std=gnu17). Under a strict dialect
 * the compiler defines __STRICT_ANSI__, glibc hides every non-ISO name behind it,
 * and the three call sites in phash.c/radial.c stop compiling; MSVC's <math.h>
 * never declares it at all without _USE_MATH_DEFINES. Darwin declares it
 * unconditionally; glibc and MSVC do not. Defining it here,
 * after <math.h>, makes the constant a property of this codebase rather than of the
 * libc it happens to be built against -- the guard keeps the libc's own definition
 * when there is one. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The one place a digest's size and kind are decided (src/hashes/algorithm.c).
 * ph_digest_info() reports it, and every ph_compute_* that returns a digest starts from
 * ph_digest_begin(), so the two cannot disagree. `algo` must be a valid ph_algorithm_t. */
void ph_digest_shape(const struct ph_context_config *config, ph_algorithm_t algo, uint8_t *size,
                     uint8_t *kind);

/* Zeroes *out and sets its size and kind from ph_digest_shape(). */
void ph_digest_begin(ph_digest_t *out, const ph_context_t *ctx, ph_algorithm_t algo);

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
 * int for images above ~46340x46340. */
ph_channel_moments_t ph_compute_moments(const uint8_t *data, size_t num_pixels, int channels,
                                        int channel_index);

/* Partial 2D DCT: computes the top-left reduction_size x reduction_size block.
 *
 * Bounds are hard limits, not hints: dct_size must be in [1, PH_DCT_MAX_SIZE] and
 * reduction_size in [1, PH_DCT_MAX_REDUCTION_SIZE] and <= dct_size. On violation the
 * function writes nothing to `out` and returns PH_ERR_INVALID_ARGUMENT — callers MUST
 * check the result, otherwise `out` stays whatever it was. */
PH_NODISCARD ph_error_t ph_dct2_partial(const float *dct_mat, const uint8_t *input, int dct_size,
                                        int reduction_size, float *out);

/* Same contract as ph_dct2_partial(), but always the scalar path, even on a build with a
 * SIMD-capable target. Exists only so tests/src/test_simd_equivalence.c can compare the
 * two against each other; production code should call ph_dct2_partial(). */
PH_NODISCARD ph_error_t ph_dct2_partial_scalar(const float *dct_mat, const uint8_t *input,
                                               int dct_size, int reduction_size, float *out);

uint64_t ph_median_bitpack(const float *values, int n);

/* As above, but the median is taken over values[median_from..n-1] only, while every one
 * of the n values still gets a bit. pHash thresholds its 8x8 DCT block against the median
 * of its 63 AC coefficients, leaving the DC term out of the decision but not out of the
 * hash; median_from = 1 is that. median_from = 0 is ph_median_bitpack(). */
uint64_t ph_median_bitpack_from(const float *values, int n, int median_from);

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
void ph_haar_1d_inverse_float(float *data, int n, float *temp);
void ph_haar_2d_level_inverse(float *data, int size, int stride, float *temp_row, float *temp_col);

const float *ph_get_dct_matrix_32(void);

#define PH_HASH_FLAGS_ALL (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH)
#define PH_DCT_SIZE 32
#define PH_DCT_REDUCTION_SIZE 8     // We use the top-left 8x8 coefficients
#define PH_CORE_HASH_SIZE 8         // Standard 8x8 grid for ahash/dhash/phash
#define PH_BLOCK_SIZE 16            // 16x16 grid for BMH
#define PH_HAAR_SCALE 1.41421356237 // sqrt(2) for Haar wavelet normalization
/* Radial: 180 angles over [0, pi) -- the Radon transform is symmetric, so 180 covers the
 * whole circle -- reduced by a 1D DCT to 40 coefficients, which are the hash. Both
 * numbers come from the source (De Roover et al. via Zauner 3.1.3). */
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

/* Default Gaussian-blur sigma for Radial, aligned on pHash's own header default
 * (ph_compare_images(), aetilius/pHash), not on Zauner's Diplomarbeit, which reports "the
 * authors suggest 1 for both variables" (sigma and gamma) and is contradicted by pHash's
 * own default here. See docs/algorithm-provenance.md section 7. */
#define PH_RADIAL_DEFAULT_SIGMA 3.5f

/* Upper bound on radial sigma: ph_gaussian_blur_sigma() (src/image/filters.c) derives its
 * kernel radius as ceil(3*sigma) and silently clamps it to 64 rather than growing the
 * fixed-size kernel array further. A sigma above this bound would be silently narrower
 * than requested -- exactly the clamping the setters refuse to do -- so the setter
 * rejects it instead. Lower bound is a bare `> 0.0f`: that same function leaves `dst`
 * entirely unwritten for a non-positive sigma (a precondition, not a clamp), so 0 or
 * negative values must never reach it. */
#define PH_RADIAL_MAX_SIGMA (64.0f / 3.0f)

/* Below this spread across the projection variances an image has no radial structure to
 * describe -- it is flat, or radially symmetric -- and the digest is all zeroes rather
 * than a standardisation of floating-point residue. No source specifies the value; it is
 * this library's choice: small enough that no real image's projection variance falls
 * under it, large enough to catch the residue a flat image leaves. */
#define PH_RADIAL_FLAT_VARIANCE 0.001

/* Hard upper bounds for the pHash DCT: ph_dct2_partial() uses a fixed
 * 32*8 stack scratch buffer, and the resulting hash must fit into 64 bits
 * (reduction_size^2 <= 64). Anything above is rejected, never clamped. */
#define PH_DCT_MAX_SIZE 32
#define PH_DCT_MAX_REDUCTION_SIZE 8

/* Hard lower bound for reduction_size. The DC coefficient is
 * excluded from the hash: ph_median_bitpack_from(dct_out, n=reduction_size^2, median_from=1)
 * skips the first (DC) coefficient and thresholds only the AC ones. At
 * reduction_size == 1 there is exactly one coefficient (the DC one), all of it is
 * skipped, and the hash is the fixed 64-bit value 0 for every image -- content-independent
 * by construction, not by a bad input. 2 is the smallest value that leaves at least one
 * AC coefficient (2^2 - 1 = 3) for the hash to depend on. */
#define PH_DCT_MIN_REDUCTION_SIZE 2

/* Hard upper bounds for the remaining tunable parameters. Every one of them is
 * derived from a real limit of the implementation, not picked as a round number; the
 * derivation is spelled out next to each constant so a future change to a digest size
 * or a pixel ceiling shows up here as an inconsistency instead of silently widening
 * the accepted range. Out-of-range input is rejected by the setter
 * (PH_ERR_INVALID_ARGUMENT, configuration untouched) and never clamped: clamping would
 * let the caller hash with a parameter it never asked for, silently and without an
 * error it could check. */

/* BMH packs one bit per block, i.e. block_size^2 bits, into a ph_digest_t of at most
 * PH_DIGEST_MAX_BYTES bytes. At PH_DIGEST_MAX_BYTES = 128: 32*32 = 1024 bits = 128 bytes
 * fits exactly; 33*33 = 1089 bits = 137 bytes does not. The two _Static_asserts below
 * tie the bound to PH_DIGEST_MAX_BYTES rather than to a literal.
 * block_size affects BMH only -- mHash has its own parameters. */
#define PH_BLOCK_MAX_SIZE 32

/* Hard lower bound for block_size. At block_size == 1 the grid is a single block
 * whose mean is itself; the "median" of one value is that same value, the threshold is
 * ">= median", so the comparison is always true and the one output bit is always 1 --
 * digest 0x01 for every image, regardless of content. 2 is the smallest grid that can
 * produce blocks with different means and therefore a content-dependent bit pattern. */
#define PH_BLOCK_MIN_SIZE 2

/* The projection count is the number of ANGLES; the digest is always PH_RADIAL_COEFFS
 * DCT coefficients. So the bound is the angular resolution beyond which more angles
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

/* Hard lower bound for samples. Variance is undefined-in-effect for a single
 * observation: with one sample per projection, every projection's variance is exactly 0
 * by definition, which is the same all-flat condition PH_RADIAL_FLAT_VARIANCE exists to
 * catch -- so at samples == 1 the digest is all zeroes for every image,
 * independent of content. 2 is the smallest sample count for which a projection's variance
 * can be nonzero. */
#define PH_RADIAL_MIN_SAMPLES 2

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

/* ColorMoments: each moment is a signed 16-bit fixed-point number, big-endian, in units
 * of 1/PH_COLOR_MOMENT_SCALE. Two bytes rather than one because the third moment carries
 * a sign -- the direction of the asymmetry -- which a single unsigned byte cannot hold.
 *
 * The scale is 128 by measurement, not by taste: over every distribution an 8-bit channel
 * admits, the extremes are a mean of 255, a standard deviation of 127.5 and a skewness of
 * +/-116.85 (two-point distributions are extremal for all three). 255 is therefore the
 * largest magnitude any moment can take, and 128 is the largest power of two with
 * 255 * scale <= INT16_MAX: the encoding covers the whole attainable range with nothing
 * to clamp, at a resolution of 1/128. */
#define PH_COLOR_MOMENT_SCALE 128
#define PH_COLOR_MOMENT_BYTES 2
#define PH_COLOR_MOMENTS_DIGEST_BYTES (PH_COLOR_CHANNELS * PH_COLOR_MOMENTS * PH_COLOR_MOMENT_BYTES)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(PH_COLOR_MOMENTS_DIGEST_BYTES <= PH_DIGEST_MAX_BYTES,
               "the colour moments must fit a digest");
/* 255 is the largest magnitude a moment of an 8-bit channel can reach; if the scale ever
 * grows past the point where that still encodes, the encoding starts clamping silently. */
_Static_assert(255 * PH_COLOR_MOMENT_SCALE <= INT16_MAX,
               "the fixed-point scale must keep every attainable moment inside int16");
#endif

/* ColorHash: the opponent colour axes of Swain & Ballard, quantised. The resolution is
 * this library's, chosen by measurement over sixteen candidates: the paper is implemented
 * from secondary descriptions and supplies none that fits a digest. See the header of
 * src/hashes/color_histogram.c and docs/algorithm-provenance.md for the table and for why two
 * higher-scoring candidates were rejected. */
#define PH_COLOR_BINS_RG 6
#define PH_COLOR_BINS_BY 6
#define PH_COLOR_BINS_WB 3
#define PH_COLOR_BINS (PH_COLOR_BINS_RG * PH_COLOR_BINS_BY * PH_COLOR_BINS_WB)

/* The opponent axes' exact ranges for 8-bit channels: rg = r - g in [-255, 255],
 * by = 2b - r - g in [-510, 510], wb = r + g + b in [0, 765]. Each axis is shifted to
 * start at 0 and divided into bins over its count of distinct values, so the whole span
 * is used and nothing falls outside it. */
#define PH_COLOR_RG_OFFSET 255
#define PH_COLOR_RG_VALUES (2 * 255 + 1)
#define PH_COLOR_BY_OFFSET 510
#define PH_COLOR_BY_VALUES (2 * 510 + 1)
#define PH_COLOR_WB_VALUES (3 * 255 + 1)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(PH_COLOR_BINS <= PH_DIGEST_MAX_BYTES,
               "the colour histogram must fit a digest, one byte per bin");
#endif

#endif /* PH_HASHES_HASHES_H */
