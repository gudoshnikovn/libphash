#ifndef PH_LOADER_H
#define PH_LOADER_H

#include "libphash.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// =====================================================================
// Build Configuration Flags (set by CMake or _build.py):
//
//   PH_USE_TURBOJPEG  — Static libjpeg-turbo (TurboJPEG API)
//   PH_USE_LIBPNG     — Static libpng (ARM NEON optimized)
//   PH_USE_SPNG       — Static spng + zlib (x86 optimized, 43% faster on Linux)
//
// PH_USE_LIBPNG and PH_USE_SPNG are mutually exclusive.
// When none are defined: stb_image handles everything (zero dependencies).
// Default: PH_USE_TURBOJPEG + PH_USE_LIBPNG on ARM, PH_USE_SPNG on x86.
// Override: LIBPHASH_MINIMAL=1 disables all at pip install time.
// =====================================================================

// max_pixels: 0 = unlimited, otherwise the max allowed width*height; the decoder must
// check this against the image's header-declared dimensions *before* allocating a
// pixel buffer, and on violation set *out_err = PH_ERR_IMAGE_TOO_LARGE and return NULL.
// out_err: set to PH_SUCCESS by the caller before the call; decoders only need to set
// it on the PH_ERR_IMAGE_TOO_LARGE path (a NULL return with it left at PH_SUCCESS is
// treated as a generic decode failure).

#ifdef PH_USE_TURBOJPEG
// --- JPEG: Static TurboJPEG API (tjDecompress2) ---
unsigned char *ph_decode_jpeg_tj(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp, uint64_t max_pixels,
                                 ph_error_t *out_err);
#endif

#ifdef PH_USE_LIBPNG
// --- PNG: Static libpng (memory-based reading, ARM NEON optimized) ---
unsigned char *ph_decode_png_mem(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp, uint64_t max_pixels,
                                 ph_error_t *out_err);
#elif defined(PH_USE_SPNG)
// --- PNG: Static spng (single-call API, fast on x86) ---
unsigned char *ph_decode_png_mem(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp, uint64_t max_pixels,
                                 ph_error_t *out_err);
#endif

#ifdef PH_USE_WEBP
// --- WebP: libwebp (decodes to RGB, no native grayscale) ---
unsigned char *ph_decode_webp_mem(const unsigned char *buffer, unsigned long size, int *width,
                                  int *height, int *channels, int req_comp, uint64_t max_pixels,
                                  ph_error_t *out_err);
#endif

// Runtime capability checks (always available)
int ph_can_use_libjpeg(void);
int ph_can_use_libpng(void);
int ph_can_use_webp(void);

// Image Backend Interface for unified decoding
typedef struct {
    int (*can_read)(const uint8_t *magic, size_t len);
    uint8_t *(*decode)(const uint8_t *data, size_t len, int *w, int *h, int *ch, int req_comp,
                      uint64_t max_pixels, ph_error_t *out_err);
} ph_image_backend_t;

// Unified decoder that tries all registered backends.
// max_pixels: 0 = unlimited, otherwise the max allowed width*height.
// out_err: optional; set to PH_ERR_IMAGE_TOO_LARGE if a backend recognized the format
// but rejected it for exceeding max_pixels (left untouched on any other failure).
uint8_t *ph_decode_buffer(const uint8_t *buffer, size_t length, int *width, int *height,
                          int *channels, int req_comp, uint64_t max_pixels, ph_error_t *out_err);

// Safe image memory free
void ph_free_image(uint8_t *data);

#endif // PH_LOADER_H
