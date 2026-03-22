#ifndef PH_LOADER_H
#define PH_LOADER_H

#include "../include/libphash.h"
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

#ifdef PH_USE_TURBOJPEG
// --- JPEG: Static TurboJPEG API (tjDecompress2) ---
unsigned char *ph_decode_jpeg_tj(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp);
#endif

#ifdef PH_USE_LIBPNG
// --- PNG: Static libpng (memory-based reading, ARM NEON optimized) ---
unsigned char *ph_decode_png_mem(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp);
#elif defined(PH_USE_SPNG)
// --- PNG: Static spng (single-call API, fast on x86) ---
unsigned char *ph_decode_png_mem(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp);
#endif

#ifdef PH_USE_WEBP
// --- WebP: libwebp (decodes to RGB, no native grayscale) ---
unsigned char *ph_decode_webp_mem(const unsigned char *buffer, unsigned long size, int *width,
                                  int *height, int *channels, int req_comp);
#endif

// Runtime capability checks (always available)
int ph_can_use_libjpeg(void);
int ph_can_use_libpng(void);
int ph_can_use_webp(void);

// Image Backend Interface for unified decoding
typedef struct {
    int (*can_read)(const uint8_t *magic, size_t len);
    uint8_t *(*decode)(const uint8_t *data, size_t len, int *w, int *h, int *ch, int req_comp);
} ph_image_backend_t;

// Unified decoder that tries all registered backends
uint8_t *ph_decode_buffer(const uint8_t *buffer, size_t length, int *width, int *height,
                          int *channels, int req_comp);

// Safe image memory free
void ph_free_image(uint8_t *data);

#endif // PH_LOADER_H
