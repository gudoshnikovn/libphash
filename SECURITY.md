# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| 2.x (latest minor) | Yes |
| 1.x | No — see `MIGRATION.md` to upgrade |

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
- Which decoder backend(s) it reproduces under, if known (native libjpeg-turbo/libpng/
  libwebp, or the `stb_image` fallback) — see the [development guide](https://gudoshnikovn.github.io/libphash/development/) for how to
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
  `vendor/libwebp`, `vendor/zlib-ng`, or the copied-in
  `vendor/stb_image.h`/`vendor/stb_image_resize2.h`) that this project ships and
  that isn't already fixed upstream. Please also report it upstream. See
  "Vendored dependencies" below for how updates to each are tracked.

Out of scope:
- The hash algorithms themselves are **unkeyed and deterministic by design** — this
  is a deduplication library for a collection its operator controls, not an
  authentication or moderation primitive. An attacker who can choose two images can
  always force a hash collision between them, or break a match between an image and
  a modified copy of itself; this is expected and is not a vulnerability to report.
  See [What a perceptual hash is not](https://gudoshnikovn.github.io/libphash/theory/perceptual-hashing/#what-a-perceptual-hash-is-not) and the [references](https://gudoshnikovn.github.io/libphash/references/) (Dolhansky &
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
decode path is fuzzed on every CI run and twice a week (`tests/fuzz/fuzz_load.c`, libFuzzer,
gated behind `PHASH_BUILD_FUZZERS`; see "Fuzzing" below) and exercised by the
sanitizer-instrumented CI legs
(ASan/UBSan, TSan for the threaded batch path, Valgrind for the allocation-failure
suite).

**What it does not guarantee:**
- **Availability under a resource budget you configured generously.** `max_pixels`,
  the per-dimension cap, and `PH_MAX_SUPPORTED_PIXELS` bound memory use for a given
  configuration, but a caller who raises those limits accepts the corresponding
  memory/CPU cost — that is a caller decision, not a library defect.
- **The same hash value from builds with different JPEG decoders.** Every algorithm gives
  the same bits on every OS, architecture and compiler the CI matrix covers, but
  libjpeg-turbo and stb_image decode a JPEG to slightly different pixels, so the hash of a
  JPEG depends on which one the build uses — see
  [Same hash on every machine](https://gudoshnikovn.github.io/libphash/theory/comparing/#same-hash-on-every-machine). This is a reproducibility property, not a security one.
- **Resistance to a deliberate adversary trying to produce a hash collision or a
  hash mismatch for two images.** See "What counts as a vulnerability here" above —
  this is out of scope by design, not an oversight.
- **Any guarantee about `ph_load_from_pixels()`'s input beyond what its own
  documented contract states.** It takes raw pixel data with no format to parse, so
  it trusts `width`/`height`/`channels`/`stride` the way any function taking a raw
  buffer and its own dimensions does.

## Vendored dependencies

This library bundles four decoder libraries as git submodules
(`libjpeg-turbo`, `libpng`, `libwebp`, `zlib-ng`) plus two files copied
directly into the tree rather than submoduled (`vendor/stb_image.h`,
`vendor/stb_image_resize2.h` — both locally patched; see `THIRD-PARTY-NOTICES.md`
for their exact pinned versions and hashes, `vendor/patches/` for the local patches they
carry, and the [development guide](https://gudoshnikovn.github.io/libphash/development/) for the reasons).

- **Submoduled dependencies** are each pinned to an upstream release tag, never to a
  commit between releases: a release is what upstream tested and announced, and the
  version `THIRD-PARTY-NOTICES.md` names. A weekly scheduled workflow
  (`.github/workflows/submodule-tags-check.yml`, `scripts/check_submodule_tags.sh`)
  checks that each submodule is on a release tag and that the tag is upstream's newest,
  and opens a tracking issue if not. Dependabot's submodule updates are not used: they
  follow a branch's newest commit, not its tags. A bump still needs a human to review it
  against this project's own test suite — a newer decoder version is not merged blindly.
- **The two copied-in stb headers** are not submodules and have no release tags. A
  monthly scheduled workflow
  (`.github/workflows/stb-freshness-check.yml`, `scripts/check_stb_freshness.sh`)
  compares each file's pinned upstream hash (recorded in `THIRD-PARTY-NOTICES.md`)
  against the current upstream file and opens a tracking issue if they differ — the
  bump itself is still a manual, reviewed task, since it has to reapply the local
  patches in `vendor/patches/` and re-verify against `tests/src/test_alloc_failure.c` rather
  than being a drop-in file replacement. On every push, `scripts/check_stb_patches.sh`
  checks that each header is exactly its recorded upstream commit plus its patch.

## Fuzzing

`tests/fuzz/fuzz_load.c` is a libFuzzer harness over the whole untrusted-input path:
it loads the input with `ph_load_from_memory()` — every decoder the build contains,
instrumented along with the library — and hashes the result with one algorithm, under
a configuration the input's last bytes choose. Build it with
`-DPHASH_BUILD_FUZZERS=ON` (requires Clang — libFuzzer needs compiler-rt, so this
option is a configure-time error under GCC). CI runs it twice a week for 30 minutes
(`.github/workflows/fuzz-scheduled.yml`), plus a 90-second run on every CI run (`ci.yml`).
Both start from the corpus the previous runs saved to the Actions cache and save it back
minimized; the scheduled run also keeps it as a 90-day artifact. A crash there is treated
the same as a privately reported vulnerability.

## Verifying a release artifact

Every archive attached to a GitHub Release comes with two things that say different
things about it:

- **`SHA256SUMS.txt`** shows that a download is intact. It does not show where the
  archive came from: it is served from the same place as the archives, so anyone able
  to replace an archive there could replace the checksum file with it.
- **A build provenance attestation** shows where it came from. The release workflow
  (`.github/workflows/release.yml`) attests every archive with
  `actions/attest-build-provenance`: a statement that the file with this SHA-256 was
  built by that workflow, in this repository, from the tagged commit, signed with a
  certificate GitHub issues to that one run and recorded in the public Sigstore
  transparency log. It cannot be produced without running the workflow here.

To check an archive, with the [GitHub CLI](https://cli.github.com/):

```bash
gh attestation verify libphash-2.0.1-linux-x86_64.tar.gz --repo gudoshnikovn/libphash
```

The command fails unless the archive is byte for byte one this repository's release
workflow built. The output names the workflow, the commit and the tag.

## Withdrawing a release

If a published release turns out to be defective — a security issue, a wrong artifact,
a build that does not work — it is not replaced under the same version:

1. The GitHub Release is marked as a pre-release (or deleted, if its archives must not
   be downloaded at all), with a note naming the problem and the fixed version. The tag
   stays: removing or moving a published tag breaks every build that pinned it, and the
   attestations of its archives keep pointing at that commit.
2. The fix is released as the next patch version, with a `CHANGELOG.md` entry that says
   which release it supersedes and why.
3. Dependents that ship the binaries — the Python bindings (`python-libphash`) among
   them — are told through a GitHub Security Advisory when the defect is a vulnerability,
   and through the release notes otherwise.
