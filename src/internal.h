#ifndef INTERNAL_H
#define INTERNAL_H

#include "../include/libphash.h"
#include <stdint.h>

/*
 * Internal Image Processing Helpers
 */

/* Converts RGB/RGBA to Grayscale with custom weights */
void ph_to_grayscale(const ph_context_t *ctx, const uint8_t *src, int w, int h, int channels,
                     uint8_t *dst);

/* Resizes a grayscale image using box sampling (averaging) */
void ph_resize_box(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);
void ph_resize_mipmap(ph_context_t *ctx, const uint8_t *src, int sw, int sh, uint8_t *dst, int dw,
                      int dh);

/* Applies a 3x3 Gaussian Blur to reduce noise */
void ph_apply_gaussian_blur(ph_context_t *ctx, uint8_t *src, int w, int h, uint8_t *dst);

/* Applies Gamma Correction (gamma=2.2) to normalize brightness */
void ph_apply_gamma(const ph_context_t *ctx, uint8_t *data, int w, int h);

void ph_resize_bilinear(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

/* Applies 3x3 Laplacian sharpening for edge preservation */
void ph_apply_laplacian_3x3(const uint8_t *src, int w, int h, uint8_t *dst);

uint8_t *ph_get_gray(ph_context_t *ctx);

/* Initializes the DCT matrix (thread-safe, idempotent) */
void init_dct_matrix(void);

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

/* Internal Context Structure */
struct ph_context {
    uint8_t *data;
    uint8_t *gray_data;
    int width;
    int height;
    int channels;
    int is_loaded;

    /* Configuration */
    float gamma;
    uint8_t gamma_lut[256];

    int gray_r, gray_g, gray_b;

    /* pHash params */
    int phash_dct_size;
    int phash_reduction_size;

    /* Radial params */
    int radial_projections;
    int radial_samples;

    /* Block params (BMH, mHash) */
    int block_size;

    /* Tier 3: Memory Reuse */
    uint8_t *scratchpad;
    size_t scratchpad_size;

    /* Optimization Flags */
    int load_grayscale;
    ph_whash_mode_t whash_mode;
};

/* Ensures the context's scratchpad is at least 'size' bytes.
 * Returns NULL on failure, pointer to buffer on success. */
uint8_t *ph_get_scratchpad(ph_context_t *ctx, size_t size);

#endif /* INTERNAL_H */
