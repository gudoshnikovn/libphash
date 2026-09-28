# Migration guide: 1.x → 2.0

This guide is written for a program built against libphash 1.10.4. Upgrading to 2.0
changes three things: several functions changed signature and two error constants were
removed, so some code no longer compiles; some calls that compile unchanged now behave
differently; and the same input can produce a different hash. The sections below follow
that order, then cover building and linking. `CHANGELOG.md`'s `## [2.0.0]` section has
the full list of changes; this document is the action list.

## Before you upgrade: read this first

If you store hash values anywhere (a database, an index, a dedup table), **the values
you already have are about to go stale for reasons that produce no error and no
warning.** The single biggest cause is auto-orientation (below), because it silently
affects most photos ever taken on a phone or camera. Recall in deduplication/search
will quietly degrade after upgrading unless you plan for it.

### Strategy for an existing hash database

Pick one, in order of how much work it costs you:

1. **Recompute everything.** Simplest and correct. If your corpus is small enough or
   you can afford a batch job, just do this and stop reading this subsection.
2. **Recompute lazily.** Rehash an item the next time it's read/compared; tolerate
   stale hashes for items that are never touched again.
3. **Store the algorithm version next to every hash value**, and recommended even if
   you do (1) or (2): a plain `uint64_t`/`ph_digest_t` on its own carries no way to
   tell a 1.x hash from a 2.0 one apart after the fact. Add a version column/field
   (e.g. `PH_VERSION_NUMBER` at the time the hash was computed, or just a "2" you bump
   yourself) so a future upgrade doesn't repeat this problem blind. This is the one
   piece of housekeeping most callers don't already have, and the main practical
   takeaway of this guide.

```c
/* Minimal example of (3): pair every stored hash with the version that produced it. */
struct stored_hash {
    uint64_t phash;
    uint32_t computed_with_version; /* = PH_VERSION_NUMBER at insert time */
};
```

## Restoring 1.x hash values (where possible)

**Auto-orientation is the one breaking change you can opt out of.** Every other hash
value change below (color hash, mHash, color moments, BMH, radial) is a rewritten
algorithm with no "old mode" switch — recompute is the only path, per each item's own
entry.

```c
ph_context_t *ctx;
ph_create(&ctx);
ph_context_set_auto_orient(ctx, 0); /* hash the stored pixels, as 1.x did */
```

Read the EXIF-orientation entry below before doing this: it means the hash describes
the *undisplayed* sensor buffer, not what a viewer shows the user, which is usually not
what you actually want long-term — it is offered as a bridge to keep old values valid
while you plan a rehash, not as the recommended steady state.

---

# Code that no longer compiles

## `ph_compute_mhash()` and `ph_compute_color_hash()` return a `ph_digest_t`

Both algorithms are rewritten (see "Rewritten algorithms" below), and neither result
fits a `uint64_t`: mHash is 576 bits, ColorHash a 108-bin histogram.

```c
/* Before (1.x) */
uint64_t m, c;
ph_compute_mhash(ctx, &m);
ph_compute_color_hash(ctx, &c);

/* After (2.0) */
ph_digest_t m, c;
ph_compute_mhash(ctx, &m);       /* compare with ph_hamming_distance_digest() */
ph_compute_color_hash(ctx, &c);  /* compare with ph_histogram_intersection() */
```

## `ph_context_set_radial_params()` takes a third argument, `sigma`

`sigma` is the Gaussian blur applied before the projections (default 3.5). The first
argument keeps its name but not its meaning: in 1.x the projection count was also the
digest width (default 40); in 2.0 it is the number of angles only (default 180), and the
digest is always the first 40 coefficients of a DCT over them.

```c
/* Before (1.x) */
ph_context_set_radial_params(ctx, 40, 128);

/* After (2.0): the defaults, spelled out */
ph_context_set_radial_params(ctx, 180, 128, 3.5f);
```

A 1.x value of `projections` does not reproduce 1.x hashes in 2.0; the defaults are the
algorithm's source's.

## `PH_ERR_DECODE_FAILED` is removed

In 1.x it was the single code for every load failure. 2.0 reports the specific cause
instead, so a check against the old name no longer compiles — which is the point:
replace it with the codes you need.

| 1.x code (removed) | What the failure actually was | 2.0 code |
|---|---|---|
| `PH_ERR_DECODE_FAILED` | Format not recognized at all | `PH_ERR_UNSUPPORTED_FORMAT` |
| `PH_ERR_DECODE_FAILED` | Format recognized, bytes corrupt/truncated | `PH_ERR_CORRUPT_DATA` |
| `PH_ERR_DECODE_FAILED` | File/path could not be read at all | `PH_ERR_IO` |
| `PH_ERR_DECODE_FAILED` | Format recognized, no decoder compiled in (e.g. WebP without `PH_USE_WEBP`) | `PH_ERR_DECODER_UNAVAILABLE` |
| `PH_ERR_DECODE_FAILED` | Decoder ran out of memory | `PH_ERR_ALLOCATION_FAILED` |

2.0 also rejects images above a size limit with `PH_ERR_IMAGE_TOO_LARGE`; 1.x had no
such limit (see "Large images are rejected by default" below).

```c
/* Before (1.x) */
ph_error_t err = ph_load_from_file(ctx, path);
if (err == PH_ERR_DECODE_FAILED) {
    log_skip(path);
}

/* After (2.0) */
ph_error_t err = ph_load_from_file(ctx, path);
switch (err) {
case PH_ERR_UNSUPPORTED_FORMAT:
case PH_ERR_CORRUPT_DATA:
case PH_ERR_IMAGE_TOO_LARGE:
case PH_ERR_IO:
case PH_ERR_DECODER_UNAVAILABLE:
    log_skip(path);
    break;
default:
    break;
}
```

`ph_get_last_error_message()` adds the decoder's own reason for a failed load.

## `PH_ERR_NOT_IMPLEMENTED` is removed

Nothing in 1.x returned it, so a check against it could never fire. Its value `-4` is
reserved and never reused. Delete any branch that tests for it. A feature missing from a
build is `PH_ERR_DECODER_UNAVAILABLE` for a decoder; a build without threads runs a
batch sequentially.

## `ph_can_use_libjpeg()`/`ph_can_use_libpng()` are `ph_can_use_jpeg()`/`ph_can_use_png()`

All three capability checks, with `ph_can_use_webp()`, are named after the format.
`ph_can_use_png()` answers "a native PNG decoder, libpng or spng, is compiled in" — what
1.x's `ph_can_use_libpng()` returned too, including in a spng build where libpng is not
linked. Return values are unchanged; rename the calls. `ph_get_build_info()` names the
library behind each format.

## `ph_digest_t` is 136 bytes and carries a `kind` tag

`PH_DIGEST_MAX_BYTES` is 128 (1.x: 64), and the byte after `size` is a
`ph_digest_kind_t kind` tag (1.x: padding). Source code that treats the struct through
its fields recompiles unchanged; anything that hardcodes the 1.x layout — a 72-byte
struct, no `kind` byte — must be rebuilt against the 2.0 header: an FFI binding,
serialization code, a stored raw struct.

---

# Code that compiles but behaves differently

## Config setters return `ph_error_t` and reject invalid input

In 1.x the setters returned `void`, and an out-of-range value was ignored (or, for the
gray weights, replaced by the defaults). In 2.0 a valid argument returns `PH_SUCCESS`;
anything else returns `PH_ERR_INVALID_ARGUMENT` and leaves the configuration
**completely unchanged**. Existing call sites still compile — the setters are
deliberately not `warn_unused_result` — but check the result wherever the arguments come
from outside your code.

```c
/* Before (1.x): no return value */
ph_context_set_phash_params(ctx, dct_size, reduction_size);

/* After (2.0) */
if (ph_context_set_phash_params(ctx, dct_size, reduction_size) != PH_SUCCESS) {
    /* dct_size/reduction_size out of bounds; the previous setting is still active */
    handle_bad_config();
}
```

Bounds: `gamma` finite in `(0.001, 1000]`; gray weights each ≥ 0 with a sum in
`(0, INT_MAX/255]` (a zero sum is an error, not a request for the defaults);
`dct_size` 1..32; `reduction_size` 2..8 and ≤ `dct_size`; radial `projections`
40..131072, `samples` 2..65536, `sigma` in `(0, 64/3]`; `block_size` 2..32 (1.x
accepted any positive value and truncated the BMH digest to 64 bytes above 22×22);
`whash_mode` a declared enumerator only.

## `ph_context_set_gamma()`: exponent `gamma`, default 1.0

1.x raised pixels to `1/gamma` with a default of 2.2. 2.0 follows pHash: pixels are
normalised by the buffer's maximum, raised to `gamma`, and rescaled, with a default of
1.0 — an identity. Gamma affects Radial only, in both versions. If you set gamma
explicitly, the same number now means the inverse; review it or drop the call.

## Hashing a context with no image returns `PH_ERR_EMPTY_IMAGE`

In 1.x every `ph_compute_*` function returned `PH_ERR_INVALID_ARGUMENT` for a context
with nothing loaded. 2.0 returns `PH_ERR_EMPTY_IMAGE` (`-5`) for a fresh context and for
one whose last `ph_load_from_file()`/`ph_load_from_memory()` failed.
`PH_ERR_INVALID_ARGUMENT` means only what it says: a NULL pointer or an out-of-range
value. Pointer arguments are checked first, so a NULL output pointer is
`PH_ERR_INVALID_ARGUMENT` whether or not an image is loaded.

```c
/* Before (1.x): "forgot to load" and "passed NULL" were the same code */
if (ph_compute_phash(ctx, &hash) == PH_ERR_INVALID_ARGUMENT) { ... }

/* After (2.0) */
switch (ph_compute_phash(ctx, &hash)) {
case PH_ERR_EMPTY_IMAGE:      /* the load before this failed, or never happened */
    break;
case PH_ERR_INVALID_ARGUMENT: /* a bug in the call itself */
    break;
default:
    break;
}
```

## Large images are rejected by default

1.x decoded any size. 2.0 refuses an image whose width × height exceeds `max_pixels`
(default 256 Mi = 268,435,456 pixels) before allocating it, returning
`PH_ERR_IMAGE_TOO_LARGE`. Raise the limit with `ph_context_set_max_pixels()`; `0` removes
your own limit, but an implementation ceiling of `INT_MAX` (2,147,483,647) pixels always
applies.

Independently of `max_pixels`, a single dimension may not exceed 1,000,000 pixels —
setting `max_pixels` higher or to `0` does not lift it. In practice this only affects
synthetic input (an aspect ratio like 268,435,456×1 sits exactly on the default area limit
but would make the decoder allocate an ~800 MB single row); real photographs are nowhere
near it.

## Color algorithms refuse grayscale images

`ph_compute_color_hash()` and `ph_compute_color_moments_hash()` return
`PH_ERR_REQUIRES_COLOR` when the loaded image has fewer than 3 channels. 1.x read r, g
and b out of the one channel and returned a meaningless but successful result.

```c
ph_context_set_load_grayscale(ctx, 0); /* default; load in color before a color hash */
if (ph_load_from_file(ctx, path) == PH_SUCCESS) {
    ph_digest_t d;
    if (ph_compute_color_hash(ctx, &d) == PH_ERR_REQUIRES_COLOR) {
        /* the image has < 3 channels -- skip it, this call cannot succeed on it */
    }
}
```

## Digest comparisons check `size` and `kind`

- A digest whose `size` exceeds `PH_DIGEST_MAX_BYTES` (128) is rejected with `-1`. 1.x
  read `size` bytes as given, past the end of `data` for anything above 64.
- A `size` of `0` is `-1` from every comparison. 1.x returned `0`, i.e. "identical", from
  `ph_hamming_distance_digest()` and `ph_l2_distance()` for two empty digests.
- Each comparison refuses a digest tagged with a kind it is not for:
  `ph_hamming_distance_digest()` returns `-1` for a radial or color-moments digest, and
  `ph_l2_distance()` returns `-1` for a BMH digest. `PH_DIGEST_KIND_UNSPECIFIED` is `0`,
  so a hand-filled struct that never sets `kind` is accepted by every comparison (no
  protection, but no new failure either).

```c
ph_digest_t d = {0};
d.size = 200;                           /* invalid: > PH_DIGEST_MAX_BYTES */
ph_hamming_distance_digest(&d, &other); /* -1; 1.x read past the struct */
```

## Non-regular files are rejected

`ph_load_from_file()` returns `PH_ERR_IO` for a FIFO, a character device or `/dev/stdin`;
1.x read them. Read the stream into a buffer yourself and call `ph_load_from_memory()`.
Regular files are unaffected.

## EXIF auto-orientation is on by default

Images carrying an `Orientation` tag other than 1 (most phone/camera photos) are
rotated/mirrored before hashing, so the hash describes what a viewer displays rather than
the stored pixels, which is what 1.x hashed. See "Restoring 1.x hash values" above for the
opt-out, and "Strategy for an existing hash database" for what to do instead of opting
out.

---

# Hash values

## Rewritten algorithms: no compatible mode, recompute is the only option

Each of these changed enough that a stored 1.x value **cannot** be compared against a
2.0 value at all — treat every stored digest from these five algorithms as invalid and
plan a rehash of anything that used them. Full rationale for each is in `CHANGELOG.md`;
this is the "what do I call now" summary.

- **`ph_compute_color_hash()`** — a 108-byte colour histogram
  (`PH_DIGEST_KIND_HISTOGRAM`), not 1.x's 42-bit value; compare with
  `ph_histogram_intersection()`, not `ph_hamming_distance_digest()`.

  ```c
  ph_digest_t a, b;
  ph_compute_color_hash(ctx_a, &a);
  ph_compute_color_hash(ctx_b, &b);
  double score;
  ph_histogram_intersection(&a, &b, &score); /* not ph_hamming_distance_digest() */
  ```

- **`ph_compute_mhash()`** — a Marr–Hildreth hash, 72-byte digest (576 bits), not 1.x's
  64-bit discrete-Laplacian value. `ph_context_set_mhash_params()` exposes the kernel's
  `alpha`/`level` and the normalisation size.

- **`ph_compute_color_moments_hash()`** — an 18-byte digest (1.x: 9), each of the nine
  features a signed 16-bit fixed-point value (`PH_DIGEST_KIND_VECTOR16`,
  `PH_VECTOR16_SCALE`) instead of an unsigned byte that discarded the sign of skewness.
  Use `ph_l2_distance()` as before; it decodes the encoding.

- **Block Mean Hash (BMH)** — thresholds against the *median* of the block means, not
  their arithmetic mean, per the algorithm's own reference method. No code change needed
  beyond recomputing stored values. BMH values differ from OpenCV's `BlockMeanHash`,
  which uses the mean.

- **Radial hash** — reimplemented after its source: a DCT over the 180-angle variance
  vector, pHash's gamma and blur defaults. No 1.x radial value carries over. Compare
  radial digests with `ph_radial_similarity()` only:

  ```c
  ph_digest_t a, b;
  ph_compute_radial_hash(ctx_a, &a);
  ph_compute_radial_hash(ctx_b, &b);
  double score;
  ph_error_t err = ph_radial_similarity(&a, &b, &score);
  /* score >= PH_RADIAL_PCC_THRESHOLD (0.9) is the algorithm's own similarity cutoff */
  ```

  Radial tolerates a few degrees of rotation plus an exact half turn.

---

# Building and linking

## `PHASH_USE_TURBOJPEG` is `PHASH_USE_LIBJPEG_TURBO`

The CMake option that selects the native JPEG decoder is named after the codec,
libjpeg-turbo. Configuring with the 1.x name stops with an error that gives the new one:

```bash
cmake -S . -B build -DPHASH_USE_LIBJPEG_TURBO=OFF   # 1.x: -DPHASH_USE_TURBOJPEG=OFF
```

In an existing build directory, also drop the old cache entry with `-UPHASH_USE_TURBOJPEG`.

## Shared library consumers must relink

Shared builds carry a versioned soname, `SOVERSION = 2` (`libphash.so.2` /
`libphash.2.dylib`). A binary linked against an unversioned 1.x shared library will not
pick up 2.0 at runtime — relink against the installed 2.x library.

## `find_package(phash)` and pkg-config

If your 1.x integration linked libphash by hand (raw `-lphash` plus manually-tracked
include paths and backend libraries), you can use either of these instead:

```cmake
# CMake
find_package(phash 2 REQUIRED)
target_link_libraries(my_app PRIVATE phash::phash)
```

```
# pkg-config
$ cc my_app.c $(pkg-config --cflags --libs libphash) -o my_app
```

Both forms pull in whatever backend libraries (`-lphash_jpeg`, `-lpng16`, `-lwebp`,
`-lz`, ...) the installed build was configured with, so a static link does not need
that list tracked by hand.

## Smaller build changes

- **The test-only mock decoder is not compiled into the library.** 1.x registered a
  backend that claimed any buffer starting with `DE AD` and "decoded" it into a 1×1
  image; in 2.0 such a buffer is `PH_ERR_UNSUPPORTED_FORMAT`. It exists only for testing:
  `-DPHASH_ENABLE_MOCK_BACKEND=ON`, never in a shipped build.
- **Enabling both PNG backends (`PHASH_USE_LIBPNG` and `PHASH_USE_SPNG`) is a
  configure-time error**; 1.x built and linked both and used libpng. Choose one.
