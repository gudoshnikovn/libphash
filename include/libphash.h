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
    PH_ERR_IMAGE_TOO_LARGE = -6,
} ph_error_t;

/**
 * @brief Returns a human-readable string description for an error code.
 * @param err The error code.
 * @return A constant string describing the error. Never returns NULL.
 */
PH_API const char *ph_get_error_string(ph_error_t err);

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
 * @brief Returns the library version string (e.g., "1.11.0").
 */
PH_API const char *ph_version(void);

/**
 * @brief Returns the library version as a single comparable integer:
 *        major*10000 + minor*100 + patch (e.g. 1.11.0 -> 11100).
 *        Intended for FFI callers doing compatibility checks without string parsing.
 */
PH_API int ph_version_number(void);

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
 * @brief Sets the maximum number of pixels (width * height) an image is allowed to
 *        decode to, before any pixel buffer is allocated. Protects against
 *        decompression-bomb inputs (a small file that declares an enormous image size).
 *
 * @param ctx The context.
 * @param max_pixels Maximum width*height allowed. 0 disables the check entirely
 *                    (unlimited). Defaults to 256 * 1024 * 1024 (256 megapixels).
 *
 * @note Loading an image that exceeds the limit fails with PH_ERR_IMAGE_TOO_LARGE
 *       instead of attempting the allocation.
 */
PH_API void ph_context_set_max_pixels(ph_context_t *ctx, uint64_t max_pixels);

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

/**
 * @brief Loads an image from an already-decoded pixel buffer.
 *
 * Use this to hash frames that already live in memory as raw pixels (e.g. from
 * OpenCV, PIL, numpy, or a video frame), skipping the encode/decode round-trip
 * that `ph_load_from_file`/`ph_load_from_memory` would otherwise require.
 *
 * @param ctx The context.
 * @param pixels Pointer to the raw pixel data, `height` rows of `channels`-interleaved bytes.
 * @param width Image width in pixels. Must be > 0.
 * @param height Image height in pixels. Must be > 0.
 * @param channels Number of channels per pixel. Must be 1, 3, or 4.
 * @param stride Number of bytes between the start of consecutive rows. Pass 0 for
 *               tightly packed rows (stride = width * channels).
 */
PH_API PH_NODISCARD ph_error_t ph_load_from_pixels(ph_context_t *ctx, const uint8_t *pixels,
                                                   int width, int height, int channels,
                                                   int stride);

// --- uint64_t Hash Algorithms ---

PH_API PH_NODISCARD ph_error_t ph_compute_ahash(ph_context_t *ctx, uint64_t *out_hash);
PH_API PH_NODISCARD ph_error_t ph_compute_dhash(ph_context_t *ctx, uint64_t *out_hash);
PH_API PH_NODISCARD ph_error_t ph_compute_phash(ph_context_t *ctx, uint64_t *out_hash);
PH_API PH_NODISCARD ph_error_t ph_compute_whash(ph_context_t *ctx, uint64_t *out_hash);
PH_API PH_NODISCARD ph_error_t ph_compute_mhash(ph_context_t *ctx, uint64_t *out_hash);
PH_API PH_NODISCARD ph_error_t ph_compute_color_hash(ph_context_t *ctx, uint64_t *out_hash);

/**
 * @brief Flags selecting which uint64_t hash algorithms to compute in a single
 *        `ph_compute_multi()` call. Bitwise-OR any combination.
 */
typedef enum {
    PH_HASH_AHASH = 1 << 0,
    PH_HASH_DHASH = 1 << 1,
    PH_HASH_PHASH = 1 << 2,
    PH_HASH_WHASH = 1 << 3,
    PH_HASH_MHASH = 1 << 4,
    PH_HASH_COLOR_HASH = 1 << 5,
} ph_hash_flags_t;

/** Number of distinct bits defined in ph_hash_flags_t. Sizes ph_compute_multi's out[]. */
#define PH_HASH_FLAGS_COUNT 6

/**
 * @brief Computes multiple uint64_t hash algorithms for the loaded image in one call.
 *
 * Equivalent to calling the individual `ph_compute_*` functions for each flag set in
 * `flags`, but shares the grayscale conversion across all of them instead of recomputing
 * it once per algorithm (each `ph_compute_*` call already reuses the context's cached
 * grayscale buffer, so calling several of them back to back on the same context has
 * always been cheaper than reloading between them — this just wraps that into one call).
 * Results are bit-for-bit identical to calling the equivalent `ph_compute_*` function
 * directly.
 *
 * @param ctx The context. Must have an image already loaded.
 * @param flags Bitwise-OR of `ph_hash_flags_t` values selecting which hashes to compute.
 * @param[out] out Array written with one uint64_t per flag that was set, in ascending
 *                 bit order (e.g. for `PH_HASH_DHASH | PH_HASH_MHASH`, `out[0]` receives
 *                 the dHash and `out[1]` the mHash). Must have room for at least as many
 *                 elements as bits set in `flags` (at most `PH_HASH_FLAGS_COUNT`).
 */
PH_API PH_NODISCARD ph_error_t ph_compute_multi(ph_context_t *ctx, uint32_t flags,
                                                uint64_t out[]);

// --- Batch Hashing ---

/**
 * @brief One entry in a `ph_hash_files()` batch: a file path in, hashes and status out.
 */
typedef struct {
    const char *path; ///< Path to the image file to hash. Must stay valid for the call.
    /** [out] One uint64_t per flag set in the `flags` passed to `ph_hash_files()`, packed
     *  in ascending bit order -- same layout as `ph_compute_multi()`'s `out[]`. Valid only
     *  if `status == PH_SUCCESS`. */
    uint64_t hashes[PH_HASH_FLAGS_COUNT];
    ph_error_t status; ///< [out] Per-item result. A failure here does not abort the batch.
} ph_batch_item_t;

/**
 * @brief One entry in a `ph_hash_buffers()` batch: an in-memory image buffer in, hashes
 *        and status out.
 */
typedef struct {
    const uint8_t *buffer; ///< Pointer to the encoded image bytes (e.g. JPEG/PNG/WebP).
    size_t length;          ///< Size of `buffer` in bytes.
    /** [out] One uint64_t per flag set in the `flags` passed to `ph_hash_buffers()`, packed
     *  in ascending bit order -- same layout as `ph_compute_multi()`'s `out[]`. Valid only
     *  if `status == PH_SUCCESS`. */
    uint64_t hashes[PH_HASH_FLAGS_COUNT];
    ph_error_t status; ///< [out] Per-item result. A failure here does not abort the batch.
} ph_batch_buffer_item_t;

/**
 * @brief Hashes a batch of image files, optionally across a pool of internal threads.
 *
 * Each item is loaded and hashed independently. Internally this calls the same shared-
 * grayscale `ph_compute_multi()` used for a single image, so requesting several
 * algorithms for the same file is no more expensive per-file than requesting one (the
 * grayscale conversion/downscales are not repeated). A decode/hash failure on one item is
 * recorded in that item's `status` and does not stop the rest of the batch from being
 * processed. The overall return value only reports argument-validation failures that
 * prevented the batch from starting at all.
 *
 * @param items Array of batch entries; `path`/`hashes`/`status` are read/written in place.
 * @param n Number of entries in `items`. 0 is a no-op that returns PH_SUCCESS immediately.
 * @param flags Bitwise-OR of `ph_hash_flags_t` values selecting which algorithms to
 *              compute for every item. See `ph_compute_multi()` for the `hashes[]`
 *              packing convention.
 * @param threads Worker thread count. 0 = one per detected CPU core, 1 = run sequentially
 *                on the calling thread with no thread creation, >1 = that many workers.
 *                Ignored (always sequential) if the library was built without
 *                `PHASH_ENABLE_THREADS` (default ON in CMake, OFF in the Makefile) or if
 *                `n` is smaller than the requested thread count.
 * @return PH_SUCCESS once the batch has been dispatched (regardless of per-item
 *         outcomes), or PH_ERR_INVALID_ARGUMENT for a malformed call (NULL `items` with
 *         `n > 0`, `flags` zero or containing an unknown bit, or negative `threads`).
 */
PH_API PH_NODISCARD ph_error_t ph_hash_files(ph_batch_item_t *items, size_t n, uint32_t flags,
                                             int threads);

/**
 * @brief Same as `ph_hash_files()`, but for already-in-memory encoded image buffers
 *        (e.g. downloaded bytes) instead of file paths.
 */
PH_API PH_NODISCARD ph_error_t ph_hash_buffers(ph_batch_buffer_item_t *items, size_t n,
                                               uint32_t flags, int threads);

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

/**
 * @brief Checks if libwebp is available and loaded.
 * @return 1 if available, 0 otherwise.
 */
PH_API int ph_can_use_webp(void);

#ifdef __cplusplus
}
#endif

#endif // LIBPHASH_H
