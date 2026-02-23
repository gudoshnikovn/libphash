#ifndef LIBPHASH_H
#define LIBPHASH_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file libphash.h
 * @brief High-performance, thread-safe perceptual hashing library.
 *
 * Designed for easy FFI integration (Python, Rust, Node.js).
 * All functions are thread-safe provided they operate on different contexts.
 */

// --- Platform & Export Macros ---
#ifndef PH_API
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef LIBPHASH_EXPORTS
#define PH_API __declspec(dllexport)
#else
#define PH_API __declspec(dllimport)
#endif
#else
#if __GNUC__ >= 4
#define PH_API __attribute__((visibility("default")))
#else
#define PH_API
#endif
#endif
#endif

#ifndef PH_NODISCARD
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define PH_NODISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
#define PH_NODISCARD __attribute__((warn_unused_result))
#elif defined(_MSC_VER) && _MSC_VER >= 1700
#define PH_NODISCARD _Check_return_
#else
#define PH_NODISCARD
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Constants ---

/** Maximum size in bytes for any digest supported by the library (64 bytes =
 * 512 bits). */
#define PH_DIGEST_MAX_BYTES 64

// --- Error Codes ---

typedef enum {
    PH_SUCCESS = 0,
    PH_ERR_ALLOCATION_FAILED = -1,
    PH_ERR_DECODE_FAILED = -2,
    PH_ERR_INVALID_ARGUMENT = -3,
    PH_ERR_NOT_IMPLEMENTED = -4,
    PH_ERR_EMPTY_IMAGE = -5,
} ph_error_t;

/**
 * @brief Wavelet Hash operating modes.
 */
typedef enum {
    PH_WHASH_FAST = 0, ///< High-speed 8x8 median approximation (default).
    PH_WHASH_FULL = 1, ///< Academically accurate full 2D DWT matching ImageHash.
} ph_whash_mode_t;

// --- Types ---

/**
 * @brief Opaque context structure holding image data and configuration.
 * Treat this as a void* in FFI.
 */
typedef struct ph_context ph_context_t;

/**
 * @brief A flat structure representing a hash digest.
 *
 * @note This structure is FFI-safe and can be allocated on the stack.
 * It does not own any heap memory.
 */
typedef struct {
    uint8_t data[PH_DIGEST_MAX_BYTES]; ///< The raw hash bytes.
    uint8_t size;                      ///< Number of valid bytes in 'data'.
    uint8_t reserved[7];               ///< Padding for 64-bit alignment.
} ph_digest_t;

// --- Lifecycle & Configuration ---

/**
 * @brief Returns the library version string (e.g., "1.2.0").
 */
PH_API const char *ph_version(void);

/**
 * @brief Allocates a new context with default settings (Gamma 2.2).
 * @param[out] out_ctx Pointer to the created context.
 */
PH_API PH_NODISCARD ph_error_t ph_create(ph_context_t **out_ctx);

/**
 * @brief Frees the context and all associated image memory.
 * @param ctx The context to free. Safe to pass NULL.
 */
PH_API void ph_free(ph_context_t *ctx);

/**
 * @brief Sets the gamma correction value for the context.
 *
 * Recomputes the internal lookup table (LUT).
 * Default value is 2.2.
 *
 * @param ctx The context.
 * @param gamma The gamma value (e.g., 2.2). Must be > 0.
 */
PH_API void ph_context_set_gamma(ph_context_t *ctx, float gamma);

/**
 * @brief Sets custom RGB-to-Grayscale weights.
 *
 * Input values are automatically normalized to sum to 128 for optimized internal processing.
 * Default is PH_GRAY_R=38, PH_GRAY_G=75, PH_GRAY_B=15.
 */
PH_API void ph_context_set_gray_weights(ph_context_t *ctx, int r, int g, int b);

/**
 * @brief Sets pHash parameters.
 * @param dct_size Size of the DCT matrix (default 32).
 * @param reduction_size Size of the low-frequency coefficients to keep (default 8).
 */
PH_API void ph_context_set_phash_params(ph_context_t *ctx, int dct_size, int reduction_size);

/**
 * @brief Sets Radial Hash parameters.
 * @param projections Number of angular projections (default 40).
 * @param samples Number of radial samples (default 128).
 */
PH_API void ph_context_set_radial_params(ph_context_t *ctx, int projections, int samples);

/**
 * @brief Sets Block-based hash parameters (BMH/mHash).
 * @param block_size Resolution of the grid (default 16).
 */
PH_API void ph_context_set_block_params(ph_context_t *ctx, int block_size);

/**
 * @brief Sets the operating mode for Wavelet Hash (wHash).
 *
 * @param ctx The context.
 * @param mode PH_WHASH_FAST (0) for 15x speedup, PH_WHASH_FULL (1) for maximum accuracy.
 */
PH_API void ph_context_set_whash_mode(ph_context_t *ctx, ph_whash_mode_t mode);

/**
 * @brief Controls whether images are loaded as grayscale by default.
 *
 * If enabled (non-zero), `ph_load_from_file` and `ph_load_from_memory` will
 * request single-channel data from the decoder. This is significantly faster for
 * algorithms that don't need color (pHash, aHash, dHash, mHash, wHash, BMH, Radial).
 *
 * @note Disabled by default for compatibility with ColorHash and custom weights.
 *       Enable it (set to 1) for significant speedup if you only need grayscale hashes
 *       (pHash, aHash, dHash, mHash, wHash, BMH, Radial).
 *
 * @param ctx The context.
 * @param enable 1 to enable grayscale loading, 0 to disable (load native channels).
 */
PH_API void ph_context_set_load_grayscale(ph_context_t *ctx, int enable);

/**
 * @brief Returns the dimensions of the currently loaded image.
 * @param ctx The context.
 * @param width Output for width.
 * @param height Output for height.
 * @param channels Output for number of channels.
 */
PH_API void ph_context_get_dimensions(ph_context_t *ctx, int *width, int *height, int *channels);

/**
 * @brief Checks if an image is currently loaded in the context.
 * @return 1 if loaded, 0 otherwise.
 */
PH_API int ph_is_loaded(ph_context_t *ctx);

// --- Loading ---

/**
 * @brief Loads an image from a file path.
 * @param ctx The context.
 * @param filepath Path to the image file.
 */
PH_API PH_NODISCARD ph_error_t ph_load_from_file(ph_context_t *ctx, const char *filepath);

/**
 * @brief Loads an image from a memory buffer.
 * @param ctx The context.
 * @param buffer Pointer to the raw file data (e.g., JPEG bytes).
 * @param length Size of the buffer.
 */
PH_API PH_NODISCARD ph_error_t ph_load_from_memory(ph_context_t *ctx, const uint8_t *buffer,
                                                   size_t length);

// --- uint64_t Hash Algorithms ---

PH_API PH_NODISCARD ph_error_t ph_compute_ahash(ph_context_t *ctx, uint64_t *out_hash);
PH_API PH_NODISCARD ph_error_t ph_compute_dhash(ph_context_t *ctx, uint64_t *out_hash);
PH_API PH_NODISCARD ph_error_t ph_compute_phash(ph_context_t *ctx, uint64_t *out_hash);
PH_API PH_NODISCARD ph_error_t ph_compute_whash(ph_context_t *ctx, uint64_t *out_hash);
PH_API PH_NODISCARD ph_error_t ph_compute_mhash(ph_context_t *ctx, uint64_t *out_hash);
PH_API PH_NODISCARD ph_error_t ph_compute_color_hash(ph_context_t *ctx, uint64_t *out_hash);

// --- Digest Hash Algorithms ---

/**
 * @brief Computes Block Mean Hash (BMH). Returns a 256-bit (32-byte) digest.
 */
PH_API PH_NODISCARD ph_error_t ph_compute_bmh(ph_context_t *ctx, ph_digest_t *out_digest);

/**
 * @brief Computes Color Moments Hash. Returns a digest representing color distribution.
 */
PH_API PH_NODISCARD ph_error_t ph_compute_color_moments_hash(ph_context_t *ctx,
                                                             ph_digest_t *out_digest);

/**
 * @brief Computes Radial Hash. Robust against rotation. Uses context Gamma.
 */
PH_API PH_NODISCARD ph_error_t ph_compute_radial_hash(ph_context_t *ctx, ph_digest_t *out_digest);

// --- Comparison Functions ---

PH_API int ph_hamming_distance(uint64_t hash1, uint64_t hash2);
PH_API int ph_hamming_distance_digest(const ph_digest_t *a, const ph_digest_t *b);
PH_API double ph_l2_distance(const ph_digest_t *a, const ph_digest_t *b);

/**
 * @brief Checks if libjpeg-turbo is available and loaded.
 * @return 1 if available, 0 otherwise.
 */
PH_API int ph_can_use_libjpeg(void);

/**
 * @brief Checks if libpng is available and loaded.
 * @return 1 if available, 0 otherwise.
 */
PH_API int ph_can_use_libpng(void);

#ifdef __cplusplus
}
#endif

#endif // LIBPHASH_H
