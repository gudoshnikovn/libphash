#ifndef PH_BYTES_H
#define PH_BYTES_H

/* Fixed-width integers read from byte buffers in a stated byte order: container headers
 * (PNG chunk lengths and CRCs, RIFF sizes, JPEG segment lengths) and EXIF's TIFF fields.
 *
 * Each byte is widened to uint32_t before it is shifted: a uint8_t promotes to int, and
 * `<< 24` on an int can reach the sign bit. Two bytes always fit a 16-bit result. */

#include <stdint.h>

static inline uint16_t ph_load_be16(const uint8_t *p) {
    return (uint16_t)(((uint32_t)p[0] << 8) | p[1]);
}

static inline uint16_t ph_load_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint32_t)p[1] << 8));
}

static inline uint32_t ph_load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static inline uint32_t ph_load_le32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#endif /* PH_BYTES_H */
