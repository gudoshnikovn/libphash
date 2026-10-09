#ifndef PH_IMAGE_IMAGE_H
#define PH_IMAGE_IMAGE_H

/* Pixel-level primitives (src/image/): grayscale conversion, resampling, blur,
 * gamma, histogram equalisation and EXIF orientation. */

#include "libphash.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/* Converts RGB/RGBA to Grayscale with custom weights */
void ph_to_grayscale(const ph_context_t *ctx, const uint8_t *src, int w, int h, int channels,
                     uint8_t *dst);

/* Same contract as ph_to_grayscale(), but always the scalar path, even on a build with a
 * SIMD-capable target. Exists only so tests/src/test_simd_equivalence.c can compare the
 * two against each other; production code should call ph_to_grayscale(). */
void ph_to_grayscale_scalar(const ph_context_t *ctx, const uint8_t *src, int w, int h, int channels,
                            uint8_t *dst);

/* A decoder's grayscale: 8-bit RGB (3 channels) to gray, or RGBA (4) to gray + alpha, with
 * the default weights (PH_GRAY_R/G/B) whatever the context's -- the fold the native
 * decoders without a grayscale output of their own apply, so every backend's gray is the
 * same bytes. `src` and `dst` must not overlap. */
void ph_fold_to_gray(const uint8_t *restrict src, uint8_t *restrict dst, size_t num_pixels,
                     int channels);

/* Same contract as ph_fold_to_gray(), always the scalar path; for
 * tests/src/test_simd_equivalence.c only. */
void ph_fold_to_gray_scalar(const uint8_t *restrict src, uint8_t *restrict dst, size_t num_pixels,
                            int channels);

/* Removes the alpha channel of a freshly decoded or copied image, in place: 4 channels
 * (RGBA) become 3, 2 (gray + alpha) become 1; 1 and 3 are left alone. Every mode but
 * PH_ALPHA_IGNORE composites onto its background, per channel,
 * (c * a + bg * (255 - a) + 127) / 255. *pixels may be shrunk to the new size; if that
 * fails the block stays valid, merely oversized. */
void ph_resolve_alpha(uint8_t **pixels, size_t num_pixels, int *channels, ph_alpha_mode_t mode);

/* Area-average downscale of the context's gray image to dw x dh: each output pixel is the
 * mean of the source area it covers, a partially covered source pixel counting by the
 * fraction covered, rounded once. Computed in integers, so the result is exact and the
 * same on every platform. When the image is at least PH_AREA_GRID on both sides and both
 * dw and dh divide PH_AREA_GRID, the answer is assembled from one cached pass over the
 * image (the area sums on the PH_AREA_GRID grid) and is bit for bit what a direct pass
 * would give: an average of equal-area cells is the average of their union. Returns 1 on
 * success, 0 on an allocation failure (dst untouched). */
int ph_area_downscale(ph_context_t *ctx, int dw, int dh, uint8_t *dst);

/* Resizes a grayscale image using box sampling (averaging). Returns 1 on success, 0 if
 * the dimensions are degenerate or the underlying stb resize failed to allocate --
 * either way `dst` is left untouched and the caller must not read it. */
int ph_resize_box(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

/* Same contract as ph_resize_box(): returns 1 on success, 0 (dst untouched) otherwise. */
int ph_resize_mitchell(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

/* Applies gamma correction, normalised by the buffer's own maximum, default gamma=1.0
 * (identity). See the implementation in src/image/color.c for the exact formula. */
void ph_apply_gamma(const ph_context_t *ctx, uint8_t *data, int w, int h);

/* Separable Gaussian blur at an arbitrary sigma, truncated at three standard deviations.
 * `scratch` is w*h floats supplied by the caller; this allocates nothing. */
void ph_gaussian_blur_sigma(const uint8_t *src, int w, int h, float sigma, float *scratch,
                            uint8_t *dst);

/* Histogram equalisation over `levels` buckets (2..256), in place. */
void ph_equalize_histogram(uint8_t *data, size_t n, int levels);

/* The loaded image in grayscale: the image itself when it has one channel, otherwise a
 * conversion made on first use and cached on the context until the image changes. NULL
 * when nothing is loaded or the conversion cannot be allocated. */
uint8_t *ph_get_gray(ph_context_t *ctx);

/* Frees the cached grayscale conversion. Whoever replaces or drops the loaded image
 * calls this; the cache belongs to src/image/color.c, which makes it. */
void ph_drop_gray_cache(ph_context_t *ctx);

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
 * value outside 1..8) is a no-op and PH_SUCCESS.
 *
 * On failure -- PH_ERR_ALLOCATION_FAILED for the output buffer,
 * PH_ERR_IMAGE_TOO_LARGE when its size overflows, PH_ERR_INVALID_ARGUMENT for a
 * NULL pointer or channels <= 0 -- the image is left untouched, i.e. still in
 * the stored orientation. That is not a usable result: a caller that asked for
 * the orientation must treat it as a failed load, not hash the unrotated image. */
ph_error_t ph_apply_exif_orientation(uint8_t **data, int *width, int *height, int channels,
                                     int orientation);

/* Grayscale weights: ITU-R BT.601 luma coefficients (0.299/0.587/0.114), approximated
 * as 38/75/15 over 128. None of the nine algorithms' primary sources specify a
 * grayscale formula at all; BT.601 is cited as an external standard
 * because it is one, not because anything here points to it.
 *
 * The canonical 8-bit triple 77/150/29 over 256 is closer to the real-valued BT.601
 * coefficients on every channel and needs no rounding in the sum, but it separates copies
 * from different images no better: the same on photographs, a little worse for aHash,
 * wHash, BMH and mHash on synthetic images (measured in docs/theory/preparation.md). A
 * closer decimal approximation does not track discrimination, so this triple is kept. */
#define PH_GRAY_R 38
#define PH_GRAY_G 75
#define PH_GRAY_B 15

/* Grayscale weights are integers over PH_GRAY_WEIGHT_SCALE, and the conversion divides by
 * it with a right shift: every pixel is (r*wr + g*wg + b*wb) >> PH_GRAY_WEIGHT_SHIFT.
 * ph_context_set_gray_weights() normalises a caller's weights to this scale. */
#define PH_GRAY_WEIGHT_SHIFT 7
#define PH_GRAY_WEIGHT_SCALE (1 << PH_GRAY_WEIGHT_SHIFT)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(PH_GRAY_R + PH_GRAY_G + PH_GRAY_B == PH_GRAY_WEIGHT_SCALE,
               "the default grayscale weights must sum to the weight scale");
#endif

/* Aligned on pHash's own default (ph_compare_images(), aetilius/pHash), which is
 * an identity transform: pow(v, 1.0) == v. Gamma raises pixels to `gamma` directly
 * (pHash's convention, not `1.0/gamma`) and normalises the buffer by its own maximum
 * before the power step and rescales by the same maximum after, so a gamma of 1.0 is
 * exactly a no-op regardless of image content (see ph_apply_gamma(),
 * src/image/color.c). docs/algorithm-provenance.md section 7 has the measurements. */
#define PH_DEFAULT_GAMMA 1.0f
#define PH_GAMMA_EPSILON 0.001f

/* Upper bound on gamma, which is the exponent applied to a pixel (see PH_DEFAULT_GAMMA
 * above). With PH_GAMMA_EPSILON it keeps the exponent inside [0.001, 1000], a range
 * symmetric about 1.0 in log scale; ph_context_set_gamma()'s isfinite() check rejects
 * NaN and infinity. */
#define PH_GAMMA_MAX 1000.0f

/* Upper bound on r + g + b in ph_context_set_gray_weights(). The weights are
 * normalized to sum to 128 via (w * 128) / sum, and with sum <= INT_MAX / 255 the
 * product w * 128 (w <= sum) stays well inside int. Expressed against 255 rather than
 * 128 so that the un-normalized weights are also safe to multiply by a full-range
 * 8-bit sample, should any future code path do so. */
#define PH_GRAY_WEIGHT_MAX_SUM ((long long)INT_MAX / 255)

#endif /* PH_IMAGE_IMAGE_H */
