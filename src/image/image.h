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

uint8_t *ph_get_gray(ph_context_t *ctx);

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
 * Measurement checked switching to the canonical 8-bit triple 77/150/29 over 256, which is
 * closer to the real-valued BT.601 coefficients on every channel
 * (+0.0018/-0.0011/-0.0007 vs. this triple's -0.0021/-0.0011/+0.0032) and whose
 * denominator is an exact power of two with no rounding in the sum. On this library's
 * measured separability corpus (test_hash_properties.c) it is not an improvement: BMH
 * drops from 5.24 to 4.97 and wHash from 4.34 to 4.27, while aHash, dHash, pHash and
 * mHash move by less than the run-to-run noise floor. A closer decimal approximation
 * does not track discrimination on real content, so the existing triple is kept. */
#define PH_GRAY_R 38
#define PH_GRAY_G 75
#define PH_GRAY_B 15

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
