#ifndef LIBPHASH_H
#define LIBPHASH_H

#include <stddef.h>
#include <stdint.h>

/*
 * Feature Detection for modern C attributes.
 * Supports C23 standard, with fallbacks for GCC, Clang, and MSVC.
 */
#ifndef PH_NODISCARD
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
/* C23 standard way */
#define PH_NODISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
/* GCC/Clang extension */
#define PH_NODISCARD __attribute__((warn_unused_result))
#elif defined(_MSC_VER) && _MSC_VER >= 1700
/* MSVC (Visual Studio) way */
#define PH_NODISCARD _Check_return_
#else
/* Fallback for older C standards (like C89/C99/C17) */
#define PH_NODISCARD
#endif
#endif

/*
 * Visibility macros for Shared Libraries (DLLs)
 */
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
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Error codes for libphash operations.
 */
typedef enum {
  PH_SUCCESS = 0,
  PH_ERR_ALLOCATION_FAILED = -1,
  PH_ERR_DECODE_FAILED = -2,
  PH_ERR_INVALID_ARGUMENT = -3,
} ph_error_t;

/**
 * @brief Opaque context handle holding image data.
 * In FFI (Python/Rust), this should be treated as an opaque pointer (void*).
 */
typedef struct ph_context ph_context_t;

/**
 * @brief Creates a new phash context.
 * @param out_ctx Pointer to the context handle to be initialized.
 * @return PH_SUCCESS on success.
 */
PH_API PH_NODISCARD ph_error_t ph_create(ph_context_t **out_ctx);

/**
 * @brief Frees the context and associated image data.
 * @param ctx Context handle. Safe to pass NULL.
 */
PH_API void ph_free(ph_context_t *ctx);

/**
 * @brief Loads an image from a file path.
 * @param ctx Valid context handle.
 * @param filepath Path to the image file (UTF-8).
 */
PH_API PH_NODISCARD ph_error_t ph_load_from_file(ph_context_t *ctx,
                                                 const char *filepath);

/**
 * @brief Computes the Average Hash (aHash).
 * Fast, but sensitive to lighting changes.
 */
PH_API PH_NODISCARD ph_error_t ph_compute_ahash(ph_context_t *ctx,
                                                uint64_t *out_hash);

/**
 * @brief Computes the Difference Hash (dHash).
 * Very fast and resistant to aspect ratio changes.
 */
PH_API PH_NODISCARD ph_error_t ph_compute_dhash(ph_context_t *ctx,
                                                uint64_t *out_hash);

/**
 * @brief Computes the Perceptual Hash (pHash) using DCT.
 * Most robust, but computationally expensive.
 */
PH_API PH_NODISCARD ph_error_t ph_compute_phash(ph_context_t *ctx,
                                                uint64_t *out_hash);

/**
 * @brief Calculates the Hamming distance between two hashes.
 * @return Number of differing bits (0-64). 0 means identical hashes.
 */
PH_API int ph_hamming_distance(uint64_t hash1, uint64_t hash2);

#ifdef __cplusplus
}
#endif

#endif /* LIBPHASH_H */
