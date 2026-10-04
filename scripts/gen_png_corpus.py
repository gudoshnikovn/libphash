#!/usr/bin/env python3
"""Generate the PNG corpus for the libpng-vs-spng decode benchmark.

Usage: gen_png_corpus.py <source.jpeg> <out_dir>

Every file is derived from one real photograph (tests/data/photo_large.jpeg,
5472x3648), so the photographic entries carry real sensor detail rather than the
unrealistically compressible gradients an upscaled or synthetic image would have.
The set covers the color types and bit depths whose decode paths differ inside a
PNG decoder (filters, expansion, 16-to-8 bit stripping, palette lookup), at sizes
from a web image to a full camera frame. Requires Pillow and numpy.
"""
import os
import struct
import sys
import zlib

import numpy as np
from PIL import Image, ImageDraw


def write_png_rgb16(path, arr):
    """Pillow cannot write 16-bit RGB, so this encodes it directly: every row
    uses the Paeth filter, the one an encoder picks most often for photos."""
    h, w, _ = arr.shape
    data = arr.astype(">u2").view(np.uint8).reshape(h, w * 6).astype(np.int16)
    bpp = 6
    out = bytearray()
    prev = np.zeros(w * 6, dtype=np.int16)
    for y in range(h):
        row = data[y]
        a = np.concatenate([np.zeros(bpp, dtype=np.int16), row[:-bpp]])
        b = prev
        c = np.concatenate([np.zeros(bpp, dtype=np.int16), prev[:-bpp]])
        p = a + b - c
        pa, pb, pc = np.abs(p - a), np.abs(p - b), np.abs(p - c)
        pred = np.where((pa <= pb) & (pa <= pc), a, np.where(pb <= pc, b, c))
        out.append(4)
        out += ((row - pred) & 0xFF).astype(np.uint8).tobytes()
        prev = row

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body +
                struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 16, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(out), 6)))
        f.write(chunk(b"IEND", b""))


def screenshot(w, h):
    """A UI-like image: flat panels, thin rules and text, which is what PNG
    holds most often outside photography and where filter 0/Sub and long zlib
    matches dominate instead of Paeth on noisy rows."""
    img = Image.new("RGB", (w, h), (246, 247, 249))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 260, h], fill=(32, 36, 44))
    d.rectangle([0, 0, w, 48], fill=(52, 120, 220))
    y = 70
    i = 0
    while y < h - 30:
        d.rectangle([290, y, w - 30, y + 60], outline=(210, 214, 220),
                    fill=(255, 255, 255) if i % 2 else (250, 251, 252))
        d.text((305, y + 10), f"Row {i:04d}  status=ok  value={i * 37 % 1000:4d}  "
               "the quick brown fox jumps over the lazy dog", fill=(30, 30, 30))
        d.text((305, y + 32), "lorem ipsum dolor sit amet " * 4, fill=(110, 110, 120))
        d.text((20, y + 10), f"nav item {i}", fill=(200, 205, 215))
        y += 72
        i += 1
    return img


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    photo = Image.open(src).convert("RGB")  # 5472x3648, 20 MP

    def save(img, name, **kw):
        img.save(os.path.join(out, name), compress_level=6, **kw)

    save(photo, "photo_rgb8_20mp.png")
    save(photo.resize((1824, 1216), Image.LANCZOS), "photo_rgb8_2mp.png")
    save(photo.convert("L"), "photo_gray8_20mp.png")

    mid = photo.resize((3648, 2432), Image.LANCZOS)  # 8.9 MP
    yy, xx = np.mgrid[0:2432, 0:3648]
    r = np.hypot((xx - 1824) / 1824, (yy - 1216) / 1216)
    alpha = Image.fromarray(np.clip(255 * (1.3 - r), 0, 255).astype(np.uint8))
    rgba = mid.copy()
    rgba.putalpha(alpha)
    save(rgba, "photo_rgba8_9mp.png")

    gray16 = (np.asarray(mid.convert("L"), dtype=np.uint16) * 257).astype(np.uint16)
    # A uint16 array maps to mode "I;16" on its own (the mode= argument is deprecated).
    Image.fromarray(gray16).save(os.path.join(out, "photo_gray16_9mp.png"), compress_level=6)
    write_png_rgb16(os.path.join(out, "photo_rgb16_9mp.png"),
                    np.asarray(mid, dtype=np.uint16) * 257)

    shot = screenshot(2560, 1600)
    save(shot, "screenshot_rgb8_4mp.png")
    save(shot.quantize(64), "screenshot_pal8_4mp.png")


if __name__ == "__main__":
    main()
