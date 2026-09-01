#ifndef PH_LOADERS_INTERNAL_H
#define PH_LOADERS_INTERNAL_H

#include "../internal.h" // ph_exceeds_pixel_limit / ph_safe_image_alloc_size / struct ph_context
#include "loader.h"

#ifdef PH_USE_TURBOJPEG
int ph_can_read_jpeg(const uint8_t *magic, size_t len);
#endif

#if defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
int ph_can_read_png(const uint8_t *magic, size_t len);

/* Upper bound on a single image dimension, deliberately chosen rather than derived
 * from max_pixels.
 *
 * max_pixels bounds the *area*, which on its own permits an absurd aspect ratio: a
 * 268435456 x 1 PNG passes the default area limit exactly, yet makes libpng size a
 * row buffer of ~800 MB. libpng's own default per-dimension limit is 1000000, so this
 * matches it -- the point being that libphash must only ever *lower* that limit, never
 * raise it, which is what passing max_pixels straight into png_set_user_limits() did
 * (R16/M1). A dimension above this is refused with PH_ERR_IMAGE_TOO_LARGE. */
#define PH_MAX_IMAGE_DIMENSION 1000000u

/* Reads width/height out of a PNG IHDR still in the input buffer and reports whether
 * they are within PH_MAX_IMAGE_DIMENSION. Both decoder backends call this before
 * handing the buffer to their library, so the two agree on the verdict and on the
 * error code regardless of what the underlying library would have done.
 *
 * Layout is fixed by the PNG spec: 8-byte signature, 4-byte length, "IHDR",
 * then width and height as big-endian uint32. Returns 1 when the header is too short
 * to judge -- the backend will report the truncation itself, with a better message. */
static inline int ph_png_dimensions_within_limit(const unsigned char *buffer, unsigned long size) {
    if (size < 24)
        return 1;
    if (!(buffer[12] == 'I' && buffer[13] == 'H' && buffer[14] == 'D' && buffer[15] == 'R'))
        return 1; /* Not an IHDR where the spec requires one; let the backend say so. */

    uint32_t w = ((uint32_t)buffer[16] << 24) | ((uint32_t)buffer[17] << 16) |
                 ((uint32_t)buffer[18] << 8) | (uint32_t)buffer[19];
    uint32_t h = ((uint32_t)buffer[20] << 24) | ((uint32_t)buffer[21] << 16) |
                 ((uint32_t)buffer[22] << 8) | (uint32_t)buffer[23];

    return w <= PH_MAX_IMAGE_DIMENSION && h <= PH_MAX_IMAGE_DIMENSION;
}
#endif

#ifdef PH_USE_WEBP
int ph_can_read_webp(const uint8_t *magic, size_t len);
#endif

#endif // PH_LOADERS_INTERNAL_H
