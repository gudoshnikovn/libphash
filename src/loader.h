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

// Magic-byte check that stays available regardless of whether a WebP decoder is
// compiled in — used by ph_decode_buffer() and core.c to report a recognized-
// but-unavailable format precisely (see PH_ERR_DECODER_UNAVAILABLE), and also by
// core.c's EXIF-orientation dispatch (src/image/orient.c) to tell a WebP buffer
// apart from a JPEG one before picking which metadata scanner to run.
static inline int ph_magic_is_webp(const uint8_t *magic, size_t len) {
    return (len >= 12 && magic[0] == 'R' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == 'F' &&
            magic[8] == 'W' && magic[9] == 'E' && magic[10] == 'B' && magic[11] == 'P');
}

// max_pixels: 0 = unlimited, otherwise the max allowed width*height; the decoder must
// check this against the image's header-declared dimensions *before* allocating a
// pixel buffer, and on violation set *out_err = PH_ERR_IMAGE_TOO_LARGE and return NULL.
// out_err: set to PH_SUCCESS by the caller before the call. Once a backend's can_read()
// has claimed the data, any decode failure it hits is a recognized-but-broken bitstream,
// so it should set *out_err to the most specific applicable code (PH_ERR_IMAGE_TOO_LARGE
// or PH_ERR_CORRUPT_DATA) rather than leaving it at PH_SUCCESS.
// err_msg/err_msg_cap: optional fixed-size buffer (may be NULL/0) that the decoder can
// fill with a short human-readable reason via ph_set_err_msg(); never allocates.

#ifdef PH_USE_TURBOJPEG
// --- JPEG: Static TurboJPEG API (tjDecompress2) ---
unsigned char *ph_decode_jpeg_tj(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp, uint64_t max_pixels,
                                 ph_error_t *out_err, char *err_msg, size_t err_msg_cap);
#endif

#ifdef PH_USE_LIBPNG
// --- PNG: Static libpng (memory-based reading, ARM NEON optimized) ---
unsigned char *ph_decode_png_mem(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp, uint64_t max_pixels,
                                 ph_error_t *out_err, char *err_msg, size_t err_msg_cap);
#elif defined(PH_USE_SPNG)
// --- PNG: Static spng (single-call API, fast on x86) ---
unsigned char *ph_decode_png_mem(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp, uint64_t max_pixels,
                                 ph_error_t *out_err, char *err_msg, size_t err_msg_cap);
#endif

#ifdef PH_USE_WEBP
// --- WebP: libwebp (decodes to RGB, no native grayscale) ---
unsigned char *ph_decode_webp_mem(const unsigned char *buffer, unsigned long size, int *width,
                                  int *height, int *channels, int req_comp, uint64_t max_pixels,
                                  ph_error_t *out_err, char *err_msg, size_t err_msg_cap);
#endif

// Runtime capability checks (always available)
int ph_can_use_libjpeg(void);
int ph_can_use_libpng(void);
int ph_can_use_webp(void);

// Image Backend Interface for unified decoding
typedef struct {
    int (*can_read)(const uint8_t *magic, size_t len);
    uint8_t *(*decode)(const uint8_t *data, size_t len, int *w, int *h, int *ch, int req_comp,
                       uint64_t max_pixels, ph_error_t *out_err, char *err_msg, size_t err_msg_cap);
} ph_image_backend_t;

// Unified decoder that tries all registered backends.
// max_pixels: 0 = unlimited, otherwise the max allowed width*height.
// out_err: optional; set to a specific PH_ERR_* code if a backend recognized the format
// but couldn't decode it (too large, corrupt) or if the format is recognized by magic
// bytes but its decoder wasn't compiled into this build (left untouched, i.e. PH_SUCCESS,
// if nothing recognized the data at all).
// err_msg/err_msg_cap: optional fixed-size buffer (may be NULL/0) filled with a short
// human-readable reason alongside *out_err.
uint8_t *ph_decode_buffer(const uint8_t *buffer, size_t length, int *width, int *height,
                          int *channels, int req_comp, uint64_t max_pixels, ph_error_t *out_err,
                          char *err_msg, size_t err_msg_cap);

// Safe image memory free
void ph_free_image(uint8_t *data);

#endif // PH_LOADER_H
