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
#    define M_PI 3.14159265358979323846
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
 * 256 times fewer multiply-adds, with exact integer box sums. Edges replicate. */
void ph_mh_block_sums(const uint8_t *img, int n, int block, const float *kernel, int side,
                      uint8_t *scratch, float *out);

typedef struct {
    double mean;
    double std_dev;
    double skew;
} ph_channel_moments_t;

/* The mean, standard deviation and skewness of each of the first three channels of an
 * interleaved image with `channels` bytes per pixel (3 or more; any further channel is
 * skipped), into out[0..PH_COLOR_CHANNELS - 1]. All zero when data is NULL, num_pixels is
 * 0 or channels < 3. num_pixels is a size_t on purpose: it is width * height, which does
 * not fit an int for images above ~46340x46340. */
void ph_compute_moments(const uint8_t *data, size_t num_pixels, int channels,
                        ph_channel_moments_t out[]);

/* Partial 2D DCT: computes the top-left reduction_size x reduction_size block.
 *
 * Bounds are hard limits, not hints: dct_size must be in [1, PH_DCT_MAX_SIZE] and
 * reduction_size in [1, PH_DCT_MAX_REDUCTION_SIZE] and <= dct_size. On violation the
 * function writes nothing to `out` and returns PH_ERR_INVALID_ARGUMENT — callers MUST
 * check the result, otherwise `out` stays whatever it was. */
PH_NODISCARD ph_error_t ph_dct2_partial(const float *dct_mat, const uint8_t *input, int dct_size,
                                        int reduction_size, float *out);

uint64_t ph_median_bitpack(const float *values, int n);

/* As above, but the median is taken over values[median_from..n-1] only, while every one
 * of the n values still gets a bit. pHash thresholds its 8x8 DCT block against the median
 * of its 63 AC coefficients, leaving the DC term out of the decision but not out of the
 * hash; median_from = 1 is that. median_from = 0 is ph_median_bitpack(). */
uint64_t ph_median_bitpack_from(const float *values, int n, int median_from);

/* As ph_median_bitpack_from(), with the threshold raised above the median by `margin`
 * times the range (maximum minus minimum) of values[median_from..n-1]: a value within that
 * margin above the median gets a 0 like the values below it. */
uint64_t ph_median_bitpack_margin(const float *values, int n, int median_from, float margin);

/* pHash's threshold margin, as a fraction of the AC coefficients' range. With a plain
 * median, an image with little low-frequency structure has many AC coefficients crowding
 * the median, and those bits follow whatever nudges them: +/-1 grey level of noise moves
 * pHash by 11 bits or more on 140 of 800 photographs. A margin sends that crowd to 0 as a
 * block. Measured over 800 photographs and the synthetic corpus of
 * tests/src/test_hash_properties.c:
 *
 *   margin    photographs: separability, false matches at 95% recall, 11+ bits from noise
 *   0              3.67        14.2%        140
 *   0.001          4.00         6.8%         98
 *   0.002          4.01         6.5%         92
 *   0.005          3.77         8.2%         75
 *
 * The synthetic corpus agrees (separability 2.69 -> 3.17 at 0.001). 0.001 takes nearly all
 * of the gain; larger margins start clearing bits that carry structure. The cost: three
 * near-flat synthetic images of 24 share a hash, where their 64 bits were rounding noise. */
#define PH_PHASH_MEDIAN_MARGIN 0.001f

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

#define PH_HASH_FLAGS_ALL     (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH)
#define PH_DCT_SIZE           32
#define PH_DCT_REDUCTION_SIZE 8             // We use the top-left 8x8 coefficients
#define PH_CORE_HASH_SIZE     8             // Standard 8x8 grid for ahash/dhash/phash
#define PH_BLOCK_SIZE         16            // 16x16 grid for BMH
#define PH_HAAR_SCALE         1.41421356237 // sqrt(2) for Haar wavelet normalization
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
#define PH_MH_ALPHA           2.0f
#define PH_MH_LEVEL           1.0f
#define PH_MH_BLUR_SIGMA      1.0f
#define PH_MH_EQUALIZE_LEVELS 256
#define PH_MH_GRID            31

/* The size the image is normalised to before filtering, and the resulting block size.
 * pHash fixes this at 512, which makes the blocks 16 pixels; the size is tunable here
 * because the ratio between the kernel's scale and the block grid is the one thing in
 * this algorithm that decides what it sees. The default is pHash's: a sweep of the
 * alternatives finds none better -- see docs/algorithm-provenance.md. */
#define PH_MH_IMAGE_SIZE       512
#define PH_MH_MIN_IMAGE_SIZE   (PH_MH_GRID * 2)
#define PH_MH_MAX_IMAGE_SIZE   4096
#define PH_MH_BLOCK_PIXELS     16
#define PH_MH_MAX_KERNEL_SIDE  65
#define PH_MH_WINDOW           3
#define PH_MH_WINDOW_STRIDE    4
#define PH_MH_WINDOWS_PER_AXIS 8
#define PH_MH_BITS  (PH_MH_WINDOWS_PER_AXIS * PH_MH_WINDOWS_PER_AXIS * PH_MH_WINDOW * PH_MH_WINDOW)
#define PH_MH_BYTES (PH_MH_BITS / 8)

#define PH_RADIAL_PROJECTIONS 180
#define PH_RADIAL_COEFFS      40
#define PH_RADIAL_SAMPLES     128

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

/* When an image has no angular structure for Radial to describe. No source specifies it;
 * both bounds are this library's, set by measurement. The projection variances are in
 * grey levels squared.
 *
 * PH_RADIAL_MIN_MEAN_VARIANCE: below it no line through the image sees any change in
 * brightness at all. A flat image leaves bilinear-interpolation residue around 1e-8; one
 * pixel differing by a single level along one projection already gives about 1e-2.
 *
 * PH_RADIAL_MIN_RELATIVE_SPREAD: the variance profile's spread across angles, squared and
 * relative to its mean squared (the squared coefficient of variation). Standardising a
 * profile that barely varies amplifies rounding noise into the digest. Measured by
 * comparing each image's digest with that of the same image plus +/-2 levels of noise
 * (800 photographs, the 24 synthetic bases of tests/src/test_hash_properties.c, 40 discs):
 *
 *   relative spread    mean correlation with the noisy copy
 *   below 1e-5             0.40 - 0.43
 *   1e-5 .. 1e-4           0.68
 *   1e-4 .. 1e-3           0.99
 *   1e-3 and above         0.94 - 0.99
 *
 * Below 1e-4 the digest describes noise, not the image. Being relative, the bound does
 * not depend on contrast: two faint patterns with different orientations keep different
 * digests, where a bound on the absolute spread would give both the flat answer. */
#define PH_RADIAL_MIN_MEAN_VARIANCE   1e-6
#define PH_RADIAL_MIN_RELATIVE_SPREAD 1e-4

/* Hard upper bounds for the pHash DCT: ph_dct2_partial() uses a fixed
 * 32*8 stack scratch buffer, and the resulting hash must fit into 64 bits
 * (reduction_size^2 <= 64). Anything above is rejected, never clamped. */
#define PH_DCT_MAX_SIZE           32
#define PH_DCT_MAX_REDUCTION_SIZE 8

/* Hard lower bound for reduction_size. The DC coefficient takes no part in choosing the
 * threshold (ph_median_bitpack_margin(dct_out, n = reduction_size^2, median_from = 1, ...))
 * and its own bit is always set, so a block of r x r coefficients leaves r^2 - 1 bits that
 * carry information. The bound is where the hash stops being
 * degenerate, measured over 400 photographs (at most 350 distinct at r = 8: the rest are
 * near-duplicates) and the synthetic corpus of tests/src/test_hash_properties.c:
 *
 *   r   AC bits   distinct photos   false-match rate at 95% true matches
 *   2      3            4                1.00
 *   3      8           70                0.74
 *   4     15          304                0.40
 *   5     24          329                0.35
 *   8     63          350                0.14
 *
 * At 2 and 3 most unrelated images collide; 4 is the first size at which nearly every
 * distinct image gets its own hash. Above it the gain is gradual, a trade of length for
 * precision that stays the caller's choice. 1 would leave no AC bit at all. */
#define PH_DCT_MIN_REDUCTION_SIZE 4

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

/* Upper bounds for the Radial projection grid. The work is projections * samples bilinear
 * samples (about 3 ns each), independent of the image size, and the digest is always
 * PH_RADIAL_COEFFS coefficients, so past a few thousand of either nothing is gained.
 * Measured on tests/data/photo.jpeg and photo_complex.png (400 x 400), digest distance
 * (squared L2 over the 40 bytes) against a 16384 x 16384 reference:
 *
 *   projections x samples   time      squared L2 to the reference
 *     180 x  128 (default)   0.6 ms    147 / 272
 *    1440 x 1024             5.5 ms      7 /  17
 *    4096 x 4096              56 ms      4 /   2
 *
 * 4096 x 4096 is already within one unit per coefficient of a grid 16 times as fine, and its cost,
 * 56 ms, is that of decoding a large JPEG. A higher ceiling would buy no information and would let
 * a configuration taken from an untrusted source spend seconds of CPU on one call, which cannot be
 * cancelled.
 *
 * The lower bound on projections is a hard one: a DCT of an n-element vector has n
 * coefficients, so fewer angles than PH_RADIAL_COEFFS cannot produce the hash at all. */
#define PH_RADIAL_MIN_PROJECTIONS PH_RADIAL_COEFFS
#define PH_RADIAL_MAX_PROJECTIONS 4096
#define PH_RADIAL_MAX_SAMPLES     4096

/* Hard lower bound for samples. Variance is undefined-in-effect for a single
 * observation: with one sample per projection, every projection's variance is exactly 0
 * by definition, which is the no-structure condition PH_RADIAL_MIN_MEAN_VARIANCE exists to
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
_Static_assert(PH_RADIAL_SAMPLES >= PH_RADIAL_MIN_SAMPLES &&
                   PH_RADIAL_SAMPLES <= PH_RADIAL_MAX_SAMPLES,
               "the default sample count must be inside the accepted range");
_Static_assert(PH_DCT_REDUCTION_SIZE >= PH_DCT_MIN_REDUCTION_SIZE &&
                   PH_DCT_REDUCTION_SIZE <= PH_DCT_MAX_REDUCTION_SIZE,
               "the default pHash reduction size must be inside the accepted range");
#endif

#define PH_COLOR_MOMENTS  3
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
#define PH_COLOR_MOMENT_SCALE         128
#define PH_COLOR_MOMENT_BYTES         2
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
#define PH_COLOR_BINS    (PH_COLOR_BINS_RG * PH_COLOR_BINS_BY * PH_COLOR_BINS_WB)

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

/* ph_color_histogram_bin() as a sum of three per-axis terms, one table per axis indexed by
 * the shifted axis value: rg[] holds a * BINS_BY * BINS_WB, by[] holds c * BINS_WB, wb[]
 * holds w. ph_color_bin_lookup() equals ph_color_histogram_bin() for every 8-bit colour. */
typedef struct {
    uint8_t rg[PH_COLOR_RG_VALUES];
    uint8_t by[PH_COLOR_BY_VALUES];
    uint8_t wb[PH_COLOR_WB_VALUES];
} ph_color_bin_table_t;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(PH_COLOR_BINS <= 256, "a bin index must fit the tables' uint8_t entries");
#endif

void ph_color_bin_table_init(ph_color_bin_table_t *t);

static inline int ph_color_bin_lookup(const ph_color_bin_table_t *t, int r, int g, int b) {
    return t->rg[r - g + PH_COLOR_RG_OFFSET] + t->by[2 * b - r - g + PH_COLOR_BY_OFFSET] +
           t->wb[r + g + b];
}

#endif /* PH_HASHES_HASHES_H */
