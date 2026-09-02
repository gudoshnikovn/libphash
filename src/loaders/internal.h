#ifndef PH_LOADERS_INTERNAL_H
#define PH_LOADERS_INTERNAL_H

#include "../internal.h" // ph_exceeds_pixel_limit / ph_safe_image_alloc_size / struct ph_context
#include "loader.h"
#include <string.h> // memcmp, for the PNG signature check

/* PNG magic bytes. Available regardless of which backend (if any) is compiled in:
 * the dimension check below is made by the dispatcher, before it picks a backend. */
static inline int ph_magic_is_png(const uint8_t *magic, size_t len) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return len >= 8 && memcmp(magic, sig, sizeof(sig)) == 0;
}

/* Reads width/height out of a PNG IHDR still in the input buffer and reports whether
 * they are within PH_MAX_IMAGE_DIMENSION.
 *
 * PNG needs this dedicated probe because the generic path -- ask the decoder for the
 * header dimensions, then check them -- is not available for it: stb_image refuses a
 * dimension above its own STBI_MAX_DIMENSIONS (2^24) from stbi_info() itself, and would
 * answer an absurdly wide PNG with "corrupt" instead of "too large". Reading the IHDR
 * ourselves keeps the verdict and the error code identical across libpng, spng and
 * stb_image builds. Layout is fixed by the PNG spec: 8-byte signature, 4-byte length,
 * "IHDR", then width and height as big-endian uint32. Returns 1 when the header is too
 * short to judge -- the backend will report the truncation itself, with a better
 * message. */
static inline int ph_png_dimensions_within_limit(const unsigned char *buffer, unsigned long size) {
    if (size < 24)
        return 1;
    if (!(buffer[12] == 'I' && buffer[13] == 'H' && buffer[14] == 'D' && buffer[15] == 'R'))
        return 1; /* Not an IHDR where the spec requires one; let the backend say so. */

    uint32_t w = ((uint32_t)buffer[16] << 24) | ((uint32_t)buffer[17] << 16) |
                 ((uint32_t)buffer[18] << 8) | (uint32_t)buffer[19];
    uint32_t h = ((uint32_t)buffer[20] << 24) | ((uint32_t)buffer[21] << 16) |
                 ((uint32_t)buffer[22] << 8) | (uint32_t)buffer[23];

    return !ph_exceeds_dimension_limit(w, h);
}

#ifdef PH_USE_TURBOJPEG
int ph_can_read_jpeg(const uint8_t *magic, size_t len);
#endif

#if defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
int ph_can_read_png(const uint8_t *magic, size_t len);
#endif

#ifdef PH_USE_WEBP
int ph_can_read_webp(const uint8_t *magic, size_t len);
#endif

#endif // PH_LOADERS_INTERNAL_H
