#!/usr/bin/env python3
"""Generates the PNG and WebP decoder fixtures in tests/data/png/.

Each valid PNG exercises one pixel-format conversion the decoders must make, and its
pixels follow a formula that tests/src/test_png_variants.c recomputes, so the test checks
decoded values rather than only a return code. The broken files cover the stages at which
a decoder can fail: a bad header, truncated image data, a bad checksum on a critical
chunk. A bad checksum on an ancillary chunk is not a failure: the chunk is dropped.

Standard library only; the output is deterministic. Run from the repository root:

    python3 scripts/gen_png_fixtures.py
"""

import os
import struct
import zlib

OUT = os.path.join("tests", "data", "png")
SIDE = 8


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def png(width, height, bit_depth, color_type, rows, extra=b""):
    ihdr = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, 0)
    raw = b"".join(b"\x00" + row for row in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + extra +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def pack_bits(values, depth):
    """Packs samples of `depth` bits (1, 2 or 4) MSB first, padding the last byte."""
    out, acc, n = bytearray(), 0, 0
    for v in values:
        acc = (acc << depth) | v
        n += depth
        if n == 8:
            out.append(acc)
            acc, n = 0, 0
    if n:
        out.append(acc << (8 - n))
    return bytes(out)


# The formulas, mirrored in tests/src/test_png_variants.c.
def gray8(x, y):
    return (x * 29 + y * 7) & 0xFF


def rgb8(x, y):
    return ((x * 32) & 0xFF, (y * 32) & 0xFF, ((x + y) * 16) & 0xFF)


PALETTE = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)]


def palette_index(x, y):
    return (x + y) % 4


def write(name, data):
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(data)


def main():
    os.makedirs(OUT, exist_ok=True)
    xs = range(SIDE)

    # Gray 1 bit: a checkerboard of single pixels, 0 and 1 -> 0 and 255.
    write("gray1.png", png(SIDE, SIDE, 1, 0,
                           [pack_bits([(x + y) % 2 for x in xs], 1) for y in xs]))
    # Gray 4 bit: v = (x + y) % 16 -> v * 17.
    write("gray4.png", png(SIDE, SIDE, 4, 0,
                           [pack_bits([(x + y) % 16 for x in xs], 4) for y in xs]))
    # Gray 8 bit.
    write("gray8.png", png(SIDE, SIDE, 8, 0, [bytes(gray8(x, y) for x in xs) for y in xs]))
    # RGB 8 bit, and the same picture at 16 bits: every sample is v * 257 + 0x55 in the
    # low byte, so a correct 16 -> 8 reduction keeps exactly the high byte, v.
    write("rgb8.png", png(SIDE, SIDE, 8, 2,
                          [bytes(c for x in xs for c in rgb8(x, y)) for y in xs]))
    write("rgb16.png", png(SIDE, SIDE, 16, 2,
                           [b"".join(struct.pack(">H", (c << 8) | 0x55)
                                     for x in xs for c in rgb8(x, y)) for y in xs]))
    # Palette, 2 bits per index.
    plte = chunk(b"PLTE", bytes(c for rgb in PALETTE for c in rgb))
    write("palette.png", png(SIDE, SIDE, 2, 3,
                             [pack_bits([palette_index(x, y) for x in xs], 2) for y in xs],
                             plte))
    # Palette with tRNS: entry 0 (black) fully transparent, the rest opaque.
    write("palette_trns.png", png(SIDE, SIDE, 2, 3,
                                  [pack_bits([palette_index(x, y) for x in xs], 2) for y in xs],
                                  plte + chunk(b"tRNS", b"\x00")))
    # RGBA 8 bit: rgb8() with alpha 255 in even columns and 0 in odd ones.
    write("rgba8.png", png(SIDE, SIDE, 8, 6,
                           [bytes(c for x in xs
                                  for c in rgb8(x, y) + ((255,) if x % 2 == 0 else (0,)))
                            for y in xs]))

    # Broken PNGs.
    good = open(os.path.join(OUT, "rgb8.png"), "rb").read()
    # (a) valid signature, garbage where IHDR belongs.
    write("broken_header.png", good[:8] + b"\x00\x00\x00\x0dNOPE" + b"\xAA" * 17)
    # (b) valid header, image data cut off halfway through the IDAT chunk.
    idat = good.index(b"IDAT")
    write("broken_truncated.png", good[:idat + 4 + 10])
    # (c) valid structure, IDAT checksum wrong.
    crc_at = idat + 4 + struct.unpack(">I", good[idat - 4:idat])[0]
    bad = bytearray(good)
    bad[crc_at] ^= 0xFF
    write("broken_crc.png", bytes(bad))
    # (d) the IHDR checksum wrong, a critical chunk ahead of the image data.
    bad = bytearray(good)
    bad[8 + 8 + 13] ^= 0xFF
    write("broken_crc_ihdr.png", bytes(bad))
    # Not broken: an ancillary chunk with a wrong checksum is dropped and the image
    # decodes, the same pixels as rgb8.png.
    text = bytearray(chunk(b"tEXt", b"Comment\x00checksum is wrong"))
    text[-1] ^= 0xFF
    write("ancillary_bad_crc.png", good[:33] + bytes(text) + good[33:])

    # Broken WebP, from the valid fixture: (a) the RIFF/WEBP header intact and the
    # bitstream replaced with garbage, (b) the file cut after its first 256 bytes, which
    # still parse as a header.
    webp = open(os.path.join("tests", "data", "photo.webp"), "rb").read()
    write("broken_bitstream.webp", webp[:20] + b"\x5A" * 64)
    write("broken_truncated.webp", webp[:256])


if __name__ == "__main__":
    main()
