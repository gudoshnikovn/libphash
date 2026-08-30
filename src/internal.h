#ifndef INTERNAL_H
#define INTERNAL_H

#include "libphash.h"
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

uint8_t *ph_get_gray(ph_context_t *ctx);

/* mHash Laplacian scan helper */
uint64_t ph_laplacian_scan(const uint8_t *grid, int size, int step);

typedef struct {
    double mean;
    double std_dev;
    double skew;
} ph_channel_moments_t;

ph_channel_moments_t ph_compute_moments(const uint8_t *data, int num_pixels, int channels,
                                        int channel_index);

void ph_dct2_partial(const float *dct_mat, const uint8_t *input, int dct_size, int reduction_size,
                     float *out);

uint64_t ph_median_bitpack(const float *values, int n);

typedef enum { PH_HSV_BLACK = 0, PH_HSV_GRAY, PH_HSV_FAINT, PH_HSV_BRIGHT } ph_hsv_category_t;

typedef struct {
    ph_hsv_category_t category;
    int hue_bin; // 0..5, valid only for FAINT/BRIGHT
} ph_hsv_result_t;

ph_hsv_result_t ph_hsv_classify_pixel(float r, float g, float b);
uint64_t ph_pack_3bit_values(const double *values, int n);

float ph_get_pixel_bilinear(const uint8_t *img, int w, int h, float x, float y);
double ph_projection_variance(const uint8_t *img, int w, int h, double cx, double cy,
                              double max_radius, float cos_t, float sin_t, int samples);

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
#define PH_BLOCK_SIZE 16            // 16x16 grid for BMH and MHash
#define PH_HAAR_SCALE 1.41421356237 // sqrt(2) for Haar wavelet normalization
#define PH_RADIAL_PROJECTIONS 40
#define PH_RADIAL_SAMPLES 128

#define PH_COLOR_MOMENTS 3
#define PH_COLOR_CHANNELS 3

#define PH_DEFAULT_GAMMA 2.2f
#define PH_GAMMA_EPSILON 0.001f

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
 * Safety Macros
 */
// Check for integer overflow before allocation: w * h
#define PH_SAFE_ALLOC_SIZE(w, h) ((unsigned long long)(w) * (unsigned long long)(h) <= SIZE_MAX)

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

/* Returns 1 if decoding a w x h image is disallowed by max_pixels (0 = unlimited). */
static inline int ph_exceeds_pixel_limit(uint64_t w, uint64_t h, uint64_t max_pixels) {
    if (max_pixels == 0)
        return 0;
    return w * h > max_pixels;
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
        int phash_dct_size;
        int phash_reduction_size;
        int radial_projections;
        int radial_samples;
        int block_size;
        ph_whash_mode_t whash_mode;

        // 0 = unlimited; otherwise max allowed width*height before decoding a pixel buffer.
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
