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
#include "../internal.h"
#include "../loader.h"
#include <stdlib.h>
#include <string.h>

static uint16_t ph_rd16(const uint8_t *p, int little_endian) {
    return little_endian ? (uint16_t)(p[0] | (p[1] << 8)) : (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t ph_rd32(const uint8_t *p, int little_endian) {
    if (little_endian)
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* `tiff` points at the "II"/"MM" byte-order marker; `len` is the remaining
 * buffer length from there. Returns the Orientation value (1..8), or 1 if the
 * tag is missing, the type/count doesn't match the spec, or the structure
 * doesn't parse at all. */
static int ph_parse_tiff_orientation(const uint8_t *tiff, size_t len) {
    if (len < 8)
        return 1;

    int le;
    if (tiff[0] == 'I' && tiff[1] == 'I')
        le = 1;
    else if (tiff[0] == 'M' && tiff[1] == 'M')
        le = 0;
    else
        return 1;

    if (ph_rd16(tiff + 2, le) != 42)
        return 1;

    uint32_t ifd0_off = ph_rd32(tiff + 4, le);
    if (ifd0_off > len || (uint64_t)ifd0_off + 2 > (uint64_t)len)
        return 1;

    uint16_t count = ph_rd16(tiff + ifd0_off, le);
    uint64_t entries_end = (uint64_t)ifd0_off + 2 + (uint64_t)count * 12;
    if (entries_end > (uint64_t)len) {
        /* Truncated IFD: only look at however many whole 12-byte entries
         * actually fit, instead of rejecting the whole thing outright. */
        count = (uint16_t)(((uint64_t)len - ifd0_off - 2) / 12);
    }

    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *entry = tiff + ifd0_off + 2 + (size_t)i * 12;
        if (ph_rd16(entry, le) != 0x0112) /* Orientation tag */
            continue;
        uint16_t type = ph_rd16(entry + 2, le);
        uint32_t cnt = ph_rd32(entry + 4, le);
        if (type != 3 /* SHORT */ || cnt != 1)
            continue; // Non-conforming tag: ignore rather than misinterpret.
        uint16_t value = ph_rd16(entry + 8, le);
        return (value >= 1 && value <= 8) ? (int)value : 1;
    }
    return 1;
}

int ph_exif_orientation_from_jpeg(const uint8_t *data, size_t len) {
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8)
        return 1;

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
        if (marker == 0xDA)
            break; // SOS: entropy-coded scan data follows, nothing left to scan.

        uint16_t seg_len = (uint16_t)((data[i + 2] << 8) | data[i + 3]);
        if (seg_len < 2 || i + 2 + seg_len > len)
            break; // Malformed length: stop rather than read out of bounds.

        if (marker == 0xE1) { // APP1
            const uint8_t *payload = data + i + 4;
            size_t payload_len = seg_len - 2;
            if (payload_len >= 6 && memcmp(payload, "Exif\0\0", 6) == 0)
                return ph_parse_tiff_orientation(payload + 6, payload_len - 6);
        }
        i += 2 + seg_len;
    }
    return 1;
}

int ph_exif_orientation_from_webp(const uint8_t *data, size_t len) {
    if (len < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WEBP", 4) != 0)
        return 1;

    size_t i = 12;
    while (i + 8 <= len) {
        const uint8_t *fourcc = data + i;
        uint32_t chunk_size = (uint32_t)data[i + 4] | ((uint32_t)data[i + 5] << 8) |
                              ((uint32_t)data[i + 6] << 16) | ((uint32_t)data[i + 7] << 24);
        size_t payload_off = i + 8;
        if (chunk_size > len - payload_off)
            break; // Malformed size: stop rather than read out of bounds.

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

void ph_apply_exif_orientation(uint8_t **data, int *width, int *height, int channels,
                               int orientation) {
    if (!data || !*data || !width || !height || orientation <= 1 || orientation > 8)
        return;

    int W = *width, H = *height;
    int Wd, Hd;
    if (orientation >= 5) {
        Wd = H;
        Hd = W;
    } else {
        Wd = W;
        Hd = H;
    }

    size_t out_size;
    if (!ph_safe_image_alloc_size((uint64_t)Wd, (uint64_t)Hd, (uint64_t)channels, &out_size))
        return; // Can't safely size the output; leave the image untouched.

    uint8_t *out = (uint8_t *)malloc(out_size);
    if (!out)
        return;

    const uint8_t *src = *data;
    for (int oy = 0; oy < Hd; oy++) {
        for (int ox = 0; ox < Wd; ox++) {
            int sx, sy;
            switch (orientation) {
                case 2:
                    sx = W - 1 - ox;
                    sy = oy;
                    break;
                case 3:
                    sx = W - 1 - ox;
                    sy = H - 1 - oy;
                    break;
                case 4:
                    sx = ox;
                    sy = H - 1 - oy;
                    break;
                case 5:
                    sx = oy;
                    sy = ox;
                    break;
                case 6:
                    sx = oy;
                    sy = H - 1 - ox;
                    break;
                case 7:
                    sx = W - 1 - oy;
                    sy = H - 1 - ox;
                    break;
                case 8:
                    sx = W - 1 - oy;
                    sy = ox;
                    break;
                default:
                    sx = ox;
                    sy = oy;
                    break;
            }
            memcpy(out + ((size_t)oy * Wd + ox) * channels, src + ((size_t)sy * W + sx) * channels,
                   (size_t)channels);
        }
    }

    ph_free_image(*data);
    *data = out;
    *width = Wd;
    *height = Hd;
}
