# 2026-10-04 — PNG decoding: libpng against spng

**Question.** Is spng, as an alternative PNG backend, faster than libpng?

**Answer.** No. On 8-bit color photographs spng is within a few percent of libpng, at
best 8.6% faster (x86-64, 20 MP RGB decoded to gray); its largest gain, 17%, is 0.7 ms on a
48 KB palette screenshot decoded to RGB. On 16-bit and grayscale images it is 30–70% slower
on every platform. spng is not a backend of the library.

## What was measured

| | |
|---|---|
| Library code | `32d84b9` |
| Benchmark code | tag `bench/2026-10-04-png-libpng-vs-spng` (`ba2228b`: `scripts/gen_png_corpus.py`, `scripts/bench_png_backends.sh`, workflow `bench-png-backends.yml`) |
| CI run | [37169860325](https://github.com/gudoshnikovn/libphash/actions/runs/37169860325), 2026-10-04 02:03 UTC, 7 rounds |
| Local run | 2026-10-04, 5 rounds |
| Raw data | [`data/2026-10-04-png-libpng-vs-spng/`](data/2026-10-04-png-libpng-vs-spng/) (Linux only) |

| Platform | CPU | OS | Compiler | System zlib |
|---|---|---|---|---|
| Linux x86-64 | AMD EPYC 9V45 (GitHub `ubuntu-latest`) | Ubuntu 24.04, image 20260927.320.1 | GCC 13.3.0 | 1.3 (`1:1.3.dfsg-3.1ubuntu2.2`) |
| Linux arm64 | Neoverse-N2 (GitHub `ubuntu-24.04-arm`) | Ubuntu 24.04, image 20260927.135.1 | GCC 13.3.0 | 1.3 (same package) |
| macOS arm64 | Apple M3 Pro, 36 GB | macOS 26.6.2 | Apple clang 21.0.0 | Apple 1.2.12 |

Decoders: libpng 1.6.58, spng 0.7.4, zlib-ng 2.3.3. Four builds of `test_benchmark`,
`Release`, JPEG and WebP off: {libpng, spng} × {zlib-ng, system zlib}.

Corpus: eight PNGs made from `tests/data/photo_large.jpeg` (5472×3648) with Pillow 12.3.0
and numpy 2.5.3: RGB 8-bit at 20 and 2 MP, gray 8-bit at 20 MP, RGBA 8-bit, gray 16-bit and
RGB 16-bit at 9 MP, a 2560×1600 synthetic screenshot as RGB and as a 64-color palette.
Compression level 6. File sizes differ between the CI and the local tables: the same Pillow
version, built for a different platform.

Method: `test_benchmark --json load <file> <iters>`, a new context per load, decode to
gray and to RGB. Each run reports `min_ms` over its iterations; the order of the builds
rotates every round; a cell is the median of `min_ms` across rounds. **spng gain** is
(libpng − spng) / libpng: above zero, spng is faster.

## Linux x86-64

### Load to gray

| file | size | libpng+zlib-ng | spng+zlib-ng | spng gain | libpng+zlib | spng+zlib | spng gain |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 7.3 MB | 45.966374 | 68.087936 | -48.1% | 55.793856 | 77.837339 | -39.5% |
| photo_gray8_20mp.png | 13.5 MB | 57.852149 | 62.099001 | -7.3% | 76.559404 | 81.076831 | -5.9% |
| photo_rgb16_9mp.png | 9.7 MB | 139.364864 | 183.180107 | -31.4% | 178.302084 | 216.262920 | -21.3% |
| photo_rgb8_20mp.png | 22.0 MB | 132.328228 | 120.915501 | +8.6% | 159.609624 | 154.550397 | +3.2% |
| photo_rgb8_2mp.png | 1.9 MB | 13.551712 | 13.037715 | +3.8% | 18.553292 | 18.675946 | -0.7% |
| photo_rgba8_9mp.png | 10.0 MB | 67.706968 | 65.959019 | +2.6% | 90.775267 | 89.311434 | +1.6% |
| screenshot_pal8_4mp.png | 0.0 MB | 6.624196 | 6.026954 | +9.0% | 11.345661 | 12.036174 | -6.1% |
| screenshot_rgb8_4mp.png | 0.1 MB | 7.393539 | 7.957246 | -7.6% | 26.422148 | 28.206032 | -6.8% |

### Load to rgb

| file | size | libpng+zlib-ng | spng+zlib-ng | spng gain | libpng+zlib | spng+zlib | spng gain |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 7.3 MB | 57.015579 | 61.818436 | -8.4% | 66.308776 | 71.402115 | -7.7% |
| photo_gray8_20mp.png | 13.5 MB | 67.692058 | 95.886028 | -41.7% | 86.576319 | 108.185925 | -25.0% |
| photo_rgb16_9mp.png | 9.7 MB | 131.445416 | 176.534654 | -34.3% | 173.022316 | 209.824943 | -21.3% |
| photo_rgb8_20mp.png | 22.0 MB | 114.625336 | 108.887054 | +5.0% | 147.490921 | 142.703431 | +3.2% |
| photo_rgb8_2mp.png | 1.9 MB | 11.555171 | 11.483814 | +0.6% | 17.187780 | 17.132186 | +0.3% |
| photo_rgba8_9mp.png | 10.0 MB | 65.699002 | 64.149607 | +2.4% | 90.570594 | 88.946308 | +1.8% |
| screenshot_pal8_4mp.png | 0.0 MB | 2.974047 | 2.887798 | +2.9% | 8.888123 | 8.811286 | +0.9% |
| screenshot_rgb8_4mp.png | 0.1 MB | 3.759998 | 4.782124 | -27.2% | 23.966215 | 25.020315 | -4.4% |

## Linux arm64

### Load to gray

| file | size | libpng+zlib-ng | spng+zlib-ng | spng gain | libpng+zlib | spng+zlib | spng gain |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 7.3 MB | 56.857883 | 91.175457 | -60.4% | 64.098447 | 98.670673 | -53.9% |
| photo_gray8_20mp.png | 13.5 MB | 81.812884 | 88.551673 | -8.2% | 98.618592 | 104.659291 | -6.1% |
| photo_rgb16_9mp.png | 9.7 MB | 150.333375 | 242.856047 | -61.5% | 186.497250 | 277.767864 | -48.9% |
| photo_rgb8_20mp.png | 22.0 MB | 175.905809 | 168.979197 | +3.9% | 203.968648 | 197.272343 | +3.3% |
| photo_rgb8_2mp.png | 1.9 MB | 16.268353 | 17.355461 | -6.7% | 20.716749 | 21.931227 | -5.9% |
| photo_rgba8_9mp.png | 10.0 MB | 92.478114 | 90.219424 | +2.4% | 114.201652 | 112.276707 | +1.7% |
| screenshot_pal8_4mp.png | 0.0 MB | 7.456869 | 9.995747 | -34.0% | 13.550108 | 16.312986 | -20.4% |
| screenshot_rgb8_4mp.png | 0.1 MB | 8.733986 | 11.655247 | -33.4% | 29.096888 | 32.330842 | -11.1% |

### Load to rgb

| file | size | libpng+zlib-ng | spng+zlib-ng | spng gain | libpng+zlib | spng+zlib | spng gain |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 7.3 MB | 74.205831 | 76.712106 | -3.4% | 81.335424 | 84.156212 | -3.5% |
| photo_gray8_20mp.png | 13.5 MB | 108.886136 | 140.913885 | -29.4% | 125.858931 | 157.169137 | -24.9% |
| photo_rgb16_9mp.png | 9.7 MB | 142.917388 | 228.423832 | -59.8% | 179.139540 | 263.617332 | -47.2% |
| photo_rgb8_20mp.png | 22.0 MB | 159.571976 | 153.133525 | +4.0% | 187.357716 | 181.364692 | +3.2% |
| photo_rgb8_2mp.png | 1.9 MB | 14.414841 | 14.021745 | +2.7% | 18.892449 | 18.633246 | +1.4% |
| photo_rgba8_9mp.png | 10.0 MB | 89.807916 | 87.422123 | +2.7% | 111.516589 | 109.486476 | +1.8% |
| screenshot_pal8_4mp.png | 0.0 MB | 4.059531 | 3.373617 | +16.9% | 10.179321 | 9.412244 | +7.5% |
| screenshot_rgb8_4mp.png | 0.1 MB | 5.350807 | 5.285900 | +1.2% | 25.644186 | 25.568007 | +0.3% |

## macOS arm64

### Load to gray

| file | size | libpng+zlib-ng | spng+zlib-ng | spng gain | libpng+zlib | spng+zlib | spng gain |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 6.5 MB | 41.725208 | 70.497667 | -69.0% | 31.033167 | 59.999125 | -93.3% |
| photo_gray8_20mp.png | 13.6 MB | 76.781167 | 99.509958 | -29.6% | 63.298375 | 85.984167 | -35.8% |
| photo_rgb16_9mp.png | 9.7 MB | 139.409875 | 143.486625 | -2.9% | 126.511042 | 130.981542 | -3.5% |
| photo_rgb8_20mp.png | 20.4 MB | 124.414625 | 123.588500 | +0.7% | 92.972083 | 92.457458 | +0.6% |
| photo_rgb8_2mp.png | 1.8 MB | 10.820458 | 10.933792 | -1.0% | 8.483708 | 8.552167 | -0.8% |
| photo_rgba8_9mp.png | 9.3 MB | 65.851167 | 61.816250 | +6.1% | 52.308208 | 48.485875 | +7.3% |
| screenshot_pal8_4mp.png | 0.0 MB | 4.376667 | 4.606042 | -5.2% | 5.143583 | 5.395917 | -4.9% |
| screenshot_rgb8_4mp.png | 0.1 MB | 6.473833 | 6.835833 | -5.6% | 8.413875 | 8.757750 | -4.1% |

### Load to rgb

| file | size | libpng+zlib-ng | spng+zlib-ng | spng gain | libpng+zlib | spng+zlib | spng gain |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 6.5 MB | 49.913750 | 63.982667 | -28.2% | 39.169917 | 53.277542 | -36.0% |
| photo_gray8_20mp.png | 13.6 MB | 84.008917 | 133.039333 | -58.4% | 70.598333 | 119.638833 | -69.5% |
| photo_rgb16_9mp.png | 9.7 MB | 134.408750 | 136.481625 | -1.5% | 120.590625 | 123.582083 | -2.5% |
| photo_rgb8_20mp.png | 20.4 MB | 112.559042 | 108.755292 | +3.4% | 80.642000 | 77.440625 | +4.0% |
| photo_rgb8_2mp.png | 1.8 MB | 9.485083 | 9.316500 | +1.8% | 7.182875 | 6.988792 | +2.7% |
| photo_rgba8_9mp.png | 9.3 MB | 63.443042 | 61.454625 | +3.1% | 49.917042 | 48.301167 | +3.2% |
| screenshot_pal8_4mp.png | 0.0 MB | 2.020750 | 1.792542 | +11.3% | 2.809500 | 2.506500 | +10.8% |
| screenshot_rgb8_4mp.png | 0.1 MB | 4.035208 | 3.864542 | +4.2% | 5.905833 | 5.618000 | +4.9% |
