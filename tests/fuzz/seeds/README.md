# Fuzzing seeds

The tracked, read-only seed corpus of `tests/fuzz/fuzz_load.c`. libFuzzer is given it as
its *second* directory; new units go into the first one, `tests/fuzz/corpus/`, which is
ignored by git and kept across CI runs in a cache.

A seed is here to open a path into a decoder, not to load it: every format the library
accepts has at least one, as small as the format allows. The harness also reads its
configuration from the last 8 bytes of an input (see the comment at the top of
`fuzz_load.c`), so any image file works as a seed as it is.

| Seeds | Origin |
|---|---|
| `bmp_*`, `gif_*`, `tga_*`, `psd_*`, `hdr_*`, `pnm_*` | Written by `tests/fuzz/make_seeds.py` (4x4, standard library only; rerunning it rewrites the same bytes). The formats stb_image decodes in every build, whatever native decoders are compiled in. |
| `png_*.png` | Copies of `tests/data/png/` fixtures: bit depths 1, 4 and 16, palette with `tRNS`, RGBA, a bad CRC, a truncated file. |
| `jpeg_*.jpg` | Copies of `tests/data/jpeg/` fixtures: grayscale, progressive, restart intervals, 4:2:0 and 4:4:4, arithmetic coding. |
| `webp_lossy.webp`, `webp_lossless.webp`, `webp_alpha.webp` | `cwebp` 1.6.0 from `tests/data/png/rgb8.png` (`-q 50 -m 6`; `-lossless`) and `rgba8.png` (`-q 50 -alpha_q 50`). |
| `webp_animated.webp` | `img2webp -loop 0 -lossy -d 100 rgb8.png rgba8.png` (two frames). The library rejects animated WebP, and that rejection is a path of its own. |
| `webp_broken_truncated.webp` | Copy of `tests/data/png/broken_truncated.webp`. |
| `hdr_zero_width.hdr` | Found by the fuzzer: an HDR header whose width reads as 0 (`+X X 4`), which the decoder accepted. The load fails with `PH_ERR_CORRUPT_DATA`. |
| `exif_*` | Hand-built JPEG and PNG files whose EXIF block is malformed in one way each (bad byte order mark, IFD offset out of bounds, giant entry count, wrong tag type, `eXIf` after `IEND`, ...): the orientation parser's error paths. |

## A crash found by the fuzzer

Minimize it (`fuzz_load -minimize_crash=1 -runs=100000 <crash-file>`), add the minimized
input here under a name that says what it exercises, and commit it together with the fix.
It then runs on every CI fuzz run as a seed, so the crash cannot come back unnoticed.
