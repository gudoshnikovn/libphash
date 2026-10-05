#!/usr/bin/env python3
"""Generates the JPEG decoder fixtures in tests/data/jpeg/.

One small image, 61x45 (odd, so no dimension is a whole number of MCUs), encoded once per
coding mode the JPEG decoder has to handle: each chroma subsampling, grayscale,
progressive, restart markers and arithmetic coding. tests/src/test_jpeg_variants.c pins a
checksum of the decoded pixels for each file, in colour and in grayscale, at every
decode_scale, so a change to how the decoder is configured shows up as a failing test
rather than as a quiet shift in hash values.

The source pixels follow an integer formula (no floating point), and the encoder is
libjpeg-turbo's own cjpeg, from the vendored submodule:

    cmake -S vendor/libjpeg-turbo -B /tmp/ljt -DENABLE_SHARED=OFF
    cmake --build /tmp/ljt --target cjpeg-static
    CJPEG=/tmp/ljt/cjpeg-static python3 scripts/gen_jpeg_fixtures.py

Run from the repository root.
"""

import os
import subprocess
import tempfile

OUT = os.path.join("tests", "data", "jpeg")
WIDTH, HEIGHT = 61, 45
QUALITY = "85"

# (file name, cjpeg options)
VARIANTS = [
    ("s444.jpg", ["-sample", "1x1"]),
    ("s422.jpg", ["-sample", "2x1"]),
    ("s440.jpg", ["-sample", "1x2"]),
    ("s420.jpg", ["-sample", "2x2"]),
    ("s411.jpg", ["-sample", "4x1"]),
    ("gray.jpg", ["-grayscale"]),
    ("progressive.jpg", ["-sample", "2x2", "-progressive"]),
    ("restart.jpg", ["-sample", "2x2", "-restart", "1B"]),
    ("arithmetic.jpg", ["-sample", "2x2", "-arithmetic"]),
]


def pixel(x, y):
    """A gradient on each channel plus a fine pattern, so every subband carries energy."""
    r = (x * 4 + y * 2) & 0xFF
    g = (255 - y * 5 + ((x ^ y) & 7) * 8) & 0xFF
    b = ((x * y) * 3 + (x // 4) * 16) & 0xFF
    return bytes((r, g, b))


def main():
    cjpeg = os.environ.get("CJPEG")
    if not cjpeg:
        raise SystemExit("set CJPEG to libjpeg-turbo's cjpeg (see the docstring)")
    os.makedirs(OUT, exist_ok=True)
    rows = b"".join(pixel(x, y) for y in range(HEIGHT) for x in range(WIDTH))
    with tempfile.TemporaryDirectory() as tmp:
        ppm = os.path.join(tmp, "src.ppm")
        with open(ppm, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (WIDTH, HEIGHT) + rows)
        for name, opts in VARIANTS:
            out = os.path.join(OUT, name)
            subprocess.run([cjpeg, "-quality", QUALITY, *opts, "-outfile", out, ppm], check=True)


if __name__ == "__main__":
    main()
