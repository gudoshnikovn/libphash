# Migration guide: 1.x → 2.0

This is a major release because of a small number of changes a consumer **cannot
notice at upgrade time**: the same input can produce a different hash, and one error
constant is gone. Everything here has a fuller rationale in `CHANGELOG.md`'s
`## [2.0.0]` BREAKING CHANGES section — this document is the action list: what breaks,
and what to change in your own code.

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
ph_context_set_auto_orient(ctx, 0); /* opt back into hashing raw sensor pixels */
```

Read the EXIF-orientation entry below before doing this: it means the hash now
describes the *undisplayed* sensor buffer, not what a viewer shows the user, which is
usually not what you actually want long-term — it is offered as a bridge to keep old
values valid while you plan a rehash, not as the recommended steady state.

## Error codes: `PH_ERR_DECODE_FAILED` is gone

It had already stopped being returned from anywhere while still declared, so
`if (err == PH_ERR_DECODE_FAILED)` was silently dead code with no compiler warning.
Replace it with the specific code(s) you actually need to handle:

| Old code (removed) | What the failure actually was | New code |
|---|---|---|
| `PH_ERR_DECODE_FAILED` | Format not recognized at all | `PH_ERR_UNSUPPORTED_FORMAT` |
| `PH_ERR_DECODE_FAILED` | Format recognized, bytes corrupt/truncated | `PH_ERR_CORRUPT_DATA` |
| `PH_ERR_DECODE_FAILED` | Image exceeds size limits | `PH_ERR_IMAGE_TOO_LARGE` |
| `PH_ERR_DECODE_FAILED` | File/path could not be read at all | `PH_ERR_IO` |
| `PH_ERR_DECODE_FAILED` | Format recognized, no decoder compiled in (e.g. WebP without `PH_USE_WEBP`) | `PH_ERR_DECODER_UNAVAILABLE` |

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

## Config setters now return `ph_error_t`, not `void`

Every `ph_context_set_*()` function has this contract now: a valid argument returns
`PH_SUCCESS`; anything else returns `PH_ERR_INVALID_ARGUMENT` and leaves the existing
configuration **completely unchanged**. Previously an invalid argument was silently
swallowed, so a whole batch could run against a configuration you never intended.
Existing call sites still compile — the setters are deliberately not
`warn_unused_result` — but a call that used to be silently ignored now just leaves the
old value in place instead, so it is worth checking.

```c
/* Before (1.x) — return value didn't exist */
ph_context_set_phash_params(ctx, dct_size, reduction_size);

/* After (2.0) — check it, at least where the arguments come from outside your code */
if (ph_context_set_phash_params(ctx, dct_size, reduction_size) != PH_SUCCESS) {
    /* dct_size/reduction_size out of bounds; the previous setting is still active */
    handle_bad_config();
}
```

Bounds enforced now (previously unchecked): `gamma` finite in `(0.001, 1000]`; gray
weights each ≥ 0 with a sum in `(0, INT_MAX/255]`; `dct_size` 1..32; `reduction_size`
1..8 and ≤ `dct_size`; radial `projections` 1..64; radial `samples` 1..65536;
`block_size` 1..32 (was 1..22, see the `ph_digest_t` entry below); `whash_mode` a
declared enumerator only.

## `ph_digest_t` grew and gained a `kind` tag

`PH_DIGEST_MAX_BYTES` is now 128 (was 64) and the struct carries a `ph_digest_kind_t
kind` field after `size`. This matters to you only if you read/write the struct's raw
bytes directly (an FFI binding, serialization code, a hand-built digest) rather than
treating it opaquely:

```c
/* Any code that hardcodes the 1.x layout (size 72, no kind byte) must be rebuilt
 * against the 2.0 header -- struct offsets moved. */
```

Comparison functions now refuse a digest whose `kind` doesn't match the metric:
`ph_hamming_distance_digest()`/`ph_similarity_digest()` return `-1` for a radial or
color-moments digest; `ph_l2_distance()` returns `-1` for a BMH digest.
`PH_DIGEST_KIND_UNSPECIFIED` is `0`, so a hand-filled struct that never set `kind`
behaves exactly as before (no protection, but no new failure either).

## Digest helpers reject `size > PH_DIGEST_MAX_BYTES` instead of reading past the struct

If you ever construct a `ph_digest_t` by hand (rather than getting one back from this
library), make sure `size` never exceeds `PH_DIGEST_MAX_BYTES` (128) — it used to be
read as given, which meant an out-of-bounds read for anything larger.

```c
ph_digest_t d = {0};
d.size = 200; /* invalid: > PH_DIGEST_MAX_BYTES */
ph_hamming_distance_digest(&d, &other); /* now returns -1 instead of reading OOB */
```

A `size` of `0` is also uniformly `-1` from every comparison function now (previously
`ph_hamming_distance_digest()`/`ph_l2_distance()` returned `0`, i.e. "identical", for
two empty digests, while `ph_similarity_digest()` already returned `-1`).

## Color algorithms now refuse grayscale images

`ph_compute_color_hash()`, `ph_compute_color_moments_hash()`, and
`ph_compute_multi()` with `PH_HASH_COLOR_HASH` return `PH_ERR_REQUIRES_COLOR` when the
loaded image has fewer than 3 channels, instead of silently reading r/g/b out of one
replicated channel and returning a meaningless-but-successful result.

```c
ph_context_set_load_grayscale(ctx, 0); /* default; load in color before a color hash */
ph_load_from_file(ctx, path);
ph_digest_t d;
ph_error_t err = ph_compute_color_hash(ctx, &d);
if (err == PH_ERR_REQUIRES_COLOR) {
    /* image genuinely has < 3 channels -- skip it, this call cannot succeed on it */
}
```

## Rewritten algorithms: no compatible mode, recompute is the only option

Each of these changed enough that a stored 1.x value **cannot** be compared against a
2.0 value at all — treat every stored digest from these five algorithms as invalid and
plan a rehash of anything that used them. Full rationale for each is in `CHANGELOG.md`;
this is the "what do I call now" summary.

- **`ph_compute_color_hash()`** — no longer the old bit-vector ColorHash;
  `PH_HASH_COLOR_HASH` is gone from `ph_hash_flags_t`. It's a 108-byte colour
  histogram now (`PH_DIGEST_KIND_HISTOGRAM`); compare with
  `ph_histogram_intersection()`, not `ph_hamming_distance_digest()`.

  ```c
  ph_digest_t a, b;
  ph_compute_color_hash(ctx_a, &a);
  ph_compute_color_hash(ctx_b, &b);
  double score;
  ph_histogram_intersection(&a, &b, &score); /* not ph_hamming_distance_digest() */
  ```

- **`ph_compute_mhash()`** — a real Marr-Hildreth hash now, 72-byte digest (576 bits),
  not the old 64-bit discrete-Laplacian bit vector. `PH_HASH_MHASH` is gone from
  `ph_hash_flags_t` (576 bits can't travel through a `uint64_t`-returning bitfield
  API) — call `ph_compute_mhash()` directly instead of through `ph_compute_multi()`.
  New `ph_context_set_mhash_params()` exposes the kernel's `alpha`/`level`.

- **`ph_compute_color_moments_hash()`** — 18-byte digest now (was 9), each of the nine
  features a signed 16-bit fixed-point value (`PH_DIGEST_KIND_VECTOR16`,
  `PH_VECTOR16_SCALE`) instead of an unsigned byte that discarded the sign of
  skewness. Use `ph_l2_distance()` as before; it decodes the new encoding
  automatically.

- **Block Mean Hash (BMH)** — thresholds against the block-mean *median* now, not the
  arithmetic mean, per the algorithm's own reference method. Every BMH value changes;
  no code change needed beyond recomputing stored values. Note this also means BMH
  values now differ from OpenCV's `BlockMeanHash`, which uses the mean.

- **Radial hash** — three separate fixes landed together in this release (real DCT
  instead of raw angle samples, corrected gamma convention/default, parameterized blur
  sigma) — every radial value changes again versus any earlier 2.0 pre-release you may
  have tested against, not just versus 1.x. Compare radial digests with
  `ph_radial_similarity()` only:

  ```c
  ph_digest_t a, b;
  ph_compute_radial_hash(ctx_a, &a);
  ph_compute_radial_hash(ctx_b, &b);
  double score;
  ph_error_t err = ph_radial_similarity(&a, &b, &score);
  /* score >= PH_RADIAL_PCC_THRESHOLD (0.9) is the algorithm's own similarity cutoff */
  ```

  Also corrected in the docs: Radial tolerates a few degrees of rotation plus an exact
  half turn, not "up to 360°" as older docs claimed.

## EXIF auto-orientation is on by default

Images carrying an `Orientation` EXIF tag other than 1 (most phone/camera photos) are
now rotated/mirrored before hashing, so the hash describes what a viewer displays
rather than the raw sensor buffer. See "Restoring 1.x hash values" above for the
opt-out, and "Strategy for an existing hash database" for what to do about it instead
of opting out.

## `max_pixels = 0` no longer means "no limit"

It now means "no limit of my own, but an implementation ceiling of `INT_MAX`
(2,147,483,647) pixels still applies and caps any larger value you configure too". An
image above the effective limit is rejected with `PH_ERR_IMAGE_TOO_LARGE`. This was a
real overflow/heap-overflow bug reachable through the documented "0 = unlimited" mode,
not a style choice — there is no way to restore the old behavior, and the default
limit (256 MP) is eight times below the new ceiling, so only callers who had
deliberately raised or disabled the limit are affected.

## A single image dimension may not exceed 1,000,000 pixels

New, independent of `max_pixels` — setting `max_pixels` higher or to `0` does not lift
it. Rejected with `PH_ERR_IMAGE_TOO_LARGE`. In practice this only affects synthetic
test input (an absurd aspect ratio like 268,435,456×1 sits exactly on the default area
limit but would make the decoder allocate an ~800 MB single row); real photographs are
nowhere near it.

## Build and linking

### `PH_VERSION_NUMBER` uses a new scheme

```c
/* Before (1.x): major*10000 + minor*100 + patch -- collided past minor/patch 99
 * (1.100.0 and 2.0.0 both produced 20000) */
#if PH_VERSION_NUMBER >= 10500  /* meant "1.5.0 or later" */

/* After (2.0): major*1000000 + minor*1000 + patch */
#if PH_VERSION_NUMBER >= 2000000  /* "2.0.0 or later" */
```

Recompute any compile-time `#if PH_VERSION_NUMBER >= ...` check against the new
scheme — there is no way to keep both.

### Shared library consumers must relink

Shared builds now carry a versioned soname, `SOVERSION = 2` (`libphash.so.2` /
`libphash.2.dylib`). A binary linked against an unversioned 1.x shared library will
not pick up 2.0 at runtime — relink against the installed 2.x library.

### `find_package(phash)` and pkg-config now exist

If your 1.x integration linked libphash by hand (raw `-lphash` plus manually-tracked
include paths and backend libraries), you can now use either:

```cmake
# CMake
find_package(phash 2 REQUIRED)
target_link_libraries(my_app PRIVATE phash::phash)
```

`find_package(phash 1.x REQUIRED)` correctly refuses to pick up an installed 2.0.0
package (`COMPATIBILITY SameMajorVersion`), so a version-pinned consumer fails loudly
at configure time instead of silently linking an incompatible major.

```
# pkg-config
$ cc my_app.c $(pkg-config --cflags --libs libphash) -o my_app
```

Both forms pull in whatever backend libraries (`-lturbojpeg`, `-lpng16`, `-lwebp`,
`-lz`, ...) the installed build was actually configured with — you no longer need to
track that list by hand for a static link.

## Smaller, situational changes

These only affect you if your code specifically depends on the old behavior:

- **The test-only mock decoder is no longer compiled into a normal Release build.**
  If you were relying on it (you should not have been, in a shipped build), configure
  with `-DPHASH_ENABLE_MOCK_BACKEND=ON` explicitly.
- **Enabling both PNG backends (`PHASH_USE_LIBPNG` and `PHASH_USE_SPNG`) is now a
  configure-time error** instead of silently picking one. Choose one explicitly.
- **Non-regular files passed to `ph_load_from_file()`** (a FIFO, a character device,
  `/dev/stdin`) now return `PH_ERR_IO` instead of being read. Read the stream into a
  buffer yourself and call `ph_load_from_memory()` instead — regular files are
  unaffected.

## Python bindings (`python-libphash`)

The Python bindings live in a separate repository and get their own major version
bump **after** this C library's 2.0.0 ships, not simultaneously — see that project's
own changelog/migration notes once it is released. Nothing in this document applies
directly to Python callers; wait for that project's own 2.x release notes.
