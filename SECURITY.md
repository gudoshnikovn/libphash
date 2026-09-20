# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| 2.x (latest minor) | Yes |
| 1.x | No, once 2.0.0 ships — see `MIGRATION.md` |

Only the latest minor of the current major version receives security fixes. This is a
single-maintainer project; there is no capacity to backport fixes across multiple
majors.

## Reporting a vulnerability

Please report suspected vulnerabilities privately through **[GitHub Security
Advisories](https://github.com/gudoshnikovn/libphash/security/advisories/new)**
("Report a vulnerability" under this repository's Security tab) rather than a public
issue. Please include:

- The version (or commit) affected.
- A minimal reproducer — for this library, that usually means a crafted input file
  or buffer that triggers the problem through the public API (`ph_load_from_file()`,
  `ph_load_from_memory()`, `ph_load_from_pixels()`, or a hash/comparison function).
- What you observed (crash, sanitizer report, hang, wrong output with a security
  implication) versus what you expected.
- Which decoder backend(s) it reproduces under, if known (native TurboJPEG/libpng/
  spng/libwebp, or the `stb_image` fallback) — see `docs/development.md` for how to
  build each configuration.

**Response SLA:** an acknowledgment within 7 days, and a fix or a mitigation plan
within 90 days of confirmation, whichever is reasonable for the severity — a crash on
untrusted input is treated as higher priority than a logic bug with no safety impact.
This is a best-effort SLA from a single maintainer, not a contractual guarantee.

## What counts as a vulnerability here

This library's job is to decode and hash **untrusted** image data — that is its
threat model, and it is the only one. See "Threat model" below for what "untrusted"
does and does not cover.

In scope:
- Memory safety issues (buffer overflow, use-after-free, uninitialized read, etc.)
  reachable by calling any public function in `include/libphash.h` on attacker-
  controlled input (a file path pointing at a file the caller does not otherwise
  trust, or a buffer passed to `ph_load_from_memory()`).
- Resource-exhaustion issues reachable the same way that aren't already covered by a
  documented limit (`ph_context_set_max_pixels()`, the built-in per-dimension cap, or
  the `PH_MAX_SUPPORTED_PIXELS` implementation ceiling — see `include/libphash.h` and
  `MIGRATION.md`). A crafted file that exceeds a documented, enforced limit and is
  correctly rejected is not a vulnerability; a crafted file that evades a documented
  limit is.
- A vulnerability in a vendored decoder (`vendor/libjpeg-turbo`, `vendor/libpng`,
  `vendor/spng`, `vendor/libwebp`, `vendor/zlib-ng`, or the copied-in
  `vendor/stb_image.h`/`vendor/stb_image_resize2.h`) that this project ships and
  that isn't already fixed upstream. Please also report it upstream — see
  "Vendored dependencies" below for why this project cannot simply subscribe to
  their advisories yet for all of them.

Out of scope:
- The hash algorithms themselves are **unkeyed and deterministic by design** — this
  is a deduplication library for a collection its operator controls, not an
  authentication or moderation primitive. An attacker who can choose two images can
  always force a hash collision between them, or break a match between an image and
  a modified copy of itself; this is expected and is not a vulnerability to report.
  See `docs/algorithms.md`'s scope section and `docs/references.md` (Dolhansky &
  Canton Ferrer 2020) for why no perceptual hash, keyed or not, fits an adversarial
  use case, and why this library does not claim to.
- Anything requiring the caller to already pass attacker-controlled data to a
  function this library documents as trusting its caller (e.g. `stride`/`width`/
  `height`/`channels` to `ph_load_from_pixels()`, which takes a raw pixel buffer the
  caller decoded themselves — there is no format to parse, so there is no untrusted
  input to defend against at that entry point).
- Denial of service from processing a very large but otherwise valid image within
  the caller's own configured limits — raise `max_pixels` and you accept the memory/
  CPU cost that comes with it; that is a resource-budgeting choice, not a bug.

## Threat model

**What this library guarantees on untrusted input** (a file path or buffer from a
source the caller does not control, passed to `ph_load_from_file()`/
`ph_load_from_memory()`): no out-of-bounds memory access, no code execution, and a
defined error code (`ph_error_t`) rather than undefined behavior, for any input —
malformed, truncated, adversarially crafted, or simply not an image at all. Every
decode path is covered by continuous fuzzing (`tests/fuzz/fuzz_load.c`, libFuzzer,
gated behind `PHASH_BUILD_FUZZERS`) and by the sanitizer-instrumented CI legs
(ASan/UBSan, TSan for the threaded batch path, Valgrind for the allocation-failure
suite).

**What it does not guarantee:**
- **Availability under a resource budget you configured generously.** `max_pixels`,
  the per-dimension cap, and `PH_MAX_SUPPORTED_PIXELS` bound memory use for a given
  configuration, but a caller who raises those limits accepts the corresponding
  memory/CPU cost — that is a caller decision, not a library defect.
- **Determinism of the *value* a hash takes across machines, in the strict sense.**
  Ordinary photographs hash identically everywhere this library is tested, but two
  algorithms (pHash, Radial) can differ by a few bits between CPU architectures for
  near-uniform/degenerate input — see `CHANGELOG.md`'s `## [2.0.0]` Changed section.
  This is a floating-point reproducibility caveat, not a security property, and does
  not affect ordinary use.
- **Resistance to a deliberate adversary trying to produce a hash collision or a
  hash mismatch for two images.** See "What counts as a vulnerability here" above —
  this is out of scope by design, not an oversight.
- **Any guarantee about `ph_load_from_pixels()`'s input beyond what its own
  documented contract states.** It takes raw pixel data with no format to parse, so
  it trusts `width`/`height`/`channels`/`stride` the way any function taking a raw
  buffer and its own dimensions does.

## Vendored dependencies

This library bundles five decoder libraries as git submodules
(`libjpeg-turbo`, `libpng`, `spng`, `libwebp`, `zlib-ng`) plus two files copied
directly into the tree rather than submoduled (`vendor/stb_image.h`,
`vendor/stb_image_resize2.h` — both locally patched; see `THIRD-PARTY-NOTICES.md`
for their exact pinned versions and hashes, and `docs/development.md` for why they
carry local patches instead of being submodules like the rest of `vendor/`).

- **Submoduled dependencies:** [Dependabot](https://docs.github.com/en/code-security/dependabot)
  is configured (`.github/dependabot.yml`, `gitsubmodule` ecosystem) to open a pull
  request when any of the five submodules has a newer upstream tag. A submodule bump
  still needs a human to review it against this project's own test suite before
  merging — a newer decoder version is not merged blindly.
- **The two copied-in stb headers** are not something Dependabot can see, since they
  aren't submodules. A monthly scheduled workflow
  (`.github/workflows/stb-freshness-check.yml`, `scripts/check_stb_freshness.sh`)
  compares each file's pinned upstream hash (recorded in `THIRD-PARTY-NOTICES.md`)
  against the current upstream file and opens a tracking issue if they differ — the
  bump itself is still a manual, reviewed task, since it has to reapply the local
  OOM-handling patch and re-verify against `tests/src/test_alloc_failure.c` rather
  than being a drop-in file replacement.

## Fuzzing

`tests/fuzz/fuzz_load.c` is a libFuzzer harness over `ph_decode_buffer()`, the single
entry point every format-decoding path funnels through. Build it with
`-DPHASH_BUILD_FUZZERS=ON` (requires Clang — libFuzzer needs compiler-rt, so this
option is a configure-time error under GCC). CI runs it nightly for 30 minutes with a
cached, growing corpus (see `.github/workflows/ci.yml`); a crash there is treated the
same as a privately reported vulnerability.
