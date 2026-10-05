#!/usr/bin/env python3
"""Writes the generated fuzzing seeds into tests/fuzz/seeds/ (standard library only).

Each one is a minimal 4x4 image in a format stb_image decodes and no native backend
claims -- BMP, GIF, TGA, PSD, HDR, PNM -- so the fuzzer starts inside those decoders
instead of having to guess their magic bytes. The output is deterministic: rerunning
the script rewrites the same bytes. tests/fuzz/seeds/README.md lists every seed and
where it comes from.

    python3 tests/fuzz/make_seeds.py
"""

import os
import struct

W = H = 4
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "seeds")


def pixel(x, y):
    """A small gradient with distinct values per channel, so no decoder sees a flat image."""
    return (x * 60 + 10, y * 60 + 20, (x + y) * 30 + 5)


def rgb_rows():
    return [[pixel(x, y) for x in range(W)] for y in range(H)]


def bmp():
    row_size = (W * 3 + 3) & ~3
    data = b""
    for row in reversed(rgb_rows()):  # bottom-up
        line = b"".join(bytes((b, g, r)) for r, g, b in row)
        data += line + b"\0" * (row_size - len(line))
    header = struct.pack("<2sIHHI", b"BM", 14 + 40 + len(data), 0, 0, 14 + 40)
    info = struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    return header + info + data


def lzw(indices, min_code_size):
    """GIF LZW, emitting a clear code before every pixel: valid and trivially decodable."""
    clear, end = 1 << min_code_size, (1 << min_code_size) + 1
    size = min_code_size + 1
    bits, nbits, out = 0, 0, bytearray()

    def emit(code):
        nonlocal bits, nbits
        bits |= code << nbits
        nbits += size
        while nbits >= 8:
            out.append(bits & 0xFF)
            bits >>= 8
            nbits -= 8

    for i in indices:
        emit(clear)
        emit(i)
    emit(end)
    if nbits:
        out.append(bits & 0xFF)
    blocks = b""
    for i in range(0, len(out), 255):
        chunk = bytes(out[i:i + 255])
        blocks += bytes((len(chunk),)) + chunk
    return bytes((min_code_size,)) + blocks + b"\0"


def gif(frames):
    palette = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)]
    out = b"GIF89a" + struct.pack("<HHBBB", W, H, 0x81, 0, 0)  # global table, 4 entries
    out += b"".join(bytes(c) for c in palette)
    if frames > 1:
        out += b"\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00"  # loop forever
    for f in range(frames):
        out += b"\x21\xf9\x04\x00" + struct.pack("<H", 10) + b"\x00\x00"  # 100 ms
        out += b"\x2c" + struct.pack("<HHHHB", 0, 0, W, H, 0)
        out += lzw([(x + y + f) % 4 for y in range(H) for x in range(W)], 2)
    return out + b"\x3b"


def tga(rle):
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 10 if rle else 2, 0, 0, 0, 0, 0, W, H, 24, 0x20)
    data = b""
    for row in rgb_rows():
        if rle:
            # One raw packet per row: the RLE path, without depending on runs.
            data += bytes((W - 1,))
        data += b"".join(bytes((b, g, r)) for r, g, b in row)
    return header + data


def psd():
    out = b"8BPS" + struct.pack(">H6xHIIHH", 1, 3, H, W, 8, 3)  # RGB, 3 channels
    out += struct.pack(">I", 0) * 3  # colour mode data, resources, layers: empty
    out += struct.pack(">H", 0)  # raw (uncompressed) image data, planar
    rows = rgb_rows()
    for c in range(3):
        out += bytes(rows[y][x][c] for y in range(H) for x in range(W))
    return out


def hdr():
    out = b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n" + b"-Y %d +X %d\n" % (H, W)
    for row in rgb_rows():
        out += b"".join(bytes((r, g, b, 128)) for r, g, b in row)  # flat: width < 8
    return out


def pnm(gray):
    rows = rgb_rows()
    if gray:
        body = bytes(rows[y][x][0] for y in range(H) for x in range(W))
        return b"P5\n%d %d\n255\n" % (W, H) + body
    body = b"".join(bytes(rows[y][x]) for y in range(H) for x in range(W))
    return b"P6\n%d %d\n255\n" % (W, H) + body


SEEDS = {
    "bmp_rgb24.bmp": bmp,
    "gif_static.gif": lambda: gif(1),
    "gif_animated.gif": lambda: gif(2),
    "tga_raw.tga": lambda: tga(False),
    "tga_rle.tga": lambda: tga(True),
    "psd_rgb_raw.psd": psd,
    "hdr_flat.hdr": hdr,
    "pnm_gray.pgm": lambda: pnm(True),
    "pnm_rgb.ppm": lambda: pnm(False),
}


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, make in SEEDS.items():
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(make())


if __name__ == "__main__":
    main()
