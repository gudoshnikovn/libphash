#include "alloc_shim.h"
#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Synthetic TIFF/EXIF buffer builders --------------------------------- */

static void put16(uint8_t *p, uint16_t v, int le) {
    if (le) {
        p[0] = (uint8_t)(v & 0xFF);
        p[1] = (uint8_t)(v >> 8);
    } else {
        p[0] = (uint8_t)(v >> 8);
        p[1] = (uint8_t)(v & 0xFF);
    }
}

static void put32(uint8_t *p, uint32_t v, int le) {
    if (le) {
        p[0] = (uint8_t)(v & 0xFF);
        p[1] = (uint8_t)((v >> 8) & 0xFF);
        p[2] = (uint8_t)((v >> 16) & 0xFF);
        p[3] = (uint8_t)((v >> 24) & 0xFF);
    } else {
        p[0] = (uint8_t)((v >> 24) & 0xFF);
        p[1] = (uint8_t)((v >> 16) & 0xFF);
        p[2] = (uint8_t)((v >> 8) & 0xFF);
        p[3] = (uint8_t)(v & 0xFF);
    }
}

/* Builds a minimal IFD0 with a single Orientation (0x0112) entry of the given
 * type/count/value, wrapped in a TIFF header. Returns the number of bytes written. */
static size_t build_tiff_orientation(uint8_t *out, int le, uint16_t tag_type, uint32_t tag_count,
                                     uint16_t value) {
    size_t off = 0;
    out[off++] = le ? 'I' : 'M';
    out[off++] = le ? 'I' : 'M';
    put16(out + off, 42, le);
    off += 2;
    put32(out + off, 8, le); // IFD0 offset
    off += 4;

    put16(out + off, 1, le); // 1 entry
    off += 2;
    put16(out + off, 0x0112, le); // tag: Orientation
    off += 2;
    put16(out + off, tag_type, le);
    off += 2;
    put32(out + off, tag_count, le);
    off += 4;
    put16(out + off, value, le); // value stored left-justified in the 4-byte field
    off += 2;
    put16(out + off, 0, le); // padding to fill the 4-byte value field
    off += 2;
    put32(out + off, 0, le); // next IFD offset: none
    off += 4;
    return off;
}

static size_t build_jpeg_with_exif(uint8_t *out, int le, uint16_t tag_type, uint32_t tag_count,
                                   uint16_t value) {
    uint8_t tiff[64];
    size_t tiff_len = build_tiff_orientation(tiff, le, tag_type, tag_count, value);

    size_t off = 0;
    out[off++] = 0xFF;
    out[off++] = 0xD8; // SOI
    out[off++] = 0xFF;
    out[off++] = 0xE1; // APP1
    uint16_t seg_len = (uint16_t)(2 + 6 + tiff_len);
    put16(out + off, seg_len, 0 /* segment length is always big-endian */);
    off += 2;
    memcpy(out + off, "Exif\0\0", 6);
    off += 6;
    memcpy(out + off, tiff, tiff_len);
    off += tiff_len;
    return off;
}

static size_t build_webp_with_exif(uint8_t *out, int le, uint16_t tag_type, uint32_t tag_count,
                                   uint16_t value, int with_exif_prefix) {
    uint8_t tiff[64];
    size_t tiff_len = build_tiff_orientation(tiff, le, tag_type, tag_count, value);
    size_t prefix_len = with_exif_prefix ? 6 : 0;
    size_t chunk_payload_len = prefix_len + tiff_len;

    size_t off = 0;
    memcpy(out + off, "RIFF", 4);
    off += 4;
    put32(out + off, 0, 1); // file size: unused by the scanner, left as 0
    off += 4;
    memcpy(out + off, "WEBP", 4);
    off += 4;

    memcpy(out + off, "EXIF", 4);
    off += 4;
    put32(out + off, (uint32_t)chunk_payload_len, 1); // RIFF sizes are little-endian
    off += 4;
    if (with_exif_prefix) {
        memcpy(out + off, "Exif\0\0", 6);
        off += 6;
    }
    memcpy(out + off, tiff, tiff_len);
    off += tiff_len;
    return off;
}

/* Wraps an arbitrary, caller-controlled raw TIFF blob in a JPEG APP1/Exif
 * segment. Unlike build_jpeg_with_exif() (which always emits a well-formed
 * TIFF header), this lets a test construct a deliberately malformed TIFF
 * structure -- bad byte-order marker, wrong magic, out-of-bounds IFD0 offset,
 * truncated IFD -- byte by byte. */
static size_t build_jpeg_with_raw_tiff(uint8_t *out, const uint8_t *tiff, size_t tiff_len) {
    size_t off = 0;
    out[off++] = 0xFF;
    out[off++] = 0xD8; // SOI
    out[off++] = 0xFF;
    out[off++] = 0xE1; // APP1
    uint16_t seg_len = (uint16_t)(2 + 6 + tiff_len);
    put16(out + off, seg_len, 0 /* segment length is always big-endian */);
    off += 2;
    memcpy(out + off, "Exif\0\0", 6);
    off += 6;
    memcpy(out + off, tiff, tiff_len);
    off += tiff_len;
    return off;
}

/* Appends one RIFF/WebP chunk (fourcc + little-endian size + payload + even
 * padding) at `off` and returns the new offset. */
static size_t append_webp_chunk(uint8_t *out, size_t off, const char *fourcc,
                                const uint8_t *payload, size_t payload_len) {
    memcpy(out + off, fourcc, 4);
    off += 4;
    put32(out + off, (uint32_t)payload_len, 1);
    off += 4;
    memcpy(out + off, payload, payload_len);
    off += payload_len;
    if (payload_len & 1)
        out[off++] = 0; // pad to even size
    return off;
}

static void put32_be_into(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Builds a minimal (invalid-as-an-image, but structurally correct) PNG stream
 * with just an eXIf chunk — the scanner only looks at the signature + chunk
 * headers, so no other chunk (IHDR/IDAT/IEND) is needed for these tests. */
static size_t build_png_with_exif(uint8_t *out, int le, uint16_t tag_type, uint32_t tag_count,
                                  uint16_t value, int with_exif_prefix) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint8_t tiff[64];
    size_t tiff_len = build_tiff_orientation(tiff, le, tag_type, tag_count, value);
    size_t prefix_len = with_exif_prefix ? 6 : 0;
    size_t chunk_len = prefix_len + tiff_len;

    size_t off = 0;
    memcpy(out + off, sig, 8);
    off += 8;

    put32_be_into(out + off, (uint32_t)chunk_len);
    off += 4;
    memcpy(out + off, "eXIf", 4);
    off += 4;
    if (with_exif_prefix) {
        memcpy(out + off, "Exif\0\0", 6);
        off += 6;
    }
    memcpy(out + off, tiff, tiff_len);
    off += tiff_len;
    put32_be_into(out + off, 0); // CRC: unchecked by the scanner
    off += 4;
    return off;
}

/* --- TIFF/IFD0 Orientation parsing --------------------------------------- */

void test_jpeg_orientation_all_values(void) {
    uint8_t buf[128];
    for (int le = 0; le <= 1; le++) {
        for (int v = 1; v <= 8; v++) {
            size_t n = build_jpeg_with_exif(buf, le, 3 /* SHORT */, 1, (uint16_t)v);
            int got = ph_exif_orientation_from_jpeg(buf, n);
            ASSERT_INT_EQ(v, got);
        }
    }
    PASS("test_jpeg_orientation_all_values");
}

void test_webp_orientation_all_values(void) {
    uint8_t buf[128];
    for (int le = 0; le <= 1; le++) {
        for (int prefix = 0; prefix <= 1; prefix++) {
            for (int v = 1; v <= 8; v++) {
                size_t n = build_webp_with_exif(buf, le, 3, 1, (uint16_t)v, prefix);
                int got = ph_exif_orientation_from_webp(buf, n);
                ASSERT_INT_EQ(v, got);
            }
        }
    }
    PASS("test_webp_orientation_all_values");
}

void test_png_orientation_all_values(void) {
    uint8_t buf[128];
    for (int le = 0; le <= 1; le++) {
        for (int prefix = 0; prefix <= 1; prefix++) {
            for (int v = 1; v <= 8; v++) {
                size_t n = build_png_with_exif(buf, le, 3, 1, (uint16_t)v, prefix);
                int got = ph_exif_orientation_from_png(buf, n);
                ASSERT_INT_EQ(v, got);
            }
        }
    }
    PASS("test_png_orientation_all_values");
}

void test_orientation_degrades_gracefully(void) {
    uint8_t buf[128];

    // Wrong tag type (LONG instead of SHORT): ignored, defaults to 1.
    size_t n = build_jpeg_with_exif(buf, 1, 4 /* LONG */, 1, 6);
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, n));

    // Wrong count.
    n = build_jpeg_with_exif(buf, 1, 3, 2, 6);
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, n));

    // Value outside the valid 1..8 range.
    n = build_jpeg_with_exif(buf, 1, 3, 1, 99);
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, n));

    // No APP1 at all: plain SOI + APP0 + immediate EOI.
    uint8_t no_exif[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00, 0xFF, 0xD9};
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(no_exif, sizeof(no_exif)));

    // Not the format at all.
    uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(garbage, sizeof(garbage)));
    ASSERT_INT_EQ(1, ph_exif_orientation_from_webp(garbage, sizeof(garbage)));
    ASSERT_INT_EQ(1, ph_exif_orientation_from_png(garbage, sizeof(garbage)));

    // First byte is 0xFF (as SOI requires) but the second isn't 0xD8.
    uint8_t almost_soi[] = {0xFF, 0x00, 0x00, 0x00};
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(almost_soi, sizeof(almost_soi)));

    // PNG with no eXIf chunk (just IHDR then IEND).
    uint8_t no_exif_png[] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R', 0,
        0,    0,   0,   0,   0,    0,    0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,
        0,    0,   0,   0,   0x00, 0x00, 0x00, 0x00, 'I',  'E',  'N',  'D',  0,   0,   0,   0};
    ASSERT_INT_EQ(1, ph_exif_orientation_from_png(no_exif_png, sizeof(no_exif_png)));

    // Truncated buffers must not crash (ASAN is the real judge here) and must
    // degrade to "no transform".
    n = build_jpeg_with_exif(buf, 1, 3, 1, 6);
    for (size_t cut = 0; cut < n; cut++) {
        ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, cut));
    }
    n = build_webp_with_exif(buf, 1, 3, 1, 6, 1);
    for (size_t cut = 0; cut < n; cut++) {
        ASSERT_INT_EQ(1, ph_exif_orientation_from_webp(buf, cut));
    }
    // Unlike the JPEG/WebP builders, a PNG chunk has a 4-byte CRC trailer the
    // scanner doesn't need — so once `cut` covers the eXIf chunk's header and
    // full payload, the tag parses correctly even with the CRC truncated away.
    n = build_png_with_exif(buf, 1, 3, 1, 6, 1);
    size_t png_payload_end =
        8 /* sig */ + 4 /* len */ + 4 /* type */ + 6 /* "Exif\0\0" */ + 26 /* tiff */;
    for (size_t cut = 0; cut < n; cut++) {
        int expected = (cut >= png_payload_end) ? 6 : 1;
        ASSERT_INT_EQ(expected, ph_exif_orientation_from_png(buf, cut));
    }

    PASS("test_orientation_degrades_gracefully");
}

/* --- TIFF header robustness: bad byte-order marker, wrong magic, IFD0 offset
 * out of bounds -------------------------------------------------------------
 *
 * All of these operate directly on a caller-crafted TIFF blob wrapped in a
 * JPEG APP1 segment, exercising ph_parse_tiff_orientation()'s early bail-out
 * checks (it has no separate entry point of its own to call directly). */
void test_tiff_header_robustness(void) {
    uint8_t buf[128];
    uint8_t tiff[32];
    size_t n;

    // Corrupt byte-order marker: neither "II" nor "MM".
    memset(tiff, 0, sizeof(tiff));
    tiff[0] = 'X';
    tiff[1] = 'Y';
    put16(tiff + 2, 42, 1);
    put32(tiff + 4, 8, 1);
    n = build_jpeg_with_raw_tiff(buf, tiff, 16);
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, n));

    // Wrong TIFF magic (must be 42, this is 43).
    tiff[0] = 'I';
    tiff[1] = 'I';
    put16(tiff + 2, 43, 1);
    put32(tiff + 4, 8, 1);
    n = build_jpeg_with_raw_tiff(buf, tiff, 16);
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, n));

    // ifd0_off far past the end of the (16-byte) TIFF buffer.
    put16(tiff + 2, 42, 1);
    put32(tiff + 4, 1000, 1);
    n = build_jpeg_with_raw_tiff(buf, tiff, 16);
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, n));

    // ifd0_off placed so that ifd0_off + 2 lands one byte past the end: the
    // offset itself is "in bounds" but the 2-byte entry-count field it points
    // at is not.
    put32(tiff + 4, 15, 1); // buffer is 16 bytes, so bytes [15,17) are OOB
    n = build_jpeg_with_raw_tiff(buf, tiff, 16);
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, n));

    // len < 8: the TIFF payload itself is too short to hold even the header.
    n = build_jpeg_with_raw_tiff(buf, tiff, 7);
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, n));

    // Byte-order marker: first byte matches "II"/"MM" but the second doesn't
    // (as opposed to both bytes being wrong, tested above).
    tiff[0] = 'I';
    tiff[1] = 'X';
    put16(tiff + 2, 42, 1);
    put32(tiff + 4, 8, 1);
    n = build_jpeg_with_raw_tiff(buf, tiff, 16);
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, n));

    tiff[0] = 'M';
    tiff[1] = 'X';
    n = build_jpeg_with_raw_tiff(buf, tiff, 16);
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, n));

    PASS("test_tiff_header_robustness");
}

/* --- IFD0 robustness: truncated entry count, tag mismatch skip ------------ */
void test_tiff_ifd_robustness(void) {
    uint8_t buf[128];
    uint8_t tiff[64];
    size_t n;

    // Declared count (3 entries) is far larger than what actually fits in the
    // buffer (room for exactly 1). The scanner must recompute the count from
    // the remaining buffer size rather than trust the declared value, and
    // still find the one entry that IS present.
    memset(tiff, 0, sizeof(tiff));
    tiff[0] = 'I';
    tiff[1] = 'I';
    put16(tiff + 2, 42, 1);
    put32(tiff + 4, 8, 1); // ifd0_off = 8
    put16(tiff + 8, 3, 1); // declared entry count: 3 (only 1 will fit)
    put16(tiff + 10, 0x0112, 1);
    put16(tiff + 12, 3, 1);                      // type SHORT
    put32(tiff + 14, 1, 1);                      // count 1
    put16(tiff + 18, 5, 1);                      // value
    n = build_jpeg_with_raw_tiff(buf, tiff, 22); // header(8)+count(2)+entry(12)
    ASSERT_INT_EQ(5, ph_exif_orientation_from_jpeg(buf, n));

    // Declared count is 1, but the buffer is truncated right after the count
    // field -- zero whole entries actually fit. The recomputed count must be
    // 0, and the loop body must not execute (no OOB read of a nonexistent
    // entry).
    put16(tiff + 8, 1, 1);                       // declared entry count: 1 (0 will fit)
    n = build_jpeg_with_raw_tiff(buf, tiff, 10); // header(8) + count(2), nothing else
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, n));

    // Two entries: the first has an unrelated tag (must be skipped via the
    // `continue` in the scan loop), the second is the real Orientation tag.
    put16(tiff + 8, 2, 1);       // 2 entries
    put16(tiff + 10, 0x0100, 1); // unrelated tag (ImageWidth)
    put16(tiff + 12, 3, 1);
    put32(tiff + 14, 1, 1);
    put16(tiff + 18, 42, 1);     // irrelevant value
    put16(tiff + 22, 0x0112, 1); // Orientation, in the second slot
    put16(tiff + 24, 3, 1);
    put32(tiff + 26, 1, 1);
    put16(tiff + 30, 7, 1);
    n = build_jpeg_with_raw_tiff(buf, tiff, 34); // header(8)+count(2)+2*entry(12)
    ASSERT_INT_EQ(7, ph_exif_orientation_from_jpeg(buf, n));

    PASS("test_tiff_ifd_robustness");
}

/* --- JPEG marker-stream resynchronization ---------------------------------
 * Exercises the byte-by-byte resync on stray fill bytes, the "FF FF" padding
 * rule, markers with no length field (SOI/EOI/TEM/RSTn), the SOS early-stop,
 * and both malformed-length rejection paths. */
void test_jpeg_marker_resync(void) {
    uint8_t buf[128];
    uint8_t tiff[32];
    size_t off, n;

    // Stray non-0xFF bytes between SOI and the real APP1: the scanner must
    // resync byte by byte rather than get stuck or misparse.
    off = 0;
    buf[off++] = 0xFF;
    buf[off++] = 0xD8; // SOI
    buf[off++] = 0x00;
    buf[off++] = 0x01;
    buf[off++] = 0x02; // garbage
    n = build_tiff_orientation(tiff, 1, 3, 1, 6);
    buf[off++] = 0xFF;
    buf[off++] = 0xE1;
    uint16_t seg_len = (uint16_t)(2 + 6 + n);
    put16(buf + off, seg_len, 0);
    off += 2;
    memcpy(buf + off, "Exif\0\0", 6);
    off += 6;
    memcpy(buf + off, tiff, n);
    off += n;
    ASSERT_INT_EQ(6, ph_exif_orientation_from_jpeg(buf, off));

    // "FF FF" padding before the real marker code.
    off = 0;
    buf[off++] = 0xFF;
    buf[off++] = 0xD8; // SOI
    buf[off++] = 0xFF;
    buf[off++] = 0xFF; // padding byte
    buf[off++] = 0xE1; // the real marker
    n = build_tiff_orientation(tiff, 1, 3, 1, 6);
    seg_len = (uint16_t)(2 + 6 + n);
    put16(buf + off, seg_len, 0);
    off += 2;
    memcpy(buf + off, "Exif\0\0", 6);
    off += 6;
    memcpy(buf + off, tiff, n);
    off += n;
    ASSERT_INT_EQ(6, ph_exif_orientation_from_jpeg(buf, off));

    // RSTn / SOI / EOI markers embedded mid-stream carry no length field and
    // must be skipped by a fixed 2-byte step rather than treated as segments.
    off = 0;
    buf[off++] = 0xFF;
    buf[off++] = 0xD8; // SOI
    buf[off++] = 0xFF;
    buf[off++] = 0xD0; // RST0
    buf[off++] = 0xFF;
    buf[off++] = 0xD9; // EOI (not actually terminal to this scanner)
    buf[off++] = 0xFF;
    buf[off++] = 0x01; // TEM
    n = build_tiff_orientation(tiff, 1, 3, 1, 4);
    buf[off++] = 0xFF;
    buf[off++] = 0xE1;
    seg_len = (uint16_t)(2 + 6 + n);
    put16(buf + off, seg_len, 0);
    off += 2;
    memcpy(buf + off, "Exif\0\0", 6);
    off += 6;
    memcpy(buf + off, tiff, n);
    off += n;
    ASSERT_INT_EQ(4, ph_exif_orientation_from_jpeg(buf, off));

    // SOS ends the marker scan: any APP1 placed after it must be ignored.
    off = 0;
    buf[off++] = 0xFF;
    buf[off++] = 0xD8; // SOI
    buf[off++] = 0xFF;
    buf[off++] = 0xE0; // APP0
    put16(buf + off, 4, 0);
    off += 2;
    buf[off++] = 0;
    buf[off++] = 0;
    buf[off++] = 0xFF;
    buf[off++] = 0xDA; // SOS: scan stops here
    n = build_tiff_orientation(tiff, 1, 3, 1, 8);
    buf[off++] = 0xFF;
    buf[off++] = 0xE1;
    seg_len = (uint16_t)(2 + 6 + n);
    put16(buf + off, seg_len, 0);
    off += 2;
    memcpy(buf + off, "Exif\0\0", 6);
    off += 6;
    memcpy(buf + off, tiff, n);
    off += n;
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, off)); // SOS came first

    // Malformed segment length: seg_len < 2.
    off = 0;
    buf[off++] = 0xFF;
    buf[off++] = 0xD8;
    buf[off++] = 0xFF;
    buf[off++] = 0xE1;
    put16(buf + off, 1, 0); // invalid: a segment length always counts itself
    off += 2;
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, off));

    // Malformed segment length: declared length runs past the buffer.
    off = 0;
    buf[off++] = 0xFF;
    buf[off++] = 0xD8;
    buf[off++] = 0xFF;
    buf[off++] = 0xE1;
    put16(buf + off, 1000, 0); // far larger than the (tiny) remaining buffer
    off += 2;
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, off));

    // An APP1 segment that is NOT an Exif payload (e.g. XMP): the "Exif\0\0"
    // prefix check must fail and the scan must fall through to the next
    // segment instead of misinterpreting the payload.
    off = 0;
    buf[off++] = 0xFF;
    buf[off++] = 0xD8; // SOI
    buf[off++] = 0xFF;
    buf[off++] = 0xE1; // APP1, but not Exif
    static const uint8_t xmp[] = "http://ns.adobe.com/xap/1.0/\0<x:xmpmeta/>";
    uint16_t xmp_seg_len = (uint16_t)(2 + sizeof(xmp));
    put16(buf + off, xmp_seg_len, 0);
    off += 2;
    memcpy(buf + off, xmp, sizeof(xmp));
    off += sizeof(xmp);
    buf[off++] = 0xFF;
    buf[off++] = 0xD9; // EOI
    ASSERT_INT_EQ(1, ph_exif_orientation_from_jpeg(buf, off));

    PASS("test_jpeg_marker_resync");
}

/* --- WebP chunk-chain walk -------------------------------------------------
 * Every prior WebP test placed the EXIF chunk immediately after the RIFF
 * header, so the scan loop's body -- the part that skips a non-EXIF chunk and
 * advances to the next one -- never actually ran. Real encoders put VP8X
 * first, then optional chunks like ICCP/ANIM/ALPH, with EXIF near the end. */
void test_webp_exif_not_first_chunk(void) {
    uint8_t buf[256];
    size_t off = 0;
    memcpy(buf + off, "RIFF", 4);
    off += 4;
    put32(buf + off, 0, 1); // file size: unused by the scanner
    off += 4;
    memcpy(buf + off, "WEBP", 4);
    off += 4;

    static const uint8_t vp8x[10] = {0};
    off = append_webp_chunk(buf, off, "VP8X", vp8x, sizeof(vp8x));
    static const uint8_t iccp[5] = {1, 2, 3, 4, 5}; // odd length: exercises the pad-to-even step
    off = append_webp_chunk(buf, off, "ICCP", iccp, sizeof(iccp));
    static const uint8_t anim[6] = {0};
    off = append_webp_chunk(buf, off, "ANIM", anim, sizeof(anim));

    uint8_t tiff[64];
    size_t tn = build_tiff_orientation(tiff, 1, 3, 1, 7);
    off = append_webp_chunk(buf, off, "EXIF", tiff, tn);

    ASSERT_INT_EQ(7, ph_exif_orientation_from_webp(buf, off));
    PASS("test_webp_exif_not_first_chunk");
}

/* --- PNG: a real eXIf chunk placed after IEND must be ignored -------------- */
void test_png_iend_before_exif(void) {
    uint8_t buf[256];
    size_t off = 0;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    memcpy(buf + off, sig, 8);
    off += 8;

    // IHDR (contents irrelevant to the scanner).
    put32_be_into(buf + off, 13);
    off += 4;
    memcpy(buf + off, "IHDR", 4);
    off += 4;
    memset(buf + off, 0, 13);
    off += 13;
    put32_be_into(buf + off, 0); // CRC, unchecked
    off += 4;

    // IEND terminates the chunk stream.
    put32_be_into(buf + off, 0);
    off += 4;
    memcpy(buf + off, "IEND", 4);
    off += 4;
    put32_be_into(buf + off, 0);
    off += 4;

    // A structurally valid eXIf chunk placed AFTER IEND: the scanner must
    // have already stopped and never reach it.
    uint8_t tiff[64];
    size_t tn = build_tiff_orientation(tiff, 1, 3, 1, 6);
    put32_be_into(buf + off, (uint32_t)tn);
    off += 4;
    memcpy(buf + off, "eXIf", 4);
    off += 4;
    memcpy(buf + off, tiff, tn);
    off += tn;
    put32_be_into(buf + off, 0);
    off += 4;

    ASSERT_INT_EQ(1, ph_exif_orientation_from_png(buf, off));
    PASS("test_png_iend_before_exif");
}

/* --- PNG: chunk truncated before its 4-byte CRC trailer -------------------
 * A chunk whose payload fits but whose CRC trailer would run past the
 * buffer must bail out (rather than compute an advance that wraps past the
 * end of the buffer) instead of reading OOB. */
void test_png_chunk_truncated_before_crc(void) {
    uint8_t buf[64];
    size_t off = 0;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    memcpy(buf + off, sig, 8);
    off += 8;

    // An ordinary (non-eXIf, non-IEND) chunk whose payload is present in
    // full, but the buffer ends right there -- no room for its 4-byte CRC.
    put32_be_into(buf + off, 13);
    off += 4;
    memcpy(buf + off, "IHDR", 4);
    off += 4;
    memset(buf + off, 0, 13);
    off += 13; // buffer ends here: no CRC trailer at all

    ASSERT_INT_EQ(1, ph_exif_orientation_from_png(buf, off));
    PASS("test_png_chunk_truncated_before_crc");
}

/* --- Pixel transform ------------------------------------------------------ */

/* 3x2 single-channel source, values labeled by position so a transform's
 * correctness can be read off directly:
 *   0 1 2
 *   3 4 5
 * Expected outputs below were derived by hand from what each EXIF Orientation
 * value means physically (rotate/mirror), not from the implementation's
 * formulas, so this is an independent check. */
void test_apply_orientation_known_values(void) {
    static const uint8_t src[6] = {0, 1, 2, 3, 4, 5};
    struct {
        int orientation;
        int wd, hd;
        uint8_t expected[6];
    } cases[] = {
        {1, 3, 2, {0, 1, 2, 3, 4, 5}}, {2, 3, 2, {2, 1, 0, 5, 4, 3}}, {3, 3, 2, {5, 4, 3, 2, 1, 0}},
        {4, 3, 2, {3, 4, 5, 0, 1, 2}}, {5, 2, 3, {0, 3, 1, 4, 2, 5}}, {6, 2, 3, {3, 0, 4, 1, 5, 2}},
        {7, 2, 3, {5, 2, 4, 1, 3, 0}}, {8, 2, 3, {2, 5, 1, 4, 0, 3}},
    };

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        uint8_t *data = (uint8_t *)malloc(6);
        memcpy(data, src, 6);
        int w = 3, h = 2;
        ph_apply_exif_orientation(&data, &w, &h, 1, cases[c].orientation);
        ASSERT_INT_EQ(cases[c].wd, w);
        ASSERT_INT_EQ(cases[c].hd, h);
        if (memcmp(data, cases[c].expected, 6) != 0) {
            fprintf(stderr, "[FAIL] orientation %d: got {%d,%d,%d,%d,%d,%d}\n",
                    cases[c].orientation, data[0], data[1], data[2], data[3], data[4], data[5]);
            exit(1);
        }
        free(data);
    }
    PASS("test_apply_orientation_known_values");
}

void test_apply_orientation_noop_and_invalid(void) {
    uint8_t *data = (uint8_t *)malloc(6);
    memcpy(data, (uint8_t[]){0, 1, 2, 3, 4, 5}, 6);
    int w = 3, h = 2;

    ph_apply_exif_orientation(&data, &w, &h, 1, 1); // orientation 1: no-op
    ASSERT_INT_EQ(3, w);
    ASSERT_INT_EQ(2, h);
    ASSERT_UINT8_EQ(0, data[0]);

    ph_apply_exif_orientation(&data, &w, &h, 1, 0); // out of range: no-op
    ph_apply_exif_orientation(&data, &w, &h, 1, 9); // out of range: no-op
    ASSERT_INT_EQ(3, w);
    ASSERT_INT_EQ(2, h);

    // Every argument the top-of-function guard checks, exercised on its own:
    // none of these may crash, and none may touch `data`/`w`/`h`.
    uint8_t *null_data = NULL;
    ph_apply_exif_orientation(NULL, &w, &h, 1, 2);       // data == NULL
    ph_apply_exif_orientation(&null_data, &w, &h, 1, 2); // *data == NULL
    ph_apply_exif_orientation(&data, NULL, &h, 1, 2);    // width == NULL
    ph_apply_exif_orientation(&data, &w, NULL, 1, 2);    // height == NULL
    ph_apply_exif_orientation(&data, &w, &h, 0, 2);      // channels == 0
    ph_apply_exif_orientation(&data, &w, &h, -1, 2);     // channels < 0
    ASSERT_INT_EQ(3, w);
    ASSERT_INT_EQ(2, h);
    ASSERT_UINT8_EQ(0, data[0]);

    free(data);
    PASS("test_apply_orientation_noop_and_invalid");
}

/* Applying an orientation and then its inverse must recover the exact original
 * buffer. 6 and 8 are each other's inverse; the rest are self-inverse. Uses a
 * larger, asymmetric 5x4 buffer (unique byte per pixel) to catch indexing bugs
 * the small 3x2 hand-checked case might not exercise. */
void test_apply_orientation_roundtrip(void) {
    const int W = 5, H = 4;
    uint8_t original[20];
    for (int i = 0; i < 20; i++)
        original[i] = (uint8_t)i;

    int inverse_of[9] = {0, 1, 2, 3, 4, 5, 8, 7, 6}; // index by orientation 1..8

    for (int o = 1; o <= 8; o++) {
        uint8_t *data = (uint8_t *)malloc(20);
        memcpy(data, original, 20);
        int w = W, h = H;

        ph_apply_exif_orientation(&data, &w, &h, 1, o);
        ph_apply_exif_orientation(&data, &w, &h, 1, inverse_of[o]);

        ASSERT_INT_EQ(W, w);
        ASSERT_INT_EQ(H, h);
        if (memcmp(data, original, 20) != 0) {
            fprintf(stderr, "[FAIL] orientation %d round-trip mismatch\n", o);
            exit(1);
        }
        free(data);
    }
    PASS("test_apply_orientation_roundtrip");
}

/* Reference implementation: the straightforward per-pixel transform, kept here
 * verbatim so the optimized row-wise/tiled version in src/image/orient.c can be
 * held to bit-exact equality with it. Deliberately naive — it is the spec, not
 * the fast path. */
static uint8_t *reference_orient(const uint8_t *src, int W, int H, int channels, int orientation,
                                 int *out_w, int *out_h) {
    int Wd = (orientation >= 5) ? H : W;
    int Hd = (orientation >= 5) ? W : H;
    uint8_t *out = (uint8_t *)malloc((size_t)Wd * Hd * channels);
    ASSERT_PTR_NOT_NULL(out);

    for (int oy = 0; oy < Hd; oy++) {
        for (int ox = 0; ox < Wd; ox++) {
            int sx, sy;
            switch (orientation) {
                case 2:
                    sx = W - 1 - ox, sy = oy;
                    break;
                case 3:
                    sx = W - 1 - ox, sy = H - 1 - oy;
                    break;
                case 4:
                    sx = ox, sy = H - 1 - oy;
                    break;
                case 5:
                    sx = oy, sy = ox;
                    break;
                case 6:
                    sx = oy, sy = H - 1 - ox;
                    break;
                case 7:
                    sx = W - 1 - oy, sy = H - 1 - ox;
                    break;
                case 8:
                    sx = W - 1 - oy, sy = ox;
                    break;
                default:
                    sx = ox, sy = oy;
                    break;
            }
            memcpy(out + ((size_t)oy * Wd + ox) * channels, src + ((size_t)sy * W + sx) * channels,
                   (size_t)channels);
        }
    }
    *out_w = Wd;
    *out_h = Hd;
    return out;
}

/* Every orientation, over sizes that straddle the 32x32 tile edge of the
 * transposing path (1xN, Nx1, odd, exactly one tile, one tile plus a remainder)
 * and over every channel count the loaders can hand in. */
void test_apply_orientation_matches_reference(void) {
    static const struct {
        int w, h;
    } sizes[] = {{1, 1},   {1, 7},   {7, 1},   {3, 5},   {5, 3},    {31, 33},
                 {32, 32}, {33, 31}, {64, 64}, {65, 63}, {17, 129}, {129, 17}};
    // 5 is not a real image channel count, but it exercises the `default:`
    // branch of ph_orient_copy_px()'s switch (channels 1..4 each have a
    // spelled-out case; anything else falls through to a generic memcpy).
    static const int channel_counts[] = {1, 2, 3, 4, 5};

    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        for (size_t c = 0; c < sizeof(channel_counts) / sizeof(channel_counts[0]); c++) {
            const int W = sizes[s].w, H = sizes[s].h, ch = channel_counts[c];
            const size_t n = (size_t)W * H * ch;

            uint8_t *original = (uint8_t *)malloc(n);
            ASSERT_PTR_NOT_NULL(original);
            for (size_t i = 0; i < n; i++)
                original[i] = (uint8_t)(i * 31u + 7u); // Distinct-ish, catches index swaps.

            for (int o = 1; o <= 8; o++) {
                int ref_w = 0, ref_h = 0;
                uint8_t *ref = reference_orient(original, W, H, ch, o, &ref_w, &ref_h);

                uint8_t *got = (uint8_t *)malloc(n);
                ASSERT_PTR_NOT_NULL(got);
                memcpy(got, original, n);
                int w = W, h = H;
                ph_apply_exif_orientation(&got, &w, &h, ch, o);

                if (w != ref_w || h != ref_h || memcmp(got, ref, n) != 0) {
                    fprintf(stderr, "[FAIL] orientation %d, %dx%d x%d: got %dx%d, expected %dx%d\n",
                            o, W, H, ch, w, h, ref_w, ref_h);
                    exit(1);
                }
                free(got);
                free(ref);
            }
            free(original);
        }
    }
    PASS("test_apply_orientation_matches_reference");
}

/* --- Allocation-failure robustness in the pixel transform ------------------
 * ph_apply_exif_orientation() has exactly two allocation-adjacent failure
 * paths: ph_safe_image_alloc_size() refusing an output size that would
 * overflow size_t arithmetic, and the malloc() for the output buffer itself
 * returning NULL. Either must leave the caller's buffer/width/height
 * completely untouched rather than crash or half-apply the transform. */
void test_apply_orientation_alloc_failure(void) {
    if (!PH_SHIM_SUPPORTED) {
        PASS("test_apply_orientation_alloc_failure (skipped: alloc shim unsupported on this "
             "platform)");
        return;
    }

    // 1. ph_safe_image_alloc_size() overflow: dimensions chosen so that
    // Wd*Hd*channels overflows size_t before any allocation is attempted.
    // No allocation happens on this path at all, so the shim doesn't even
    // need to be armed -- this is really a pure-arithmetic guard, tested here
    // because it lives right next to the malloc() failure path below and
    // guards the same call site.
    uint8_t *tiny = (uint8_t *)malloc(4);
    ASSERT_PTR_NOT_NULL(tiny);
    tiny[0] = 1;
    tiny[1] = 2;
    tiny[2] = 3;
    tiny[3] = 4;
    uint8_t *saved_ptr = tiny;
    int w = 2000000000, h = 2000000000;             // both within INT_MAX
    ph_apply_exif_orientation(&tiny, &w, &h, 5, 2); // 2e9 * 2e9 * 5 overflows size_t
    ASSERT(tiny == saved_ptr);                      // untouched: bailed out before any malloc
    ASSERT_INT_EQ(2000000000, w);
    ASSERT_INT_EQ(2000000000, h);
    ASSERT_UINT8_EQ(1, tiny[0]);
    free(tiny);

    // 2. malloc() itself failing: a normal, modest-size transform, but the
    // shim fails the very first allocation made while armed -- which is
    // exactly the output buffer malloc(), since ph_apply_exif_orientation()
    // makes no other heap allocation.
    uint8_t *data = (uint8_t *)malloc(6);
    ASSERT_PTR_NOT_NULL(data);
    memcpy(data, (uint8_t[]){0, 1, 2, 3, 4, 5}, 6);
    int w2 = 3, h2 = 2;
    uint8_t *before = data;

    ph_shim_arm(1);
    ph_apply_exif_orientation(&data, &w2, &h2, 1, 2);
    long injected = ph_shim_injected();
    ph_shim_disarm();

    ASSERT_INT_EQ(1, (int)injected); // confirms the injection actually fired
    ASSERT(data == before);          // untouched: malloc() returned NULL
    ASSERT_INT_EQ(3, w2);
    ASSERT_INT_EQ(2, h2);
    ASSERT_UINT8_EQ(0, data[0]);
    free(data);

    PASS("test_apply_orientation_alloc_failure");
}

/* --- Load-path parity: ph_load_from_memory() vs. ph_load_from_file() ------
 * Both public entry points are documented to apply EXIF auto-orientation
 * identically; both now funnel through the same ph_load_encoded_bytes() in
 * core.c. This pins that as an explicit regression test (auto-orientation
 * used to be applied on the file path only in some build configurations)
 * rather than relying on the shared code path staying that way by accident. */
void test_auto_orient_load_path_parity(void) {
    FILE *f = fopen(TEST_DATA_DIR "/photo.jpeg", "rb");
    ASSERT_PTR_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *orig = (uint8_t *)malloc((size_t)sz);
    ASSERT_INT_EQ((int)sz, (int)fread(orig, 1, (size_t)sz, f));
    fclose(f);

    uint8_t app1[128];
    size_t app1_len = build_jpeg_with_exif(app1, 1, 3, 1, 6) - 2;
    size_t tagged_len = (size_t)sz + app1_len;
    uint8_t *tagged = (uint8_t *)malloc(tagged_len);
    memcpy(tagged, orig, 2);
    memcpy(tagged + 2, app1 + 2, app1_len);
    memcpy(tagged + 2 + app1_len, orig + 2, (size_t)sz - 2);

    char tmp_path[] = "/tmp/ph_exif_parity_test.XXXXXX";
    int fd = mkstemp(tmp_path);
    ASSERT(fd >= 0);
    FILE *out = fdopen(fd, "wb");
    ASSERT_PTR_NOT_NULL(out);
    ASSERT_INT_EQ((int)tagged_len, (int)fwrite(tagged, 1, tagged_len, out));
    fclose(out);

    ph_context_t *ctx_mem = NULL, *ctx_file = NULL, *ctx_plain = NULL;
    ASSERT_OK(ph_create(&ctx_mem));
    ASSERT_OK(ph_create(&ctx_file));
    ASSERT_OK(ph_create(&ctx_plain));

    ASSERT_OK(ph_load_from_memory(ctx_mem, tagged, tagged_len));
    ASSERT_OK(ph_load_from_file(ctx_file, tmp_path));

    uint64_t hash_mem = 0, hash_file = 0, hash_plain = 0;
    ASSERT_OK(ph_compute_ahash(ctx_mem, &hash_mem));
    ASSERT_OK(ph_compute_ahash(ctx_file, &hash_file));
    ASSERT_UINT64_EQ(hash_mem, hash_file);

    // Both must have actually applied the rotation, not merely agree while
    // both silently skip it: compare against the untagged plain load.
    ASSERT_OK(ph_load_from_memory(ctx_plain, orig, (size_t)sz));
    ASSERT_OK(ph_compute_ahash(ctx_plain, &hash_plain));
    int dist = ph_hamming_distance(hash_plain, hash_mem);
    if (dist < 10) {
        fprintf(stderr,
                "[FAIL] test_auto_orient_load_path_parity: rotation not applied, distance was "
                "only %d\n",
                dist);
        exit(1);
    }

    ph_free(ctx_mem);
    ph_free(ctx_file);
    ph_free(ctx_plain);
    free(orig);
    free(tagged);
    remove(tmp_path);
    PASS("test_auto_orient_load_path_parity");
}

/* --- End-to-end: real JPEG + spliced-in synthetic EXIF -------------------- */

void test_auto_orient_e2e(void) {
    FILE *f = fopen(TEST_DATA_DIR "/photo.jpeg", "rb");
    ASSERT_PTR_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *orig = (uint8_t *)malloc((size_t)sz);
    ASSERT_INT_EQ((int)sz, (int)fread(orig, 1, (size_t)sz, f));
    fclose(f);

    // Splice a synthetic APP1/Exif segment (Orientation=6) right after SOI.
    uint8_t app1[128];
    size_t app1_len = build_jpeg_with_exif(app1, 1, 3, 1, 6) - 2; // minus the SOI we already have
    size_t tagged_len = (size_t)sz + app1_len;
    uint8_t *tagged = (uint8_t *)malloc(tagged_len);
    memcpy(tagged, orig, 2);                // SOI
    memcpy(tagged + 2, app1 + 2, app1_len); // APP1 segment only
    memcpy(tagged + 2 + app1_len, orig + 2, (size_t)sz - 2);

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Default (auto-orient on): the rotation must actually be applied,
    // producing a hash that differs substantially from the untagged original.
    ASSERT_OK(ph_load_from_memory(ctx, tagged, tagged_len));
    uint64_t hash_default = 0;
    ASSERT_OK(ph_compute_ahash(ctx, &hash_default));

    ASSERT_OK(ph_load_from_memory(ctx, orig, (size_t)sz));
    uint64_t hash_plain = 0;
    ASSERT_OK(ph_compute_ahash(ctx, &hash_plain));

    int dist = ph_hamming_distance(hash_plain, hash_default);
    if (dist < 10) {
        fprintf(stderr,
                "[FAIL] test_auto_orient_e2e: expected the default (auto-orient on) to "
                "substantially change the hash of a 90-degree rotation, distance was only %d\n",
                dist);
        exit(1);
    }

    // Explicitly disabled: the tag must be ignored, matching plain photo.jpeg.
    ph_context_set_auto_orient(ctx, 0);
    ASSERT_OK(ph_load_from_memory(ctx, tagged, tagged_len));
    uint64_t hash_off = 0;
    ASSERT_OK(ph_compute_ahash(ctx, &hash_off));
    ASSERT_UINT64_EQ(hash_plain, hash_off);

    free(orig);
    free(tagged);
    ph_free(ctx);
    PASS("test_auto_orient_e2e");
}

int main(void) {
    test_jpeg_orientation_all_values();
    test_webp_orientation_all_values();
    test_png_orientation_all_values();
    test_orientation_degrades_gracefully();
    test_tiff_header_robustness();
    test_tiff_ifd_robustness();
    test_jpeg_marker_resync();
    test_webp_exif_not_first_chunk();
    test_png_iend_before_exif();
    test_png_chunk_truncated_before_crc();
    test_apply_orientation_known_values();
    test_apply_orientation_noop_and_invalid();
    test_apply_orientation_roundtrip();
    test_apply_orientation_matches_reference();
    test_apply_orientation_alloc_failure();
    test_auto_orient_load_path_parity();
    test_auto_orient_e2e();
    return 0;
}
