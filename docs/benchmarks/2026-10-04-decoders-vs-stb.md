# 2026-10-04 — libjpeg-turbo, libpng and zlib-ng against stb_image

**Question.** What does each vendored decoder buy over the `stb_image` fallback, in
time, memory and size?

**Answer.**
- **libjpeg-turbo** decodes JPEG to gray 1.4–6.4× faster than stb_image, with 30% less
  peak memory. Decoding at `PH_DECODE_SCALE_EIGHTH` is a further 1.1–1.6× faster to
  gray (1.1–2.3× to RGB; least on the progressive file, 1.02–1.07×) and cuts peak memory
  about ninefold (87 → 10 MB on 20 MP).
- **libpng + zlib-ng** decodes photographic PNG to gray 1.25–1.7× faster than stb_image
  on Linux and needs 83 MB of peak memory against stb_image's 141 MB (309 MB on macOS) on
  a 20 MP RGB file. On small screenshots stb_image is as fast.
- **zlib-ng** carries the Linux PNG gain: libpng on the system zlib is only 1.0–1.17×
  faster than stb_image on x86-64. On macOS, libpng on the system zlib decodes photographs
  10–35% faster than on zlib-ng.
- Size: the native build's `test_benchmark` is 0.9–1.3 MB stripped, the stb_image-only
  one 0.3–0.4 MB.

## What was measured

| | |
|---|---|
| Library code | `33c252e` |
| Benchmark code | tag `bench/2026-10-04-decoders-vs-stb` (`4c2cca6`: `scripts/gen_bench_corpus.py`, `scripts/bench_decoders.sh`, `test_benchmark` taking the decode scale from `PH_BENCH_DECODE_SCALE`, workflow `bench-decoders.yml`) |
| CI run | [37172361010](https://github.com/gudoshnikovn/libphash/actions/runs/37172361010), 2026-10-04 02:52 UTC, 7 rounds |
| Local run | 2026-10-04, 5 rounds |
| Raw data | [`data/2026-10-04-decoders-vs-stb/`](data/2026-10-04-decoders-vs-stb/) (Linux only) |

| Platform | CPU | OS | Compiler | System zlib |
|---|---|---|---|---|
| Linux x86-64 | AMD EPYC 7763 (GitHub `ubuntu-latest`) | Ubuntu 24.04, image 20260927.320.1 | GCC 13.3.0 | 1.3 (`1:1.3.dfsg-3.1ubuntu2.2`) |
| Linux arm64 | Neoverse-N2 (GitHub `ubuntu-24.04-arm`) | Ubuntu 24.04, image 20260927.135.1 | GCC 13.3.0 | 1.3 (same package) |
| macOS arm64 | Apple M3 Pro, 36 GB | macOS 26.6.2 | Apple clang 21.0.0 | Apple 1.2.12 |

The x86-64 runner is a different CPU from the one in the libpng-against-spng record of
the same day, so absolute times of the two records do not compare; ratios within a
record do.

Decoders: libjpeg-turbo 3.2.0 (libjpeg API), libpng 1.6.58, zlib-ng 2.3.3, stb_image
2.30. Three builds of `test_benchmark`, `Release`, WebP off:
- **native** — libjpeg-turbo + libpng + zlib-ng;
- **syszlib** — libpng + the system zlib, JPEG through stb_image (PNG rows only);
- **stb** — every native decoder off.

Corpus: the eight PNGs of the libpng-against-spng record, and six JPEGs from the same
photograph (Pillow 12.3.0): quality 90 at 20 MP as 4:2:0, 4:4:4, progressive 4:2:0 and
gray; quality 85 4:2:0 at 9 MP and at 2 MP.

Method: as in the libpng-against-spng record — `test_benchmark --json load`, gray and RGB,
rotated order, median of `min_ms`. **×** is stb_image's time over the native one. JPEG
also runs on the native build at `PH_DECODE_SCALE_EIGHTH` (libjpeg's DCT-domain downscale,
which stb_image does not have). Footprint: `strip`ped `test_benchmark`, and peak RSS
(`/usr/bin/time`) of one process decoding the largest file of each format once.

## Linux x86-64

### PNG, load to gray

| file | libpng+zlib-ng | libpng+zlib | stb | zlib-ng vs stb | zlib vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 65.94 | 79.28 | 83.82 | 1.27× | 1.06× |
| photo_gray8_20mp.png | 85.44 | 109.91 | 128.95 | 1.51× | 1.17× |
| photo_rgb16_9mp.png | 217.09 | 268.89 | 271.50 | 1.25× | 1.01× |
| photo_rgb8_20mp.png | 190.10 | 237.16 | 259.88 | 1.37× | 1.10× |
| photo_rgb8_2mp.png | 19.17 | 26.58 | 27.89 | 1.46× | 1.05× |
| photo_rgba8_9mp.png | 99.45 | 135.85 | 141.74 | 1.43× | 1.04× |
| screenshot_pal8_4mp.png | 9.84 | 18.01 | 7.86 | 0.80× | 0.44× |
| screenshot_rgb8_4mp.png | 8.72 | 34.94 | 12.45 | 1.43× | 0.36× |

### JPEG, load to gray

| file | libjpeg-turbo | libjpeg-turbo 1/8 | stb | turbo vs stb | turbo 1/8 vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| q85_420_2mp.jpg | 3.39 | 2.49 | 14.26 | 4.21× | 5.73× |
| q85_420_9mp.jpg | 19.48 | 15.63 | 64.79 | 3.33× | 4.14× |
| q90_420_20mp.jpg | 72.32 | 61.83 | 184.15 | 2.55× | 2.98× |
| q90_420_prog_20mp.jpg | 236.93 | 229.96 | 413.51 | 1.75× | 1.80× |
| q90_444_20mp.jpg | 77.71 | 67.53 | 206.37 | 2.66× | 3.06× |
| q90_gray_20mp.jpg | 72.62 | 62.92 | 104.19 | 1.43× | 1.66× |

### PNG, load to rgb

| file | libpng+zlib-ng | libpng+zlib | stb | zlib-ng vs stb | zlib vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 85.23 | 98.47 | 89.52 | 1.05× | 0.91× |
| photo_gray8_20mp.png | 102.63 | 127.47 | 145.33 | 1.42× | 1.14× |
| photo_rgb16_9mp.png | 206.99 | 258.92 | 262.04 | 1.27× | 1.01× |
| photo_rgb8_20mp.png | 166.65 | 213.74 | 238.98 | 1.43× | 1.12× |
| photo_rgb8_2mp.png | 16.64 | 24.02 | 25.58 | 1.54× | 1.06× |
| photo_rgba8_9mp.png | 96.90 | 129.87 | 136.09 | 1.40× | 1.05× |
| screenshot_pal8_4mp.png | 5.14 | 13.32 | 3.59 | 0.70× | 0.27× |
| screenshot_rgb8_4mp.png | 4.01 | 30.25 | 8.16 | 2.04× | 0.27× |

### JPEG, load to rgb

| file | libjpeg-turbo | libjpeg-turbo 1/8 | stb | turbo vs stb | turbo 1/8 vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| q85_420_2mp.jpg | 4.86 | 2.71 | 11.91 | 2.45× | 4.40× |
| q85_420_9mp.jpg | 25.20 | 16.49 | 54.93 | 2.18× | 3.33× |
| q90_420_20mp.jpg | 88.31 | 63.76 | 163.35 | 1.85× | 2.56× |
| q90_420_prog_20mp.jpg | 249.27 | 231.46 | 392.45 | 1.57× | 1.70× |
| q90_444_20mp.jpg | 98.82 | 71.84 | 185.16 | 1.87× | 2.58× |
| q90_gray_20mp.jpg | 87.75 | 63.12 | 120.77 | 1.38× | 1.91× |

### Footprint

| build | stripped test_benchmark | peak RSS, largest PNG | peak RSS, largest JPEG |
| :--- | ---: | ---: | ---: |
| native | 1307 KB | 83764 KB | 87744 KB (1/8: 10508 KB) |
| syszlib | 647 KB | 83680 KB | — |
| stb | 407 KB | 141780 KB | 125764 KB |

## Linux arm64

### PNG, load to gray

| file | libpng+zlib-ng | libpng+zlib | stb | zlib-ng vs stb | zlib vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 56.62 | 63.89 | 75.29 | 1.33× | 1.18× |
| photo_gray8_20mp.png | 81.48 | 98.52 | 136.46 | 1.67× | 1.39× |
| photo_rgb16_9mp.png | 149.94 | 186.18 | 228.30 | 1.52× | 1.23× |
| photo_rgb8_20mp.png | 174.62 | 202.83 | 243.08 | 1.39× | 1.20× |
| photo_rgb8_2mp.png | 16.12 | 20.59 | 23.66 | 1.47× | 1.15× |
| photo_rgba8_9mp.png | 90.93 | 112.67 | 126.97 | 1.40× | 1.13× |
| screenshot_pal8_4mp.png | 7.22 | 13.22 | 7.69 | 1.07× | 0.58× |
| screenshot_rgb8_4mp.png | 8.66 | 28.83 | 13.20 | 1.52× | 0.46× |

### JPEG, load to gray

| file | libjpeg-turbo | libjpeg-turbo 1/8 | stb | turbo vs stb | turbo 1/8 vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| q85_420_2mp.jpg | 3.40 | 2.14 | 21.74 | 6.39× | 10.14× |
| q85_420_9mp.jpg | 19.11 | 13.52 | 94.67 | 4.95× | 7.00× |
| q90_420_20mp.jpg | 69.59 | 53.80 | 266.59 | 3.83× | 4.95× |
| q90_420_prog_20mp.jpg | 197.47 | 183.83 | 430.24 | 2.18× | 2.34× |
| q90_444_20mp.jpg | 75.21 | 59.48 | 331.74 | 4.41× | 5.58× |
| q90_gray_20mp.jpg | 70.88 | 55.13 | 144.82 | 2.04× | 2.63× |

### PNG, load to rgb

| file | libpng+zlib-ng | libpng+zlib | stb | zlib-ng vs stb | zlib vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 74.08 | 81.21 | 78.85 | 1.06× | 0.97× |
| photo_gray8_20mp.png | 107.09 | 124.18 | 160.00 | 1.49× | 1.29× |
| photo_rgb16_9mp.png | 142.66 | 178.90 | 221.33 | 1.55× | 1.24× |
| photo_rgb8_20mp.png | 157.88 | 186.59 | 228.42 | 1.45× | 1.22× |
| photo_rgb8_2mp.png | 14.29 | 18.77 | 21.96 | 1.54× | 1.17× |
| photo_rgba8_9mp.png | 87.92 | 110.11 | 124.56 | 1.42× | 1.13× |
| screenshot_pal8_4mp.png | 3.85 | 9.86 | 4.64 | 1.21× | 0.47× |
| screenshot_rgb8_4mp.png | 5.30 | 25.48 | 10.12 | 1.91× | 0.40× |

### JPEG, load to rgb

| file | libjpeg-turbo | libjpeg-turbo 1/8 | stb | turbo vs stb | turbo 1/8 vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| q85_420_2mp.jpg | 5.60 | 2.42 | 20.10 | 3.59× | 8.32× |
| q85_420_9mp.jpg | 27.85 | 14.57 | 88.05 | 3.16× | 6.04× |
| q90_420_20mp.jpg | 103.32 | 56.13 | 251.77 | 2.44× | 4.49× |
| q90_420_prog_20mp.jpg | 222.77 | 186.41 | 416.18 | 1.87× | 2.23× |
| q90_444_20mp.jpg | 113.49 | 62.95 | 316.41 | 2.79× | 5.03× |
| q90_gray_20mp.jpg | 86.57 | 55.17 | 170.14 | 1.97× | 3.08× |

### Footprint

| build | stripped test_benchmark | peak RSS, largest PNG | peak RSS, largest JPEG |
| :--- | ---: | ---: | ---: |
| native | 1027 KB | 83104 KB | 86888 KB (1/8: 9980 KB) |
| syszlib | 579 KB | 83016 KB | — |
| stb | 323 KB | 141260 KB | 125268 KB |

## macOS arm64

### PNG, load to gray

| file | libpng+zlib-ng | libpng+zlib | stb | zlib-ng vs stb | zlib vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 41.16 | 30.66 | 56.07 | 1.36× | 1.83× |
| photo_gray8_20mp.png | 76.70 | 63.22 | 118.62 | 1.55× | 1.88× |
| photo_rgb16_9mp.png | 136.74 | 124.28 | 152.31 | 1.11× | 1.23× |
| photo_rgb8_20mp.png | 122.45 | 90.89 | 193.35 | 1.58× | 2.13× |
| photo_rgb8_2mp.png | 10.80 | 8.41 | 16.20 | 1.50× | 1.93× |
| photo_rgba8_9mp.png | 65.73 | 51.96 | 102.35 | 1.56× | 1.97× |
| screenshot_pal8_4mp.png | 4.37 | 5.17 | 4.18 | 0.96× | 0.81× |
| screenshot_rgb8_4mp.png | 6.46 | 8.32 | 6.36 | 0.99× | 0.76× |

### JPEG, load to gray

| file | libjpeg-turbo | libjpeg-turbo 1/8 | stb | turbo vs stb | turbo 1/8 vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| q85_420_2mp.jpg | 2.08 | 1.55 | 9.69 | 4.66× | 6.26× |
| q85_420_9mp.jpg | 12.62 | 10.14 | 44.83 | 3.55× | 4.42× |
| q90_420_20mp.jpg | 47.74 | 41.37 | 128.32 | 2.69× | 3.10× |
| q90_420_prog_20mp.jpg | 141.63 | 139.28 | 263.60 | 1.86× | 1.89× |
| q90_444_20mp.jpg | 50.88 | 46.27 | 147.78 | 2.90× | 3.19× |
| q90_gray_20mp.jpg | 47.63 | 42.23 | 82.55 | 1.73× | 1.95× |

### PNG, load to rgb

| file | libpng+zlib-ng | libpng+zlib | stb | zlib-ng vs stb | zlib vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| photo_gray16_9mp.png | 49.53 | 38.44 | 58.85 | 1.19× | 1.53× |
| photo_gray8_20mp.png | 84.06 | 70.48 | 128.40 | 1.53× | 1.82× |
| photo_rgb16_9mp.png | 131.05 | 119.89 | 146.80 | 1.12× | 1.22× |
| photo_rgb8_20mp.png | 111.42 | 79.83 | 184.69 | 1.66× | 2.31× |
| photo_rgb8_2mp.png | 9.59 | 6.90 | 15.19 | 1.58× | 2.20× |
| photo_rgba8_9mp.png | 63.47 | 49.74 | 101.45 | 1.60× | 2.04× |
| screenshot_pal8_4mp.png | 1.98 | 2.68 | 2.14 | 1.08× | 0.80× |
| screenshot_rgb8_4mp.png | 4.06 | 5.90 | 4.33 | 1.07× | 0.73× |

### JPEG, load to rgb

| file | libjpeg-turbo | libjpeg-turbo 1/8 | stb | turbo vs stb | turbo 1/8 vs stb |
| :--- | ---: | ---: | ---: | ---: | ---: |
| q85_420_2mp.jpg | 2.68 | 1.72 | 8.47 | 3.16× | 4.93× |
| q85_420_9mp.jpg | 15.52 | 10.89 | 40.11 | 2.58× | 3.68× |
| q90_420_20mp.jpg | 53.83 | 42.93 | 117.72 | 2.19× | 2.74× |
| q90_420_prog_20mp.jpg | 147.44 | 140.60 | 252.33 | 1.71× | 1.79× |
| q90_444_20mp.jpg | 59.38 | 47.42 | 137.54 | 2.32× | 2.90× |
| q90_gray_20mp.jpg | 48.32 | 42.48 | 90.60 | 1.88× | 2.13× |

### Footprint

| build | stripped test_benchmark | peak RSS, largest PNG | peak RSS, largest JPEG |
| :--- | ---: | ---: | ---: |
| native | 936 KB | 81408 KB | 86704 KB (1/8: 9440 KB) |
| syszlib | 513 KB | 81264 KB | — |
| stb | 327 KB | 309072 KB | 125280 KB |
