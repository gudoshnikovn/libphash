# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Nothing yet.

## [2.0.0] - Unreleased

First major release. It is a major because some changes are invisible at upgrade time:
the same input can produce a different hash, several functions changed signature, and
two error constants were removed. Read the BREAKING CHANGES section before upgrading, and
see `MIGRATION.md` for the 1.x → 2.0 walkthrough.

### BREAKING CHANGES

- **`ph_compute_color_hash()` is a colour histogram, returns a 108-byte digest.** 1.x
  returned a `uint64_t` of 42 bits ported from ImageHash's `colorhash`, which cites no
  source. It is 108 bins of the opponent colour space (red–green × blue–yellow ×
  light–dark, 6 × 6 × 3), compared with **`ph_histogram_intersection()`** — a colour
  histogram with histogram intersection, after Swain & Ballard (1991), implemented from
  secondary descriptions of the paper. The signature takes a `ph_digest_t *`.
  *Restore the old behaviour:* not possible; recompute any stored ColorHash values.

- **`ph_compute_mhash()` is a Marr–Hildreth hash and returns a 72-byte digest.** 1.x
  returned a `uint64_t`: the sign of a four-neighbour discrete Laplacian on an 18×18 grid.
  It normalises the image to 512×512, equalises it, correlates it with the
  Laplacian-of-Gaussian operator of Marr and Hildreth, and emits 576 bits — the
  construction pHash's `ph_mh_imagehash()` defines. The signature takes a `ph_digest_t *`.
  `ph_context_set_mhash_params()` exposes the kernel's scale (`alpha`, `level`) and the
  normalisation size; the defaults are the reference implementation's.
  *Restore the old behaviour:* not possible; recompute any stored mHash values.

- **`ph_compute_color_moments_hash()` returns an 18-byte digest, not 9, and keeps the sign
  of the skewness.** 1.x stored each moment in one unsigned byte, clamped at 255, and
  stored the skewness as `fabs()`, so mirror-image channel distributions were identical.
  Each of the nine features is a **signed 16-bit big-endian fixed-point number in units of
  1/128** (`PH_VECTOR16_SCALE`), tagged `PH_DIGEST_KIND_VECTOR16`. No attainable moment
  exceeds a magnitude of 255, so nothing clamps. `ph_l2_distance()` decodes the pairs and
  returns the distance in the moments' own units.
  *Restore the old behaviour:* not possible; recompute any stored ColorMoments values.

- **`ph_digest_t` is 136 bytes, not 72, and carries a `kind` tag.** `PH_DIGEST_MAX_BYTES`
  is 128 (1.x: 64): the Marr–Hildreth hash is 576 bits, and the rest is headroom for the
  whole 2.x series. The byte after `size` is `kind`, a `ph_digest_kind_t` saying what the
  bytes are — a bit vector, transform coefficients, a feature vector, a histogram — so a
  comparison meant for one **refuses** the others instead of returning a plausible number
  that means nothing: `ph_hamming_distance_digest()` returns -1 for a radial or
  colour-moments digest, `ph_l2_distance()` for a BMH digest. The tag never chooses a
  metric for you. `PH_DIGEST_KIND_UNSPECIFIED` is zero, so a hand-filled struct is
  accepted everywhere and gets no protection.
  *Restore the old behaviour:* rebuild any FFI binding that hardcodes the layout; where a
  comparison returns -1, switch to the metric for that digest.

- **aHash reduces the image by an exact area average and sets the bit of a pixel exactly
  equal to the mean.** 1.x resampled to 8×8 with stb's Mitchell filter and cleared a
  pixel on the mean (`pixel > mean`). Each of the 64 values is the mean of the part of the
  image it covers, which measures better — on the synthetic test corpus separability
  3.63 → 4.55 and false matches at 95 % recall 8.0 % → 2.9 %; on 800 photographs fewer
  pairs of different images share a hash (1208 → 1051) — and the reduction is shared with
  pHash, wHash and BMH, so `ph_compute_multi()` pays for it once. The tie-break is `>=`,
  the rule BMH follows, against the exact mean: an image that is uniform at 8×8 hashes to
  all ones instead of all zeros. Across 807 photographs and textures about two thirds of
  aHash values move, mostly by 1–6 bits.
  *Restore the old behaviour:* not possible; recompute stored aHash values.

- **pHash thresholds its DCT block at the median plus 0.1 % of the AC coefficients'
  range, not at the bare median.** On an image with little low-frequency structure many
  AC coefficients crowd the median and their bits followed rounding and noise: ±1 grey
  level moved pHash by 11 bits or more on 140 of 800 photographs, and flat greys hashed
  like unrelated images. The margin sends that crowd to 0 together: 98 of 800, and on the
  same photographs false matches at 95 % recall fall from 14.2 % to 6.8 %. pHash values
  differ from the reference `ph_dct_imagehash()` (and from 1.x) wherever coefficients sit
  within the margin of the median: across 807 photographs and textures, about three in
  five, mostly by 1–3 bits.
  *Restore the old behaviour:* not possible; recompute stored pHash values.

- **Images with transparency are hashed as they look.** 1.x dropped the alpha channel and
  hashed the colour stored under it — invisible, and different from encoder to encoder:
  over 141 PNGs with at least 5 % transparency, two copies differing only in the colour
  under alpha 0 hashed 28–42 bits apart of 64. Every image with alpha (an alpha channel or
  a PNG `tRNS` chunk, from any decoder, or RGBA passed to `ph_load_from_pixels()`) is
  composited onto mid-grey at load time, and such copies hash identically. The new
  **`ph_context_set_alpha_mode()`** chooses a white or black background instead, or
  `PH_ALPHA_IGNORE`. A loaded image never stores alpha: `ph_context_get_dimensions()`
  reports 3 channels (1 when loaded as grayscale).
  *Restore the old behaviour:* `ph_context_set_alpha_mode(ctx, PH_ALPHA_IGNORE)`.

- **The Block Mean Hash thresholds against the median of the block means, not their
  arithmetic mean, so every BMH value changes.** That is what Yang, Gu and Niu's method 1
  specifies (step d and equation 3.9), and the median makes the bit distribution balanced
  by construction — under the mean, a dark image with a few bright blocks produces a
  lopsided hash. On photographs the two rules almost agree, so most values move by a bit
  or two; on images with skewed block values they move a great deal. BMH values differ
  from OpenCV's `BlockMeanHash`, which thresholds on the mean (in a variable it calls
  `median`).
  *Restore the old behaviour:* not possible; recompute any stored BMH digests.

- **The Radial hash is reimplemented after its source, and every radial value changes.**
  It takes the variance along one projection line per degree over 180°, standardises that
  vector, applies a 1-D DCT and keeps the first 40 coefficients — always a 40-byte
  digest (De Roover et al. 2005, as pHash implements it). 1.x took 40 angles, no
  transform, and a digest one byte per angle.
  `ph_context_set_radial_params()`'s first argument is the number of angles (40..4096,
  default 180), and it takes a new third argument, `sigma`, the Gaussian blur before the
  projections (default 3.5, pHash's own default; 1.x used a fixed 3×3 kernel). Gamma
  follows pHash: default 1.0, pixels normalised by the buffer's maximum and raised to
  `gamma` (1.x raised them to `1/gamma`, default 2.2). Compare digests with
  **`ph_radial_similarity()`** — the peak of their cross-correlation over cyclic shifts,
  with the source's threshold as `PH_RADIAL_PCC_THRESHOLD` (0.9); the bit and vector
  metrics accept a radial digest but return a number that does not mean what it appears
  to. The hash tolerates a few degrees of rotation plus an exact half turn (measured on a
  photograph: 1° → 0.993, 5° → 0.870, 15° → 0.437, 90° → 0.243, 180° → 0.993, against
  0.69 for an unrelated image).
  *Restore the old behaviour:* not possible; recompute any stored radial digests. A caller
  that sets gamma explicitly must review the value: the same number means the inverse
  exponent.

- **Automatic EXIF orientation is on by default.** Images carrying an `Orientation` tag
  other than 1 (most photos straight from phones and cameras) are rotated/mirrored before
  hashing, so a hash describes what a viewer displays rather than the stored pixels.
  **Every hash you have stored for such an image changes.** There is no error and no
  warning — only a silently lower recall in deduplication, so plan a rehash of the
  affected corpus.
  *Restore the old behaviour:* `ph_context_set_auto_orient(ctx, 0)` after `ph_create()`.

- **`PH_ERR_DECODE_FAILED` is removed from `ph_error_t`.** In 1.x it was returned for every
  load failure; 2.0 returns the specific cause (see "Specific error codes" under Added).
  Removing the name makes the change visible at compile time. Its value `-2` is reserved
  and never reused.
  *Restore the old behaviour:* not possible, and not desirable — replace the check with the
  specific codes. A code-to-code mapping table is in `MIGRATION.md`.

- **Hashing a context with no image returns `PH_ERR_EMPTY_IMAGE`, and
  `PH_ERR_NOT_IMPLEMENTED` is removed.** Every `ph_compute_*` function reported "no image
  loaded" as `PH_ERR_INVALID_ARGUMENT`, indistinguishable from a NULL pointer, while
  `PH_ERR_EMPTY_IMAGE` — the code that describes exactly that — was never returned.
  Arguments are checked first, so a NULL pointer stays `PH_ERR_INVALID_ARGUMENT`.
  `PH_ERR_NOT_IMPLEMENTED` was returned from nowhere; its value `-4` is reserved like `-2`.
  *Restore the old behaviour:* treat `PH_ERR_EMPTY_IMAGE` the way you treated
  `PH_ERR_INVALID_ARGUMENT` after a hash call, and delete any check for
  `PH_ERR_NOT_IMPLEMENTED`. See `MIGRATION.md`.

- **Every `ph_context_set_*` function returns `ph_error_t` instead of `void`, and invalid
  input is rejected.** 1.x ignored an out-of-range value, or, for the gray weights,
  replaced it with the defaults, so a caller could hash a whole batch with a configuration
  it never asked for. One contract for all of them: a valid argument returns
  `PH_SUCCESS`; anything else returns `PH_ERR_INVALID_ARGUMENT` and leaves the
  configuration **completely unchanged** — never clamped, never partially applied, never
  reset to defaults. Existing call sites still compile (the setters are deliberately not
  `warn_unused_result`). The bounds are implementation limits: `gamma` finite and in
  (0.001, 1000]; gray weights each ≥ 0 with a sum in (0, INT_MAX/255];
  `reduction_size` 4..8 (below 4 most unrelated images share a pHash) and `dct_size`
  `reduction_size`..32; radial `projections` 40..4096 (the lower bound is the coefficient
  count the DCT keeps) and `samples` 2..4096 (the digest has converged well below both
  ceilings, and one call at the ceiling costs about 56 ms instead of tens of seconds),
  `sigma` in (0, 64/3];
  `block_size` 2..32 (a single block cannot threshold against a median; 32×32 bits is the
  largest grid a digest holds — 1.x accepted any positive value and truncated the BMH
  digest to 64 bytes above 22×22); `whash_mode` a declared enumerator only.
  *Restore the old behaviour:* not possible — pass values inside the documented bounds,
  and check the return value wherever the argument comes from outside your own code.

- **Public helpers reading a `ph_digest_t` reject `size > PH_DIGEST_MAX_BYTES` instead of
  reading past `data`.** `ph_digest_t` is a flat struct that FFI bindings assemble by
  hand, and in 1.x, with 64 data bytes, `size = 200` read 136 bytes past the end of the
  array. Functions returning `ph_error_t` return `PH_ERR_INVALID_ARGUMENT`;
  distance/similarity functions return `-1`. A `size` of `0` is `-1` from every
  comparison function (1.x returned `0`, i.e. "identical", from
  `ph_hamming_distance_digest()` and `ph_l2_distance()` for two digests without a bit).
  *Restore the old behaviour:* not possible — it was an out-of-bounds read. Make sure
  hand-assembled digests carry a `size` of at most `PH_DIGEST_MAX_BYTES`.

- **The colour algorithms refuse grayscale images.** `ph_compute_color_hash()` and
  `ph_compute_color_moments_hash()` return the new `PH_ERR_REQUIRES_COLOR` when the loaded
  image has fewer than 3 channels, and leave the output untouched. 1.x read r/g/b out of
  one replicated channel and returned `PH_SUCCESS` with an outwardly valid but meaningless
  result.
  *Restore the old behaviour:* not possible — load the image in colour
  (`ph_context_set_load_grayscale(ctx, 0)`, the default, or pass 3/4 channels to
  `ph_load_from_pixels()`) before asking for a colour hash.

- **Images above 256 Mi pixels are rejected by default.** 1.x decoded any size. 2.0
  refuses an image whose width × height exceeds `max_pixels` (default 268,435,456) before
  allocating it, with `PH_ERR_IMAGE_TOO_LARGE`; see `ph_context_set_max_pixels()` under
  Added.
  *Restore the old behaviour:* raise the limit with `ph_context_set_max_pixels()`; an
  implementation ceiling of `INT_MAX` pixels always applies.

- **A single image dimension may not exceed 1000000 pixels.** The cap applies to every
  format and to both `ph_load_from_file()` and `ph_load_from_memory()`, on top of
  `max_pixels` and independently of it — setting `max_pixels` higher, or to `0`, does not
  lift it. The area limit on its own permits an absurd aspect ratio: a 268435456 × 1 image
  sits exactly on the default limit, yet makes the decoder size a single row of ~800 MB.
  Real photographs are nowhere near this. `ph_load_from_pixels()` is not subject to the
  cap.
  *Restore the old behaviour:* not possible — split such an image yourself, or decode it
  with your own decoder and pass the pixels to `ph_load_from_pixels()`.

- **Non-regular files are rejected instead of decoded.** Passing a FIFO, a character
  device or `/dev/stdin` to `ph_load_from_file()` returns `PH_ERR_IO`; 1.x's `stb_image`
  fallback read such a path. Regular files are unaffected.
  *Restore the old behaviour:* not possible by path — read the stream into memory yourself
  and call `ph_load_from_memory()`, the supported way to hash something that is not a file
  on disk.

- **`ph_can_use_libjpeg()`/`ph_can_use_libpng()` are renamed `ph_can_use_jpeg()`/
  `ph_can_use_png()`**, so all three capability checks, with `ph_can_use_webp()`, are named
  after the format. `ph_can_use_libpng()` answered `1` in a build using spng, where libpng
  is not linked at all; the new name says what it answers — whether a native PNG decoder,
  libpng or spng, is compiled in.
  *Restore the old behaviour:* rename the calls; the return values are unchanged.

- **Public enums are 32 bits wide under `-fshort-enums`.** Each public enum ends in a
  `*_FORCE_INT32_` enumerator that is not a real value. Under `-fshort-enums` — the default
  ABI on ARM EABI — 1.x's enums shrank to one byte, so a library and a consumer built with
  different settings silently disagreed on every `ph_error_t` return value. Only code
  built with that flag sees a change.
  *Restore the old behaviour:* not applicable — rebuild against the 2.0 header.

- **Shared builds carry a versioned soname, `SOVERSION = 2`** (`libphash.so.2` /
  `libphash.2.dylib`). Consumers linked against an unversioned 1.x shared library must
  relink.
  *Restore the old behaviour:* not applicable — relink against the installed 2.x library.

- **The test-only mock decoder is not compiled into the library.** 1.x registered, in
  every build, a backend that claimed any buffer starting with `DE AD` and "decoded" it
  into a 1×1 image. Such a buffer yields `PH_ERR_UNSUPPORTED_FORMAT`.
  *Restore the old behaviour:* configure with `-DPHASH_ENABLE_MOCK_BACKEND=ON` (OFF by
  default; it warns at configure time). It is for testing only and must not be enabled in
  a shipped build.

- **Enabling both PNG backends is a configure-time error.** `PHASH_USE_LIBPNG` and
  `PHASH_USE_SPNG` are mutually exclusive; 1.x built and linked both and used libpng.
  *Restore the old behaviour:* not applicable — choose one backend explicitly.

- **The JPEG backend option is `PHASH_USE_LIBJPEG_TURBO`** (1.x: `PHASH_USE_TURBOJPEG`),
  named after the codec. Passing the old name stops the configure step and names the new
  one, rather than being ignored and building the default backend set.
  *Restore the old behaviour:* not applicable — pass `-DPHASH_USE_LIBJPEG_TURBO=…`.

### Added

- **Prebuilt release artifacts.** Each tagged release publishes static and shared
  archives (headers, library, `LICENSE`, `THIRD-PARTY-NOTICES.md`) for linux-x86_64,
  linux-arm64, macos-arm64 and windows-x86_64 on the GitHub Releases page, each built
  with the full vendored decoder set and smoke-tested in isolation before publishing.
  See the README's "Prebuilt binaries" section.

- **Batch API.** `ph_hash_files()` and `ph_hash_buffers()` hash a batch of files or
  in-memory buffers, optionally across an internal thread pool (`threads`: 0 = one worker
  per CPU the process may use — the affinity mask on Linux and Windows, the cgroup CPU
  quota on Linux; 1 = sequential on the calling thread; >1 = that many workers). On
  Windows the automatic count covers the current processor group (at most 64 logical
  processors); pass an explicit count for more. Per-item failures are reported in
  `ph_batch_item_t::status` / `ph_batch_buffer_item_t::status` and never abort the batch;
  the return value reports only failures that stopped the batch from being worked on at
  all, and the arguments are validated even for an empty batch. Each item's `hashes[]`
  holds `PH_BATCH_HASHES_CAPACITY` (8) slots rather than one per currently defined
  algorithm, so new `uint64_t` algorithms can be added inside 2.x without changing the
  structs' size or layout. Peak memory grows linearly with the worker count; the header
  documents the bound.
- **`ph_hash_files_ex()`/`ph_hash_buffers_ex()`** take a `ph_batch_options_t` (initialise
  it with `ph_batch_options_init()`): a template context whose whole configuration,
  `max_pixels` included, applies to every item; a `should_continue` callback that stops the
  batch, returning the new **`PH_ERR_CANCELLED`** and storing it in every item not started;
  and an `on_progress` callback. The plain `ph_hash_files()`/`ph_hash_buffers()` run on the
  default configuration.
- **`ph_compute_multi()`** computes several of the four `uint64_t` algorithms (aHash,
  dHash, pHash, wHash) in one call, selected by a `ph_hash_flags_t` bitmask. The work over
  the full image is cached on the context and done once whichever algorithms ask for it —
  the grayscale conversion and one area-average pass that aHash, pHash, wHash and BMH all
  reduce from — so on a 20-megapixel photograph the four cost 7.6 ms rather than 15.8.
  Results are bit-for-bit identical to the individual `ph_compute_*` calls.
- **Algorithms as values.** `ph_algorithm_t` names every algorithm (contiguous from 0 to
  `PH_ALGORITHM_COUNT - 1`; for the four `uint64_t` ones the `ph_hash_flags_t` bit is
  `1 << value`). `ph_compute_digest()` computes any of them into a `ph_digest_t` — the
  `uint64_t` algorithms as 8-byte `bits` digests — so one code path can store and compare
  every algorithm. `ph_digest_info()` reports a digest's size and kind without an image.
  `ph_algorithm_name()`/`ph_algorithm_from_name()` convert to and from stable names such
  as `"radial"`.
- **`ph_load_from_pixels()`** hashes an already-decoded pixel buffer (OpenCV, PIL, numpy,
  a video frame), skipping the encode/decode round-trip. Accepts 1, 3 or 4 channels and an
  arbitrary row stride; the `max_pixels` limit applies to it too.
- **`ph_radial_similarity()`** and **`ph_histogram_intersection()`**, the comparisons the
  Radial hash and ColorHash are defined with (see BREAKING CHANGES). An image with no
  angular structure (blank, or radially symmetric: a variance profile spread across angles
  by less than 1 % of its mean) hashes to an all-zero radial digest, and
  `ph_radial_similarity()` answers any comparison with it with **`PH_ERR_NO_STRUCTURE`**
  rather than a score — the threshold is relative, so a faint pattern still gets a digest
  of its own. Histogram
  intersection is computed exactly: identical or proportionally scaled histograms score
  exactly `1.0`, swapping the arguments gives a bit-identical result, and the value is the
  same on every platform.
- **Hex and similarity helpers:** `ph_digest_to_hex()`, `ph_digest_from_hex()`,
  `ph_hash_to_hex()`, `ph_hash_from_hex()`, `ph_similarity()`, `ph_similarity_digest()`. A
  digest's text form is `<kind>:<hex>` (e.g. `coefficients:1f80…`), and decoding restores
  the kind, so digests read back from storage still refuse the wrong comparison metric.
  Hex digits are accepted in either case. `PH_DIGEST_HEX_BUFFER_SIZE` is a buffer size that
  fits any digest.
- **`ph_context_get_gray_weights()`** reads back the grayscale weights a context uses —
  the normalized values, which differ from what was passed to the setter.
- **`ph_context_set_decode_scale()`** lets a caller opt into decoding JPEG at 1/2, 1/4 or
  1/8 linear resolution instead of natively, trading accuracy for decode speed via
  libjpeg-turbo's DCT-domain scaling. Default is `PH_DECODE_SCALE_FULL`. Only the JPEG
  backend honors it; PNG has no format-level scaled decode and libwebp's scaling API
  resizes *after* a full decode (no decode-time saving), so both backends decode at full
  resolution regardless of the setting. Measured tradeoffs (speed saturates around 18% at
  1/8 because entropy decoding isn't skipped; accuracy holds on photographic content but
  pHash and mHash can exceed this library's own same-scene-transform contract on fine
  periodic textures) are in the setter's doc comment in `include/libphash.h`. Radial,
  ColorMoments and ColorHash are not resize-based, so at any scale other than full they
  operate directly on the reduced buffer as their actual input — their accuracy at reduced
  scale has not been measured.
- **`ph_context_set_whash_remove_max_haar_ll()`** exposes ImageHash's `remove_max_haar_ll`,
  which zeroes the coarsest Haar LL band before the working decomposition. It defaults to
  **off**, and turning it on does not change a bit except by rounding a tie: zeroing that
  single coefficient and reconstructing subtracts the image mean from every sample, a
  constant subtraction shifts the working LL band and its median alike, and a hash
  thresholded at the median is blind to it — equally true of ImageHash, where the option is
  on by default. The setter exists for callers who need to mirror ImageHash's
  configuration.
- **`ph_context_set_max_pixels()`** — a decompression-bomb guard applied before any pixel
  buffer is allocated, in every decoder and every build. Defaults to 256 Mi pixels;
  exceeding it fails with `PH_ERR_IMAGE_TOO_LARGE`. `0` removes the caller's own limit, but
  an implementation ceiling of `INT_MAX` (2147483647) pixels always applies and also caps
  any larger configured value, because pixel indexing inside the library is done in `int`.
- **EXIF/metadata orientation**, applied automatically by default (see BREAKING CHANGES)
  and controllable via `ph_context_set_auto_orient()`. Orientation is read from JPEG APP1,
  the WebP `EXIF` chunk and the PNG `eXIf` chunk. Missing or malformed metadata is treated
  as "no transform needed", never as an error; an allocation failure while applying a real
  orientation fails the load with `PH_ERR_ALLOCATION_FAILED`, so an image is never hashed
  in an orientation the caller did not ask for.
- **Specific error codes** replacing 1.x's single decode failure: `PH_ERR_IMAGE_TOO_LARGE`,
  `PH_ERR_UNSUPPORTED_FORMAT`, `PH_ERR_CORRUPT_DATA`, `PH_ERR_DECODER_UNAVAILABLE`,
  `PH_ERR_IO`, plus `PH_ERR_REQUIRES_COLOR`, `PH_ERR_CANCELLED` and
  `PH_ERR_NO_STRUCTURE`. Every build and every decoder answers the same input with the
  same code:
  - `PH_ERR_IO` — a missing path (including a dangling symlink), no read permission, a
    non-regular file, an empty file, or a file larger than the address space (a 32-bit
    build fed something above 4 GB), decided before any decoder sees the bytes;
  - `PH_ERR_IMAGE_TOO_LARGE` — above `max_pixels`, the per-dimension cap or the `INT_MAX`
    ceiling, or encoded input over `INT_MAX` bytes;
  - `PH_ERR_ALLOCATION_FAILED` — a decoder ran out of memory, in every backend (the
    vendored `stb_image` is patched locally so its fallback path reports this too);
  - `PH_ERR_DECODER_UNAVAILABLE` — a WebP file in a build without `PHASH_USE_WEBP`.
  `ph_error_t` documents its ABI rule: values are spelled out explicitly, new codes are
  only appended, and a value is never reused.
- **`ph_get_last_error_message()`** returns a short diagnostic string about the most recent
  failure on a context (e.g. the decoder-reported reason a load failed). Every decoder
  backend fills it on every failure path, and the message is always valid UTF-8, cut on a
  character boundary.
- **`ph_get_build_info()`** returns one line describing how the library was built —
  version, JPEG/PNG/WebP backends, zlib, whether the batch functions can use threads, the
  SIMD target, and whether the test-only mock decoder is compiled in — for logs and bug
  reports.
- **`ph_version_number()`** and the **`phash_version.h`** header, included by
  `libphash.h`: the version as one comparable integer, `major*1000000 + minor*1000 +
  patch` (2.0.0 → 2000000), at run time and as `PH_VERSION_NUMBER` at compile time. The
  two headers are installed side by side; code that copies the header by hand needs both.
- **Packaging:** `install()` rules, a `phashConfig.cmake` package usable via
  `find_package(phash)` (`COMPATIBILITY SameMajorVersion`), and a relocatable
  `libphash.pc` for pkg-config. Both carry the backend libraries the build was configured
  with, the vendored JPEG codec installed as `-lphash_jpeg` so it cannot shadow a system
  libjpeg. `PHASH_STATIC_DEFINE` is the macro a build system other than this project's own
  CMake must define when linking libphash statically on Windows; the exported target and
  the `.pc` file set it automatically. `add_subdirectory()` works for static and shared
  parents and leaves the parent project's settings and cache as it found them.
- **`PHASH_USE_ZLIB_NG` build option** to build libpng/spng against the vendored zlib-ng,
  safe for concurrent first decodes. The JPEG backend decodes through libjpeg-turbo's
  libjpeg API, whose archive carries no zlib or spng of its own to compete with it.
- **`PHASH_STRICT_DEPS` build option** turning the "libjpeg-turbo not found, falling back
  to stb_image" and "zlib-ng submodule not found" warnings into configure-time errors, so a
  green build proves the vendored decoders were built and linked.
- **`docs/algorithm-provenance.md`** traces each of the nine hashes to its primary source
  and records, per algorithm, where this implementation departs from it; wHash has no
  primary source. `docs/references.md` is the matching bibliography, and every
  `src/hashes/*.c` file opens with its own citation.
- **A stated scope and threat model** in the README: the library is for deduplicating a
  collection you control. Every hash here is deterministic and unkeyed, which is what
  makes deduplication work and what makes the hashes straightforward to defeat
  deliberately; do not use them as a moderation filter, a copyright blocklist, or an
  integrity check on untrusted input.
- **Fuzzing and sanitizers:** a libFuzzer harness for the decode path
  (`PHASH_BUILD_FUZZERS`) and ASan/UBSan CI jobs.
- **CI across platforms:** Linux x86_64 (gcc and clang), Linux arm64 and macOS arm64 for
  the full build — so the NEON paths actually run — plus a minimal (stb_image-only) matrix
  over Linux/macOS/Windows that exercises the MSVC path, an `install()`/pkg-config/
  `find_package` smoke test, an `add_subdirectory()` smoke test, and a benchmark job that
  gates on a regression against the base branch.
- **A native linux/arm64 Docker development environment.**
- **Tests:** a perceptual robustness suite and a golden-hash regression suite.

### Changed

- Vendored decoder submodules are at their latest stable tags.
- `THIRD-PARTY-NOTICES.md` names the exact version of the two copied stb headers
  (`stb_image` v2.30, `stb_image_resize2` v2.18), records that both are modified copies,
  and gives two hashes for each: the file as vendored and the upstream file it was derived
  from.
- **pHash excludes the DC coefficient from the median it thresholds against**, as pHash's
  own `ph_dct_imagehash()` does; 1.x included it. A median is not dragged by an outlier,
  so this can move a bit only on an exact tie: across 807 photographs no pHash value
  changes against a 1.x build without FMA contraction.
- `ph_load_from_file()` opens the file once (1.x could open it twice) and decodes through
  exactly the same code path as `ph_load_from_memory()`, so auto-orientation, the size
  limits and format detection behave identically for a path and a buffer. It holds the
  encoded bytes in memory (mapped where the platform allows it) alongside the decoded
  image, where 1.x's `stb_image` path read them incrementally; for ordinary images the
  encoded bytes are a small fraction of the decoded ones, but callers hashing very large
  files on a tight memory budget should be aware of it.
- CMake pins `CMAKE_C_STANDARD` to 17 (1.x left it to the compiler's default), with
  `CMAKE_C_EXTENSIONS` off; the Makefile uses `-std=c17` too. The minimum CMake version is
  3.21 (1.x: 3.10), needed to recognize the C17/C23 standard values.
- Each CMake build type gets its own optimisation level (`Debug` unoptimised, `Release`
  `-O3`, `RelWithDebInfo` `-O2`, `MinSizeRel` `-Os`); 1.x forced `-O3` on top of every
  build type. A standalone configure that names no build type defaults to `Release`; under
  `add_subdirectory()` the parent's build type applies. The project's warning flags are not
  applied to the vendored decoders.
- `libphash.h`'s include guard is `PH_LIBPHASH_H` (1.x: `LIBPHASH_H`).
- **The shared library exports only the functions of `libphash.h`.** 1.x exported every
  internal helper, the bundled `stb_image`, and all of the libjpeg-turbo, libpng and
  libwebp linked into it, so an application with its own copy of any of those got
  whichever the dynamic linker found first. Both build systems compile with
  `-fvisibility=hidden`, which also keeps the internals out of a shared library a consumer
  builds from the static archive. See `MIGRATION.md`.
- Clang is the default compiler in both build systems (`CMakePresets.json`'s `clang`
  preset; the Makefile's `CC`; 1.x: `gcc`) — still fully overridable
  (`-DCMAKE_C_COMPILER=gcc`, `CC=gcc make`, or the `gcc` preset).
- A plain `cmake -B build` builds the vendored libjpeg-turbo submodule itself if it isn't
  built yet, instead of warning and falling back to stb_image.
- `make debug` inherits `CFLAGS` instead of replacing it.
- **libphash builds with `-ffp-contract=off`**, so GCC and Clang builds produce identical
  hashes on arm64. On arm64 a 1.x build made by a compiler that contracts multiply-adds
  into FMA (Clang's default) can give different pHash values on some images (20 of 807
  photographs, all PNG, in one measurement); x86-64 hashes are unaffected.
- **An x86-64 build does not require AVX2.** 1.x compiled the Hamming distance with
  `-mavx2` whenever the compiler accepted it, so the CMake build and anything packaged from
  it died with an illegal instruction on a CPU without AVX2. The x86-64 baseline is SSE4.2
  with POPCNT (x86-64-v2), and it is stated in the README.
- **Architecture flags follow the target compiler, not the build host.** 1.x chose them
  from the host's processor name, so a build for another architecture on the same machine
  (`CMAKE_OSX_ARCHITECTURES=x86_64` on Apple silicon, a macOS universal build) failed on a
  foreign `-march`, and the Makefile gave Linux arm64 (`aarch64`) no flag while naming one
  for macOS. arm64 gets no architecture flag at all: Advanced SIMD is part of the base
  architecture. A universal macOS build works with every bundled decoder except
  libjpeg-turbo, which it refuses at configure time. The Makefile takes
  `EXTRA_CFLAGS`/`EXTRA_LDFLAGS`, appended to its own, so `make EXTRA_CFLAGS=-m32
  EXTRA_LDFLAGS=-m32` builds 32-bit x86 on a 64-bit host.
- **Minimum supported 32-bit x86 CPU is one with SSE2** (~Pentium 4/Athlon 64, 2000-2003
  onward). Both build systems force `-mfpmath=sse -msse2` there to avoid x87
  extended-precision float math, whose results depend on the compiler/CPU in a way SSE2's
  don't — see "Fixed". Every other architecture this library targets is unaffected.
- **The same image hashes to the same values on every platform measured** — arm64 and
  x86-64, Linux and macOS, GCC and Clang, and 32-bit x86 with SSE2 — whichever PNG
  decoder the build uses. The one difference between builds is the JPEG decoder:
  libjpeg-turbo and stb_image round their IDCT differently, so a JPEG reaches the hashes
  as slightly different pixels. 1.x differed between architectures on near-uniform
  images: pHash's DCT summed in a different order on arm64, and its bare median
  threshold turned the rounding into flipped bits.

### Fixed

- **A truncated JPEG or PNG loaded successfully when decoded by `stb_image`.** It decoded
  what it could read of a half-downloaded file and reported success, while the native
  decoders rejected the same bytes. Every build requires the file to reach its own end
  (JPEG end-of-image marker, PNG `IEND`, the WebP RIFF size) and returns
  `PH_ERR_CORRUPT_DATA` otherwise. Data after that end is still accepted.
- **An encoded buffer over 2 GiB was decoded from a truncated length.** On the `stb_image`
  path the length was cut to an `int`: a valid image in a 2 GiB + 4 KiB buffer was reported
  as not an image, and past 4 GiB the length wrapped and the start of the buffer was
  decoded as if it were the whole file. Encoded input over `INT_MAX` bytes fails with
  `PH_ERR_IMAGE_TOO_LARGE` in every build.
- **pHash with an odd `dct_size` read and wrote misaligned `float`s.** Every odd value from
  3 to 31 placed the DCT buffers at an odd address: undefined behaviour, reported by UBSan
  and a crash on strict-alignment targets. Hash values do not change.
- `ph_compute_phash()` returned a hash computed from **uninitialized memory** when the DCT
  parameters were out of range; out-of-range parameters are rejected.
- **`ph_context_get_dimensions()` reported an image that was no longer loaded.** A failed
  `ph_load_from_file()`/`ph_load_from_memory()` drops the previous image, but its width,
  height and channel count stayed behind; they are reset to 0 with it. It and
  `ph_is_loaded()` take a `const ph_context_t *`.
- Resizing and Gaussian blur reported an allocation failure as `PH_SUCCESS` with a hash
  computed over garbage. aHash, dHash, pHash, wHash, mHash, BMH and Radial return
  `PH_ERR_ALLOCATION_FAILED` instead. No hash value changes on the success path.
- Pixel counts were computed in `int` and could overflow (undefined behaviour); they are
  computed in `size_t`.
- **A gray + alpha image decoded by stb_image read one byte past its pixel buffer** and
  hashed a mixture of gray and alpha values, in every build without a native PNG decoder
  (the Makefile build). stb_image also hands back a grayscale PNG as one channel, so
  ColorHash and ColorMoments refused an image that a libpng build accepted, and
  grayscale loading used stb_image's own gray weights instead of the library's. libpng,
  for its part, converted a 16-bit colour PNG to gray before reducing it to 8 bits, a level
  away from the other decoders. Every PNG decoder produces the same channel layout and the
  same gray values.
- **`ph_hamming_distance_digest()` silently undercounted on x86_64** for a digest whose
  size in bytes wasn't a multiple of 32: the AVX2 loop advanced its index as a byte offset,
  but the SSE4.2 loop after it compared that index against a word count, so the last bytes
  of the digest were dropped from the popcount without any error. A 32-bit x86 build with
  AVX2 available failed to link, on an intrinsic that exists only on x86-64. Both lived in
  hand-written vector paths that bought nothing on a digest of at most 128 bytes; the
  function is one portable loop over 64-bit words on every target.
- **32-bit x86 (`i686`) builds could produce a different hash than a 64-bit build for the
  same image**, including a degenerate all-zero pHash for a uniform-colour input that a
  64-bit build hashes normally: GCC/Clang default to x87 extended-precision intermediates
  on 32-bit x86. See "Changed" for the SSE2 floor.
- **The library did not compile for any target where `size_t` is not `unsigned long`.**
  The native decoder backends declared their compressed-buffer length as `unsigned long`
  while the backend table declares it as `size_t`: GCC 14+/Clang 16+ reject that on a
  32-bit target, and on Windows x64 it compiled but the decoder read a 32-bit length out of
  a 64-bit one. Buffer lengths are `size_t` throughout.
- **`libphash.h` could not be included by GCC in C23 mode** — and, since GCC 15 defaults to
  C23, by any program compiled with plain `gcc`. `PH_NODISCARD` stood after the visibility
  attribute, a position C23 does not allow; the attributes are in the conforming order.
- **A Windows build could not succeed with a native toolchain.** A static build failed to
  compile (`PH_API` had no case for a static library), and a build with any of the bundled
  decoders failed on POSIX-only file-mapping headers, `<stdatomic.h>` under MSVC, the
  missing `M_PI` and `libm`, and Unix archive naming in the link paths. Both linkages build
  with MSVC; see `PHASH_STATIC_DEFINE` under Added.
- PNG decoder: `setjmp` is armed *before* the info struct is created, so an early libpng
  error does not longjmp into an unprepared state; the `row_ptrs` allocation is checked for
  overflow; an error while reading the pixels frees every buffer the decoder allocated.
- The spng backend could not decode to grayscale at all: it asked spng for an 8-bit gray
  output format unconditionally, which spng only accepts for images that are already
  grayscale. It converts when the source format requires it, byte for byte identically to
  the libpng backend.
- `add_subdirectory()` forced libpng's `PNG_SHARED`/`PNG_STATIC` into the parent project's
  cache; see Packaging under Added.
- The vendored `stb_image_resize2` crashed or leaked when one of its internal allocations
  failed under AddressSanitizer's separate-allocation mode. Patched locally pending an
  upstream fix.

### Security

- **Decompression bombs** are rejected before any pixel buffer is allocated (1.x had no
  size limit), via a configurable `max_pixels` limit (256 Mi pixels by default), an
  unconditional `INT_MAX`-pixel implementation ceiling, and a per-dimension cap of 1000000
  pixels that no `max_pixels` setting can lift.
- **Heap overflow / signed overflow** reachable through `int` pixel-count arithmetic on
  large images. Fixed by the ceiling and by `size_t` arithmetic.
- **Out-of-bounds read** in every public helper that reads a `ph_digest_t`: a
  hand-assembled digest with `size > 64` read past the end of the struct. Rejected.
- **The mock decoder does not ship.** 1.x registered it in every build, ahead of the real
  catch-all backend, where it intercepted any input beginning with `DE AD`.
- **Heap out-of-bounds write / segfault in the Block Mean Hash** on a 32-bit build:
  `block_size * block_size` was cast to `size_t` before multiplying, which only prevents
  overflow where `size_t` is wider than `int` — on 32-bit `size_t` it wrapped and produced
  a 1-byte scratch buffer that the resize step then wrote a full block into. The size goes
  through the library's overflow-checked allocation-size helper.
- **PNG decoder hardening:** overflow-checked `row_ptrs` allocation, the dimension cap
  applied straight from the IHDR, and `setjmp` armed before the info struct exists.
- The decode path is fuzzed (libFuzzer) and built under ASan/UBSan in CI.

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
