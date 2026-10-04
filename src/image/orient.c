/*
 * EXIF Orientation (TIFF tag 0x0112) support: a self-contained, narrowly-scoped
 * reader for the one tag libphash cares about, plus the 8-case pixel transform.
 *
 * Deliberately not a general EXIF/TIFF library: only IFD0 of the primary TIFF
 * structure is walked, looking for a single SHORT tag. All of this operates on
 * untrusted input (raw file bytes), so every read is bounds-checked and any
 * malformed/absent metadata degrades silently to orientation 1 ("normal", no
 * transform) rather than failing the load — EXIF data is optional annotation,
 * not something a decode should fail over.
 */
#include "image/image.h"
#include "loader.h"
#include "safety.h"

#include "bytes.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* TIFF fields in the byte order the "II"/"MM" marker announced. */
static uint16_t ph_rd16(const uint8_t *p, int little_endian) {
    return little_endian ? ph_load_le16(p) : ph_load_be16(p);
}

static uint32_t ph_rd32(const uint8_t *p, int little_endian) {
    return little_endian ? ph_load_le32(p) : ph_load_be32(p);
}

/* `tiff` points at the "II"/"MM" byte-order marker; `len` is the remaining
 * buffer length from there. Returns the Orientation value (1..8), or 1 if the
 * tag is missing, the type/count doesn't match the spec, or the structure
 * doesn't parse at all. */
static int ph_parse_tiff_orientation(const uint8_t *tiff, size_t len) {
    if (len < 8) {
        return 1;
    }

    int le;
    if (tiff[0] == 'I' && tiff[1] == 'I') {
        le = 1;
    } else if (tiff[0] == 'M' && tiff[1] == 'M') {
        le = 0;
    } else {
        return 1;
    }

    if (ph_rd16(tiff + 2, le) != 42) {
        return 1;
    }

    uint32_t ifd0_off = ph_rd32(tiff + 4, le);
    if (ifd0_off > len || len - ifd0_off < 2) {
        return 1;
    }

    size_t count = ph_rd16(tiff + ifd0_off, le);
    const size_t entries_avail = len - ifd0_off - 2;
    if (count * 12 > entries_avail) {
        /* Truncated IFD: only look at however many whole 12-byte entries
         * actually fit, instead of rejecting the whole thing outright. */
        count = entries_avail / 12;
    }

    for (size_t i = 0; i < count; i++) {
        const uint8_t *entry = tiff + ifd0_off + 2 + i * 12;
        if (ph_rd16(entry, le) != 0x0112) { /* Orientation tag */
            continue;
        }
        uint16_t type = ph_rd16(entry + 2, le);
        uint32_t cnt = ph_rd32(entry + 4, le);
        if (type != 3 /* SHORT */ || cnt != 1) {
            continue; // Non-conforming tag: ignore rather than misinterpret.
        }
        uint16_t value = ph_rd16(entry + 8, le);
        return (value >= 1 && value <= 8) ? value : 1;
    }
    return 1;
}

int ph_exif_orientation_from_jpeg(const uint8_t *data, size_t len) {
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return 1;
    }

    size_t i = 2;
    while (i + 4 <= len) {
        if (data[i] != 0xFF) {
            i++; // Resync on stray fill bytes between segments.
            continue;
        }
        uint8_t marker = data[i + 1];
        if (marker == 0xFF) {
            i++; // Padding byte before the real marker code.
            continue;
        }
        if (marker == 0xD8 || marker == 0xD9 || marker == 0x01 ||
            (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2; // SOI/EOI/TEM/RSTn carry no length field.
            continue;
        }
        if (marker == 0xDA) {
            break; // SOS: entropy-coded scan data follows, nothing left to scan.
        }

        uint16_t seg_len = ph_load_be16(data + i + 2);
        if (seg_len < 2 || i + 2 + seg_len > len) {
            break; // Malformed length: stop rather than read out of bounds.
        }

        if (marker == 0xE1) { // APP1
            const uint8_t *payload = data + i + 4;
            size_t payload_len = seg_len - 2;
            if (payload_len >= 6 && memcmp(payload, "Exif\0\0", 6) == 0) {
                return ph_parse_tiff_orientation(payload + 6, payload_len - 6);
            }
        }
        i += 2 + seg_len;
    }
    return 1;
}

int ph_exif_orientation_from_webp(const uint8_t *data, size_t len) {
    if (len < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WEBP", 4) != 0) {
        return 1;
    }

    size_t i = 12;
    while (i + 8 <= len) {
        const uint8_t *fourcc = data + i;
        uint32_t chunk_size = ph_load_le32(data + i + 4);
        size_t payload_off = i + 8;
        if (chunk_size > len - payload_off) {
            break; // Malformed size: stop rather than read out of bounds.
        }

        if (memcmp(fourcc, "EXIF", 4) == 0) {
            const uint8_t *payload = data + payload_off;
            size_t plen = chunk_size;
            // Some encoders prefix the chunk with "Exif\0\0" like JPEG APP1 does.
            if (plen >= 6 && memcmp(payload, "Exif\0\0", 6) == 0) {
                payload += 6;
                plen -= 6;
            }
            return ph_parse_tiff_orientation(payload, plen);
        }

        size_t advance = 8 + chunk_size + (chunk_size & 1); // Chunks pad to even size.
        i += advance;
    }
    return 1;
}

int ph_exif_orientation_from_png(const uint8_t *data, size_t len) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (len < 8 || memcmp(data, sig, 8) != 0) {
        return 1;
    }

    size_t i = 8;
    while (i + 8 <= len) {
        uint32_t chunk_len = ph_load_be32(data + i);
        const uint8_t *type = data + i + 4;
        size_t payload_off = i + 8;
        if (chunk_len > len - payload_off) {
            break; // Malformed length: stop rather than read out of bounds.
        }

        if (memcmp(type, "eXIf", 4) == 0) {
            // Per the PNG spec the chunk holds the raw EXIF TIFF structure with
            // no "Exif\0\0" wrapper, but tolerate one anyway in case some writer
            // copied the JPEG APP1 payload over verbatim.
            const uint8_t *payload = data + payload_off;
            size_t plen = chunk_len;
            if (plen >= 6 && memcmp(payload, "Exif\0\0", 6) == 0) {
                payload += 6;
                plen -= 6;
            }
            return ph_parse_tiff_orientation(payload, plen);
        }
        if (memcmp(type, "IEND", 4) == 0) {
            break;
        }

        // Advance past this chunk's payload and its 4-byte CRC trailer; bail
        // rather than wrap past the end of the buffer on a truncated chunk.
        if (payload_off + chunk_len > len - 4) {
            break;
        }
        i = payload_off + chunk_len + 4;
    }
    return 1;
}

/* Copy one pixel. The common channel counts are spelled out so the compiler
 * emits inline loads/stores instead of a call into memcpy with a runtime size:
 * this runs once per pixel, so a call here costs more than the copy itself. */
static inline void ph_orient_copy_px(uint8_t *dst, const uint8_t *src, size_t channels) {
    switch (channels) {
        case 1:
            dst[0] = src[0];
            return;
        case 2:
            dst[0] = src[0];
            dst[1] = src[1];
            return;
        case 3:
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            return;
        case 4:
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
            return;
        default:
            memcpy(dst, src, channels);
            return;
    }
}

/* Tile edge for the transposing orientations (5..8). A tile keeps both the
 * strided source strip and the destination row strip resident in cache while it
 * is walked; the naive full-width transpose instead touches a new source cache
 * line per output pixel and thrashes on anything wider than a few hundred
 * pixels. 64 measured fastest on a 20 Mp RGB buffer (16 and 32 are ~1.5x
 * slower, 128 is a wash), and a tile still fits in L1 at 4 channels. */
#define PH_ORIENT_TILE 64

ph_error_t ph_apply_exif_orientation(uint8_t **data, int *width, int *height, int channels,
                                     int orientation) {
    if (!data || !*data || !width || !height || channels <= 0) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    if (orientation <= 1 || orientation > 8) {
        return PH_SUCCESS; // Orientation 1 (and anything unknown) needs no transform at all.
    }

    if (*width < 0 || *height < 0) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    const int out_width = (orientation >= 5) ? *height : *width;
    const int out_height = (orientation >= 5) ? *width : *height;
    const size_t W = ph_size(*width), H = ph_size(*height), px = ph_size(channels);
    const size_t Wd = (orientation >= 5) ? H : W;
    const size_t Hd = (orientation >= 5) ? W : H;

    size_t out_size;
    if (!ph_safe_image_alloc_size(Wd, Hd, px, &out_size)) {
        return PH_ERR_IMAGE_TOO_LARGE; // Can't safely size the output; image untouched.
    }

    uint8_t *out = malloc(out_size);
    if (!out) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    const uint8_t *src = *data;
    const size_t src_stride = W * px;
    const size_t dst_stride = Wd * px;

    /* Same mapping as a per-pixel `dst(ox,oy) = src(sx,sy)` switch, but with the
     * case analysis hoisted out of the pixel loops and the memory walk shaped to
     * the access pattern each case actually has. */
    if (orientation == 4) {
        /* Flip vertically: rows are copied whole, in reverse order. */
        for (size_t oy = 0; oy < Hd; oy++) {
            memcpy(out + oy * dst_stride, src + (H - 1 - oy) * src_stride, src_stride);
        }
    } else if (orientation == 2 || orientation == 3) {
        /* Mirror horizontally (2), plus the vertical flip for the 180° rotation
         * (3). Either way one destination row comes from exactly one source row,
         * so both pointers stay within a single row's worth of cache lines. */
        const int flip_rows = (orientation == 3);
        for (size_t oy = 0; oy < Hd; oy++) {
            const uint8_t *row = src + (flip_rows ? H - 1 - oy : oy) * src_stride;
            uint8_t *d = out + oy * dst_stride;
            /* Walk the source row backwards. Offsets rather than a moving
             * pointer: a pointer would step one pixel before the row on the
             * final iteration, which is undefined even when never dereferenced. */
            size_t soff = (W - 1) * px;
            for (size_t ox = 0; ox < W; ox++, d += px, soff -= px) {
                ph_orient_copy_px(d, row + soff, px);
            }
        }
    } else {
        /* Transposing orientations (5..8): the output is the source with its
         * axes swapped, optionally mirrored on either axis. `sx` depends only on
         * `oy` and `sy` only on `ox`, so both reduce to a fixed source column and
         * a fixed ±row step; a tiled walk keeps that strided column read inside
         * the cache. */
        const int flip_x = (orientation == 7 || orientation == 8); // sx = W-1-oy
        const int flip_y = (orientation == 6 || orientation == 7); // sy = H-1-ox
        /* One source row down, or up when mirrored: unsigned arithmetic wraps modulo
         * SIZE_MAX + 1, so adding 0 - src_stride steps back exactly one row. */
        const size_t row_step = flip_y ? 0 - src_stride : src_stride;

        for (size_t oy0 = 0; oy0 < Hd; oy0 += PH_ORIENT_TILE) {
            const size_t oy1 = (oy0 + PH_ORIENT_TILE < Hd) ? oy0 + PH_ORIENT_TILE : Hd;
            for (size_t ox0 = 0; ox0 < Wd; ox0 += PH_ORIENT_TILE) {
                const size_t ox1 = (ox0 + PH_ORIENT_TILE < Wd) ? ox0 + PH_ORIENT_TILE : Wd;
                for (size_t oy = oy0; oy < oy1; oy++) {
                    const size_t sx = flip_x ? W - 1 - oy : oy;
                    const size_t sy0 = flip_y ? H - 1 - ox0 : ox0;
                    /* Offset, not a moving pointer: on the last iteration of a
                     * mirrored walk a pointer would step off the front of the
                     * buffer, which is undefined even if never dereferenced. */
                    size_t soff = sy0 * src_stride + sx * px;
                    uint8_t *d = out + oy * dst_stride + ox0 * px;
                    for (size_t ox = ox0; ox < ox1; ox++, d += px, soff += row_step) {
                        ph_orient_copy_px(d, src + soff, px);
                    }
                }
            }
        }
    }

    ph_free_image(*data);
    *data = out;
    *width = out_width;
    *height = out_height;
    return PH_SUCCESS;
}
