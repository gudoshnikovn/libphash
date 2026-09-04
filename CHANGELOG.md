# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Nothing yet.

## [2.0.0] - Unreleased

First major release. It is a major not because of the amount of new work, but because
of a small number of changes a consumer **cannot notice at upgrade time**: the hash
value produced for the same input can differ, and one error constant is gone. Read
the BREAKING CHANGES section before upgrading, and see `MIGRATION.md` for the 1.x → 2.0
walkthrough.

### BREAKING CHANGES

- **`ph_digest_t` is 136 bytes, not 72, and carries a `kind` tag.** `PH_DIGEST_MAX_BYTES`
  is now 128: the Marr-Hildreth hash is 576 bits and did not fit in 64, and the remaining
  room is headroom taken once rather than twice. The byte after `size` is now `kind`, a
  `ph_digest_kind_t` saying what the bytes are — a bit vector, transform coefficients, a
  feature vector, a histogram — so that a comparison meant for one can **refuse** the
  others instead of returning a plausible number that means nothing.
  `ph_hamming_distance_digest()` and `ph_similarity_digest()` return -1 for a radial or
  colour-moments digest; `ph_l2_distance()` returns -1 for a BMH digest. The tag never
  chooses a metric for you. `PH_DIGEST_KIND_UNSPECIFIED` is zero, so a hand-filled struct
  behaves exactly as before and simply gets no protection.
  As a knock-on, `ph_context_set_block_params()` accepts up to 32 rather than 22: the
  bound is the largest grid whose bits fit a digest, and it followed the capacity.
  *Restore the old behaviour:* rebuild any FFI binding that hardcodes the layout; where a
  comparison now returns -1, switch to the metric for that digest.

- **The Block Mean Hash thresholds against the median of the block means, not their
  arithmetic mean, so every BMH value changes.** That is what Yang, Gu and Niu's method 1
  specifies (step d and equation 3.9), and the median is what makes the bit distribution
  balanced by construction — under the mean, a dark image with a few bright blocks
  produces a lopsided hash. On photographs the two rules almost agree, so most values move
  by a bit or two; on images with skewed block values, which is the case the median exists
  to handle, they move a great deal. Note this also puts the library at odds with OpenCV's
  `BlockMeanHash`, which thresholds on the mean (in a variable it calls `median`) — expect
  BMH values to differ from OpenCV's.
  *Restore the old behaviour:* not possible; recompute any stored BMH digests.

- **Radial digests must now be compared with `ph_radial_similarity()`.** The algorithm's
  source compares two radial hashes by the peak of their cross-correlation over cyclic
  shifts, and that function exposes it, with the source's threshold available as
  `PH_RADIAL_PCC_THRESHOLD` (0.9). `ph_similarity_digest()`, `ph_hamming_distance_digest()`
  and `ph_l2_distance()` still accept a radial digest and still return a number, but that
  number treats quantised DCT coefficients as a bit vector or a point in space and does not
  mean what it appears to. Radial values also change once more in this release: the
  variance vector is standardised before the transform, as the reference implementation
  does — without it one byte of every digest was a constant 255, carrying no information
  and inflating the correlation between every pair of digests.
  *Restore the old behaviour:* not possible; recompute any stored radial digests.

  One correction to earlier documentation while you are here: the Radial hash tolerates a
  **few degrees** of rotation, plus an exact half turn — not the "up to 360°" this
  project's own docs used to claim. Measured on a photograph: 1° → 0.993, 3° → 0.944,
  5° → 0.870, 15° → 0.437, 90° → 0.243, 180° → 0.993, against 0.69 for an unrelated image.
  That is what the algorithm's source delivers, and the reference implementation behaves
  the same way; `docs/algorithm-provenance.md` §7 explains why the transform does not carry
  a larger rotation.

- **The Radial hash now applies the DCT its source specifies, and every radial value
  changes.** The algorithm (De Roover et al. 2005, as pHash implements it) computes the
  variance along one projection line per degree over 180°, then takes a 1-D DCT of that
  vector and keeps the first 40 coefficients as the hash. This library was taking 40
  *angles* and no transform — the 40 had been transplanted from the coefficient count
  onto the angle count — so the digest was a raw, still-correlated variance profile at
  4.5× coarser angular resolution. The digest is now always 40 bytes of quantised
  coefficients, `ph_context_set_radial_params()`'s first argument is the number of
  angles (40..131072, default 180) and no longer sets the digest width, and a value of
  40 does **not** reproduce the old hashes. Rotation tolerance improves as a
  side-effect — a quarter turn now moves a digest about a fifth as far as an unrelated
  image does, against about a half before — but full rotation invariance still needs
  the source's cross-correlation comparison, which this release does not add.
  *Restore the old behaviour:* not possible; recompute any stored radial digests.

- **Automatic EXIF orientation is now on by default.** Images carrying an
  `Orientation` tag other than 1 (most photos straight from phones and cameras) are
  rotated/mirrored before hashing, so a hash now describes what a viewer displays
  rather than the raw sensor buffer. **Every hash you have stored for such an image
  changes.** There is no error and no warning — only a silently lower recall in
  deduplication, so plan a rehash of the affected corpus.
  *Restore the old behaviour:* `ph_context_set_auto_orient(ctx, 0)` after
  `ph_create()`.

- **`PH_ERR_DECODE_FAILED` was removed from `ph_error_t`.** It had already stopped
  being returned from anywhere while still being declared, so
  `if (err == PH_ERR_DECODE_FAILED)` had quietly stopped matching with no compiler
  diagnostic. Removing the name makes the break visible at compile time. Its numeric
  value `-2` is retired and will not be reused.
  *Restore the old behaviour:* not possible, and not desirable — replace the check
  with the specific codes that superseded it: `PH_ERR_CORRUPT_DATA`,
  `PH_ERR_UNSUPPORTED_FORMAT`, `PH_ERR_IMAGE_TOO_LARGE`, `PH_ERR_IO`,
  `PH_ERR_DECODER_UNAVAILABLE`. A code-to-code mapping table is in `MIGRATION.md`.

- **Every `ph_context_set_*` function returns `ph_error_t` instead of `void`, and
  invalid input is now rejected.** One contract for all of them: a valid argument
  returns `PH_SUCCESS`; anything else returns `PH_ERR_INVALID_ARGUMENT` and leaves
  the configuration **completely unchanged** — never clamped, never partially
  applied, never reset to defaults. Previously invalid input was swallowed silently,
  so a caller could hash a whole batch with a configuration it never asked for.
  Existing call sites still compile (the setters are deliberately not
  `warn_unused_result`), but calls that used to be silently ignored now leave the
  previous value in place. The bounds are implementation limits, not style:
  `gamma` finite and in (0.001, 1000]; gray weights each ≥ 0 with a sum in
  (0, INT_MAX/255]; `dct_size` 1..32; `reduction_size` 1..8 and ≤ `dct_size`;
  radial `projections` 1..64; radial `samples` 1..65536; `block_size` 1..22;
  `whash_mode` a declared enumerator only.
  *Restore the old behaviour:* not possible — pass values inside the documented
  bounds, and check the return value wherever the argument comes from outside your
  own code.

- **Public helpers reading a `ph_digest_t` reject `size > PH_DIGEST_MAX_BYTES` (64)
  instead of truncating.** `ph_digest_t` is a flat struct that FFI bindings assemble
  by hand, and `size` could hold values `data` could not — `size = 200` read up to
  128 bytes past the end of the struct. Functions returning `ph_error_t` now return
  `PH_ERR_INVALID_ARGUMENT`; distance/similarity functions return `-1`. A `size` of
  `0` is also uniformly `-1` from every comparison function now (it used to be `0`
  from `ph_hamming_distance_digest()` and `ph_l2_distance()`, i.e. "identical" for
  two digests without a single bit, while `ph_similarity_digest()` already returned
  `-1`).
  *Restore the old behaviour:* not possible — it was an out-of-bounds read. Make
  sure hand-assembled digests carry a `size` of at most `PH_DIGEST_MAX_BYTES`.

- **The color algorithms refuse grayscale images.** `ph_compute_color_hash()` and
  `ph_compute_color_moments_hash()` (and `ph_compute_multi()` with
  `PH_HASH_COLOR_HASH`) return the new `PH_ERR_REQUIRES_COLOR` when the loaded image
  has fewer than 3 channels, and leave the output untouched. They previously read
  r/g/b out of one replicated channel and returned `PH_SUCCESS` with an outwardly
  valid but meaningless result.
  *Restore the old behaviour:* not possible — load the image in color
  (`ph_context_set_load_grayscale(ctx, 0)`, the default, or pass 3/4 channels to
  `ph_load_from_pixels()`) before asking for a color hash.

- **`max_pixels = 0` no longer means "no limit".** It means "no limit of my own": an
  implementation ceiling of `INT_MAX` (2147483647) pixels always applies and also
  caps an explicitly configured larger value. An image above it is rejected with
  `PH_ERR_IMAGE_TOO_LARGE`. Pixel indexing in the hot loops is done in `int`, so
  such an image previously overflowed it — undefined behaviour and a heap overflow
  reachable through the documented "0 = unlimited" mode. The default limit
  (256 MP) is eight times below the ceiling, so only callers who deliberately raised
  or disabled the limit are affected.
  *Restore the old behaviour:* not possible, by design — the previous behaviour was
  undefined.

- **A single image dimension may not exceed 1000000 pixels.** The cap applies to every
  format and to both `ph_load_from_file()` and `ph_load_from_memory()`, on top of
  `max_pixels` and independently of it — setting `max_pixels` higher, or to `0`, does
  not lift it. An image beyond it is rejected with `PH_ERR_IMAGE_TOO_LARGE`. The area
  limit on its own permits an absurd aspect ratio: a 268435456 × 1 image sits exactly
  on the default 256 MP limit, yet makes the decoder size a single row of ~800 MB.
  Real photographs are nowhere near this, so in practice only synthetic input is
  affected. `ph_load_from_pixels()` is not subject to the cap.
  *Restore the old behaviour:* not possible — split such an image yourself, or decode
  it with your own decoder and pass the pixels to `ph_load_from_pixels()`.

- **`PH_VERSION_NUMBER` uses a new scheme:** `major*1000000 + minor*1000 + patch`
  (2.0.0 → 2000000), replacing `major*10000 + minor*100 + patch`. The old scheme
  collided as soon as a minor or patch exceeded 99 — 1.100.0 and 2.0.0 both produced
  20000.
  *Restore the old behaviour:* not possible — recompute any compile-time
  `#if PH_VERSION_NUMBER >= ...` check against the new scheme.

- **Shared builds now carry a versioned soname, `SOVERSION = 2`**
  (`libphash.so.2` / `libphash.2.dylib`). Consumers linked against an unversioned
  1.x shared library must relink. This is the only signal a consumer's linker gets
  when the library breaks compatibility across a major version, so it is intentional
  and permanent.
  *Restore the old behaviour:* not applicable — relink against the installed 2.x
  library. `find_package(phash 1.x REQUIRED)` correctly refuses to pick up the 2.0.0
  package (`COMPATIBILITY SameMajorVersion`).

- **The test-only mock decoder is no longer compiled into the library.** The
  recommended Release build used to ship a backend that intercepted any buffer
  starting with `DE AD` and "decoded" it into a 1×1 image. Such a buffer now yields
  `PH_ERR_UNSUPPORTED_FORMAT`.
  *Restore the old behaviour:* configure with `-DPHASH_ENABLE_MOCK_BACKEND=ON` (OFF
  by default, deliberately not tied to `PHASH_BUILD_TESTS`, and it warns at configure
  time). It is for testing only and must not be enabled in a shipped build.

- **Enabling both PNG backends is now a configure-time error.** `PHASH_USE_LIBPNG`
  and `PHASH_USE_SPNG` are mutually exclusive; setting both used to silently pick one.
  *Restore the old behaviour:* not applicable — choose one backend explicitly.
- **Non-regular files are rejected instead of decoded.** Passing a FIFO, a character
  device or `/dev/stdin` to `ph_load_from_file()` now returns `PH_ERR_IO`. Previously
  the fallback decoder would read such a path happily, so this turns a former
  `PH_SUCCESS` into an error for those (exotic) inputs. Regular files are unaffected.
  *Restore the old behaviour:* not possible by path — read the stream into memory
  yourself and call `ph_load_from_memory()`, which is the supported way to hash
  something that is not a file on disk.

### Added

- **A stated scope and threat model.** The README now says what the library is for —
  deduplicating a collection you control — and, more importantly, what it is not for.
  Every hash here is deterministic and unkeyed, which is what makes deduplication work
  and what makes the hashes straightforward to defeat deliberately. Do not use them as a
  moderation filter, a copyright blocklist, or an integrity check on untrusted input.
  Nothing about the code changed; the exclusion was always true and is now written down.

- **`docs/algorithm-provenance.md`** traces each of the nine hashes to its primary
  source and records, per algorithm, where this implementation departs from it. It
  names the departures that are known to contradict a published formula — most of them
  in the radial hash — so that a choice of algorithm can be made with the gaps visible
  rather than discovered. Two attributions are corrected there: wHash has no primary
  source, and mHash is not a Marr-Hildreth hash. `docs/references.md` is the matching
  bibliography, and every `src/hashes/*.c` file now opens with its own citation.

- **Batch API.** `ph_hash_files()` and `ph_hash_buffers()` hash a batch of files or
  in-memory buffers, optionally across an internal thread pool (`threads`: 0 = one
  worker per detected core, 1 = sequential on the calling thread, >1 = that many
  workers). Per-item failures are reported in `ph_batch_item_t::status` /
  `ph_batch_buffer_item_t::status` and never abort the batch; the return value
  reports only failures that stopped the batch from being worked on at all.
- **`ph_compute_multi()`** computes several `uint64_t` algorithms in one call,
  selected by a `ph_hash_flags_t` bitmask, sharing the grayscale conversion across
  them. Results are bit-for-bit identical to the individual `ph_compute_*` calls.
- **`ph_load_from_pixels()`** hashes an already-decoded pixel buffer (OpenCV, PIL,
  numpy, a video frame), skipping the encode/decode round-trip. Accepts 1, 3 or 4
  channels and an arbitrary row stride.
- **Hex and similarity helpers:** `ph_digest_to_hex()`, `ph_digest_from_hex()`,
  `ph_hash_to_hex()`, `ph_similarity()`, `ph_similarity_digest()`.
- **`ph_get_last_error_message()`** returns a short diagnostic string about the most
  recent failure on a context (e.g. the decoder-reported reason a load failed).
- **`ph_version_number()`** returns the version as one comparable integer, for FFI
  callers that would otherwise parse the string.
- **Specific error codes** replacing the old decode catch-all: `PH_ERR_IMAGE_TOO_LARGE`,
  `PH_ERR_UNSUPPORTED_FORMAT`, `PH_ERR_CORRUPT_DATA`, `PH_ERR_DECODER_UNAVAILABLE`,
  `PH_ERR_IO`, plus `PH_ERR_REQUIRES_COLOR`. `ph_error_t` now documents its ABI rule:
  values are spelled out explicitly, new codes are only appended, and a removed
  code's value stays retired.
- **EXIF/metadata orientation support**, applied automatically by default (see
  BREAKING CHANGES) and controllable via `ph_context_set_auto_orient()`. Orientation
  is read from JPEG APP1, the WebP `EXIF` chunk and the PNG `eXIf` chunk. Missing or
  malformed metadata is treated as "no transform needed", never as an error.
- **`ph_context_set_max_pixels()`** — a decompression-bomb guard applied before any
  pixel buffer is allocated. Defaults to 256 MP; exceeding it fails with
  `PH_ERR_IMAGE_TOO_LARGE`.
- **More input formats for free:** `stb_image` is now registered as an unconditional
  last-resort decoder backend, adding BMP/GIF/TGA/PSD/HDR/PIC/PNM and covering
  JPEG/PNG in builds without a native decoder for them. WebP is deliberately excluded
  from the fallback so a WebP file in a build without `PHASH_USE_WEBP` reports
  `PH_ERR_DECODER_UNAVAILABLE` precisely instead of failing generically. TIFF and
  AVIF/HEIC remain unsupported.
- **Packaging:** `install()` rules, a `phashConfig.cmake` package usable via
  `find_package(phash)`, and a relocatable `libphash.pc` for pkg-config. The
  generated `phash_version.h` is installed next to `libphash.h`, so consumers get a
  compile-time version macro and not just the runtime `ph_version()`.
- **`PHASH_USE_ZLIB_NG` build option** to build libpng/spng against zlib-ng.
- **`PHASH_STRICT_DEPS` build option** turning the previously silent "TurboJPEG not
  found, falling back to stb_image" and "zlib-ng submodule not found" warnings into
  configure-time errors, so a green build actually proves the vendored decoders were
  built and linked.
- **Fuzzing and sanitizers:** a libFuzzer harness for the decode path
  (`PHASH_BUILD_FUZZERS`) and ASan/UBSan CI jobs.
- **CI across platforms:** Linux x86_64 (gcc and clang), Linux arm64 and macOS arm64
  for the full build — so the NEON paths actually run — plus a minimal
  (stb_image-only) matrix over Linux/macOS/Windows that exercises the MSVC path, an
  `install()`/pkg-config/`find_package` smoke test, an `add_subdirectory()` smoke
  test, and a benchmark job that gates on a regression against the base branch.
- **A native linux/arm64 Docker development environment.**
- **Tests:** a perceptual robustness suite and a golden-hash regression suite.

### Changed

- Vendored decoder submodules bumped to their latest stable tags.
- `THIRD-PARTY-NOTICES.md` now names the exact version of the two copied stb headers
  (`stb_image` v2.30, `stb_image_resize2` v2.18) with their hashes. Everything else
  under `vendor/` is a submodule, whose revision the repository already records.
- The library version has a single source of truth: `project(libphash VERSION ...)`
  in `CMakeLists.txt`, from which `phash_version.h` is generated. There are no
  version literals in sources, scripts, CI or the README any more.
- The Radial hash's scratch allocations moved onto the context arena, removing
  per-call `malloc()`/`free()` traffic from the hot path.
- Thread defaults now match across build systems, and `libphash.pc` is relocatable.
- `make debug` and `make coverage` inherit `CFLAGS` instead of replacing it.
- **Documentation corrections that follow from the provenance work.** `docs/algorithms.md`
  claimed the radial hash gives "unmatched robustness against rotation (up to 360°)". It
  does not: that invariance comes from comparing two hashes by the peak of their
  cross-correlation, and this library compares digests element-wise. The claim is
  withdrawn until the comparison is implemented. The same page described mHash as
  configurable through `ph_context_set_block_params`, which mHash ignores.
- Documentation fix: the gamma setting affects the Radial hash and nothing else.
  Its default of 2.2 is under review for a future release; it is deliberately not
  changed in 2.0.0, since changing it would move Radial hashes.
- The batch API validates its arguments *before* the `n == 0` shortcut, so an empty
  batch no longer excuses a malformed call.
- `PH_ERR_IO` is now reported for files that cannot be opened or read, instead of a
  generic decode failure, and it means the same thing on every platform. The Windows
  build previously skipped the check entirely and answered
  `PH_ERR_UNSUPPORTED_FORMAT` or `PH_ERR_CORRUPT_DATA` for a missing file. The cases
  are: missing path (including a dangling symlink), no read permission, not a regular
  file, and an empty file. All of them are decided before any decoder sees the bytes,
  and all of them leave a description in `ph_get_last_error_message()`.
- Batch thread auto-detection on Windows is documented as covering only the current
  processor group (at most 64 logical processors); pass an explicit thread count and
  set affinity yourself if you need more.
- `ph_load_from_file()` opens the file once instead of up to six times per call, and
  decodes through exactly the same code path as `ph_load_from_memory()`. Loading is
  measurably cheaper in builds without the bundled decoders (8-11% faster on a small
  JPEG); with them the file handling was already a small fraction of the work. The
  checks that used to accept a path can no longer disagree with the bytes that are
  actually decoded.
- Auto-orientation, the `max_pixels` limit and format detection now behave identically
  whether an image comes from a path or from a buffer, and are covered by a parity test.
- Applying EXIF orientation is 1.3-7.5x faster, which matters because auto-orientation
  is on by default: whole-row copies for the flips and a tiled walk for the four
  transposing orientations, in place of a pixel-at-a-time copy. On a 20 Mp photo the
  transposing orientations drop from 84-105 ms to 12-20 ms. The transformed pixels are
  bit-for-bit what they were, so no hash moves.
- Loading from a file holds the encoded bytes in memory (mapped where the platform
  allows it) alongside the decoded image, where some build configurations previously
  read them incrementally. For ordinary images the encoded bytes are a small fraction
  of the decoded ones, but callers hashing very large files on a tight memory budget
  should be aware of the change.

### Fixed

- **pHash thresholds its 8×8 DCT block against the median of its 63 AC coefficients**,
  leaving the DC term out of that median as the reference implementation does. It had been
  included. In practice this changes nothing: no pHash value in the test fixtures moves,
  and the only way the two can differ at all is a single bit when the two middle
  coefficients tie to within a float ulp — contrary to the usual explanation, a median is
  not dragged by an outlier. Recorded because it is a conformance change you may see on
  degenerate input, not because it will move your hashes.
- `ph_compute_phash()` returned a hash computed from **uninitialized memory** when
  the DCT parameters were out of range; out-of-range parameters are now rejected.
- Pixel counts were computed in `int` and could overflow (undefined behaviour); they
  are computed in `size_t` now. This also closed a decompression-bomb hole in
  `ph_load_from_pixels()`, which bypassed the pixel-limit check entirely.
- Windows batch thread-pool wait was racy and undefined; the pool now joins its
  workers correctly.
- PNG decoder: `setjmp` is now armed *before* the info struct is created, so an early
  libpng error no longer longjmps into an unprepared state.
- PNG decoder: the `row_ptrs` allocation is now checked for overflow.
- PNG decoder: a deliberate dimension cap is applied instead of raising libpng's own
  user limit.
- The spng backend could not decode to grayscale at all: it asked spng for an 8-bit
  gray output format unconditionally, which spng only accepts for images that are
  already grayscale, so loading any truecolor, palette, gray+alpha or 16-bit PNG with
  grayscale loading enabled failed with `PH_ERR_CORRUPT_DATA`. It now converts when
  the source format requires it, byte for byte identically to the libpng backend.
- `add_subdirectory()` no longer clobbers the parent project's settings, and works for
  both static and shared parents. The zlib-ng block in particular used to leave a
  `ZLIB_LIBRARY` pin behind in the parent's cache.
- The exported CMake package declares its `Threads` dependency, so
  `find_package(phash)` links correctly in a consumer project.
- Fuzzer targets link correctly when built together with the tests.
- A build for Windows with any of the bundled native decoders enabled failed to
  compile: the file-mapping code was guarded by the decoder switches but used
  POSIX-only headers. It now sits behind its own platform check, with a portable read
  fallback.
- `ph_load_from_memory()` could return `PH_SUCCESS` without having loaded an image,
  in the case where the decoder reported failure without setting an error code.
- A file larger than `SIZE_MAX` (a 32-bit build fed something above 4 GB) is reported
  as `PH_ERR_IO` instead of failing inside the mapping call.
- A top-down BMP (legitimately negative height from `stbi_info`) was rejected as
  `PH_ERR_IMAGE_TOO_LARGE` regardless of its actual size, because the signed height
  was cast straight to `uint64_t`.
- `ph_digest_from_hex()` accepted uppercase input as documented but this was never
  tested; mismatched digest sizes in the comparison functions were also unchecked.
- `make clean` removes stray `*.o` files left outside `obj/`.
- UBSan alignment noise originating in the vendored `stb_image_resize2.h` is
  suppressed, so the sanitizer output is actionable again.

### Security

- **Decompression bombs** are rejected before any pixel buffer is allocated, via a
  configurable `max_pixels` limit (256 MP by default) plus an unconditional
  `INT_MAX`-pixel implementation ceiling.
- **Out-of-bounds read** in every public helper that reads a `ph_digest_t`: a
  hand-assembled digest with `size > 64` read past the end of the struct. Now
  rejected.
- **Heap overflow / signed overflow** reachable through `max_pixels = 0` and through
  `int` pixel-count arithmetic. Fixed by the ceiling and by `size_t` arithmetic.
- **The mock decoder no longer ships in release builds.** It was registered ahead of
  the real catch-all backend and intercepted any input beginning with `DE AD`.
- **Absurd aspect ratios** are rejected by a per-dimension cap of 1000000 pixels that
  no `max_pixels` setting can lift, in every format and every build configuration.
- **PNG decoder hardening:** overflow-checked `row_ptrs` allocation, the dimension cap
  applied straight from the IHDR, and `setjmp` armed before the info struct exists.
- The decode path is now fuzzed (libFuzzer) and built under ASan/UBSan in CI.

## [1.10.4] - 2026-04-19

### Changed

- Version bump only; no functional change over 1.10.3.

## [1.10.3] - 2026-04-19

### Changed

- Resizing moved to the vendored `stb_image_resize2`, with Lanczos as the filter;
  the previous hand-written bilinear and mipmap resize paths were removed.
- Vendored decoder submodules updated to stable tags.
- THIRD-PARTY-NOTICES updated.

## [1.10.2] - 2026-03-31

### Fixed

- wHash accuracy: switched to box resizing for better feature preservation.
- `ph_median_bitpack()` computed the wrong median for even-sized arrays.

## [1.10.1] - 2026-03-30

### Changed

- Version bump only; no functional change over 1.10.0.

## [1.10.0] - 2026-03-30

### Added

- Native libwebp decoder backend.
- `ph_get_error_string()` for human-readable error descriptions.
- Unit tests for the hash algorithms and math-rigor tests for DCT, median and HSV
  classification, bringing coverage above 95%.
- CI with benchmark comparison.
- THIRD-PARTY-NOTICES.

### Changed

- Complete modular reorganization of the sources (`src/hashes/`, `src/loaders/`,
  `src/image/`).
- SIMD optimizations for Linux and x86_64; Radial hash optimized and wHash modes
  benchmarked.
- The scratchpad is trimmed automatically to prevent unbounded memory growth.

### Fixed

- Several architectural and mathematical bugs across the hash algorithms.
- Global AVX2 and LTO were reverted after they caused a performance regression.

## [1.9.0] - 2026-02-23

Earlier releases (1.0.0 – 1.9.0) predate this changelog. See the git history and the
release tags for details.

[Unreleased]: https://github.com/gudoshnikovn/libphash/compare/1.10.4...HEAD
[2.0.0]: https://github.com/gudoshnikovn/libphash/compare/1.10.4...HEAD
[1.10.4]: https://github.com/gudoshnikovn/libphash/compare/1.10.3...1.10.4
[1.10.3]: https://github.com/gudoshnikovn/libphash/compare/1.10.2...1.10.3
[1.10.2]: https://github.com/gudoshnikovn/libphash/compare/1.10.1...1.10.2
[1.10.1]: https://github.com/gudoshnikovn/libphash/compare/1.10.0...1.10.1
[1.10.0]: https://github.com/gudoshnikovn/libphash/compare/1.9.0...1.10.0
[1.9.0]: https://github.com/gudoshnikovn/libphash/releases/tag/1.9.0
