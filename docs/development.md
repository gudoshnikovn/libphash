# Development Guide

This guide outlines the standards and procedures for contributing to `libphash`.

## Build Environment

### Toolchains
The project supports `gcc`, `clang`, and `msvc`. Clang/LLVM is the priority default in
both build systems (it's the more actively developed of the two Unix compilers this
project tests, and several sanitizer/architecture checks in `CMakeLists.txt` are
already Clang-specific) — still fully overridable, and CI tests both gcc and clang on
every push:

```bash
# Makefile: CC defaults to clang, override with CC=
make CC=gcc

# CMake: use the "clang" or "gcc" preset from CMakePresets.json, or override directly
cmake --preset clang -DPHASH_BUILD_TESTS=ON -B build
cmake -B build -DCMAKE_C_COMPILER=gcc  # equivalent, no presets
```

The C standard is pinned explicitly: `CMAKE_C_STANDARD 17` (`CMakeLists.txt`), with
`CMAKE_C_EXTENSIONS OFF` since nothing here needs the GNU dialect. It is not C23, even
though every toolchain this project tests on Linux/macOS handles it (the
`c-standard-matrix` CI job below builds it) — `windows-latest`'s MSVC support for
`/std:c23` isn't mature enough to trust as the project-wide default.

### Build systems and their defaults

There are two build systems, and they are not interchangeable:

- **CMake** — the high-performance build. Vendored SIMD decoders (libjpeg-turbo,
  libpng, libwebp, zlib-ng) from `vendor/`, `install()`/`find_package(phash)`
  packaging, `ctest`. This is what CI builds and what releases ship.
- **Makefile** — the portable/minimal build. No vendored decoders: `stb_image` only,
  one test binary per `tests/src/test_*.c`, plus the `debug`/`coverage` flows.

Because they are separate implementations, their defaults can drift, and a default
that differs between them means a code path that is exercised in one flow and dead in
the other. The table below is the single place where both are recorded — **keep it in
sync when you add or flip a switch.**

| Knob | CMake default | Makefile default | Notes |
|---|---|---|---|
| Bundled libjpeg-turbo | `PHASH_USE_LIBJPEG_TURBO=ON` | *n/a* (stb only) | Makefile has no native JPEG path |
| Bundled libpng | `PHASH_USE_LIBPNG=ON` | *n/a* (stb only) | Makefile has no native PNG path |
| libwebp | `PHASH_USE_WEBP=ON` | `USE_WEBP=0` | Makefile path expects a system libwebp |
| zlib-ng instead of system zlib | `PHASH_USE_ZLIB_NG=ON` | *n/a* | |
| **Batch thread pool** | `PHASH_ENABLE_THREADS=ON` | `PHASH_ENABLE_THREADS=1` | matches CMake's default |
| Shared library | `PHASH_BUILD_SHARED=OFF` | *n/a* (static `libphash.a` only) | |
| Tests | `PHASH_BUILD_TESTS=ON` | always built by `all` | |
| `-march=native` | `PHASH_OPTIMIZE_NATIVE=OFF` | *n/a* | without it the flags follow the target (see below); no AVX anywhere |
| libFuzzer harnesses | `PHASH_BUILD_FUZZERS=OFF` | *n/a* | requires Clang |
| Test-only mock decoder | `PHASH_ENABLE_MOCK_BACKEND=OFF` | `PHASH_ENABLE_MOCK_BACKEND=0` | must never be on in a shipped build |
| Strict dependency handling | `PHASH_STRICT_DEPS=OFF` | *n/a* | |
| Warnings as errors (own code only) | `PHASH_WARNINGS_AS_ERRORS=OFF` | *n/a* | for the `strict-warnings` CI job; never reaches the vendored decoders |
| Coverage instrumentation | `PHASH_COVERAGE=OFF` | `PHASH_COVERAGE=0` | same flag name, independent implementations — see below for why one build alone isn't enough |

**Architecture flags follow the target, not the host.** Both build systems read the
target from the compiler's predefined macros, so `-m32`, a toolchain file and
`CMAKE_OSX_ARCHITECTURES` all count:

| Target | Flags | CPU baseline |
|---|---|---|
| x86-64 | `-msse4.2` | x86-64-v2 (SSE4.2 + POPCNT, which the Hamming distances run on) |
| 32-bit x86 | `-msse2 -mfpmath=sse` | SSE2; x87 intermediates would make 32-bit hashes differ from 64-bit ones |
| arm64 | none | ARMv8-A; Advanced SIMD is part of the base architecture |

```bash
make EXTRA_CFLAGS=-m32 EXTRA_LDFLAGS=-m32          # 32-bit x86 on a 64-bit host
cmake -B build -DCMAKE_OSX_ARCHITECTURES=x86_64      # Intel macOS from Apple silicon
cmake -B build "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DPHASH_USE_LIBJPEG_TURBO=OFF
```

`EXTRA_CFLAGS`/`EXTRA_LDFLAGS` are appended after everything the Makefile sets; plain
`make CFLAGS=...` would replace the include paths too. A universal build gives the x86-64
slice its flag through `-Xarch_x86_64` and builds libpng with its portable filters, since
libpng chooses NEON or SSE2 sources for the whole build. The bundled libjpeg-turbo is a
separately configured single-architecture archive, so a universal build refuses it at
configure time: use stb_image for JPEG, or build each architecture and join them with
`lipo -create`.

Both spellings of the thread switch accept the same off-ramp:

```bash
make PHASH_ENABLE_THREADS=0                  # portable build without -pthread
cmake -S . -B build -DPHASH_ENABLE_THREADS=OFF
```

**Why threads are on in the portable build.** With the pool compiled out, `make test` and
`make coverage` would never execute the threaded half of
`ph_hash_files()`/`ph_hash_buffers()`. `-pthread` is therefore a dependency of the
portable build: a pthread implementation is present on every platform the Makefile
targets (it uses `uname` and POSIX tooling throughout), and coverage of the concurrent
path is worth more than the dependency.

### Instrumented build modes (Makefile)

`debug` and `coverage` are switch-driven (`PHASH_SANITIZE=1`, `PHASH_COVERAGE=1`) and
re-invoke `make` rather than listing `clean` as a sibling prerequisite, which would race
under `-jN` (`clean` deleting object files other jobs are compiling). Prefer the same shape for any future instrumented mode.

### Two coverage targets, and why one isn't enough

`make coverage` and `make coverage-cmake` measure disjoint code:

- **`make coverage`** runs the Makefile's stb_image-only build. Every line inside
  native decoder code in `src/loaders/` doesn't exist in that binary at all — `jpeg.c`
  and `webp.c` compile to nothing without their `PH_USE_*` flag, and `png_libpng.c`
  is not built. The overall percentage this target reports does **not** include the
  native decoders, no matter how high it reads.
- **`make coverage-cmake`** (`scripts/coverage_cmake.sh`) runs a CMake
  `-DPHASH_COVERAGE=ON` + `ctest` pass with the default vendored decoder set
  (libjpeg-turbo + libpng + libwebp + zlib-ng, i.e. what CI's `build-and-test` job
  and releases ship) and renders its `lcov` trace under
  `docs/coverage/cmake/html/index.html`. It is the only measurement of the native
  backends' own limit checks: each backend's `max_pixels` check
  (`test_decode_limits.c`, every format at its exact boundary), the libpng backend's
  per-dimension cap and the JPEG backend's encoded-length check (called directly, since
  the dispatcher answers the same inputs first), and `png_error_fn`/`png_warning_fn`
  with the `longjmp` that carries libpng's error message out.

  The row-buffer size checks behind `max_pixels` (`alloc_size`/stride in each backend)
  fail only where `size_t` is 32 bits: an image within `PH_MAX_SUPPORTED_PIXELS` needs
  at most 8 GiB, which a 64-bit `size_t` holds. libpng's is run by CI's 32-bit job; the
  JPEG one has no 32-bit libjpeg-turbo build to run in, and the WebP one cannot fail
  at all (VP8 caps a side at 16383 pixels). These blocks are marked as coverage
  exclusions (see "Coverage standard" below).

Neither target subsumes the other — always read the two side by side, and treat a
report that only ran one of them as measuring at most half the decoder surface.

`scripts/coverage_cmake.sh` treats a failing test the same way `make coverage`
does: coverage is a measurement pass, not a correctness gate, so a `ctest` failure
prints a warning and the script continues rather than aborting (a failed test still
executed its lines). If a decoder submodule isn't built locally, that backend's
native path simply can't be measured on
that machine; `find_library(PHASH_LIBJPEG_LIB ...)` falls back to stb_image silently in
that case, same as any other CMake build here, so check the summary's per-file
breakdown (`lcov --list docs/coverage/cmake/native.info`) rather than assuming the
option being `ON` means the backend was actually linked.

### Coverage standard

Both targets measure lines **and branches**: the paths that lines alone report as
covered while their error branch never ran are where the defects live. CI's
`coverage-cmake` job (Linux x86_64, native decoders) checks the result against
`scripts/coverage_thresholds.txt` with `scripts/check_coverage.py`, and fails when an
area drops below its minimum:

| Area | Lines | Branches |
|---|---|---|
| `src/hashes/` | 95% | 91% |
| `src/image/` | 95% | 86% |
| `src/loaders/` | 92% | 74% |
| `src/core.c` | 96% | 90% |
| `src/batch.c` | 97% | 91% |
| `src/loader.c` | 93% | 77% |
| `src/fileio.c` | 94% | 85% |
| all of `src/` | 95% | 87% |

The thresholds file is the one source of these numbers; the table repeats it. Each
minimum is the coverage measured on that job's toolchain (GCC, lcov) less one or two
points: the threaded batch takes different branches from run to run, and the canonical
runner compiles x86 code that an arm64 machine does not. The same tests give a
different branch figure under clang: the two compilers split compound conditions
(`&&`, `||`, `?:`) into branches differently, so an area's percentage can differ by a
few points either way between them (`src/loader.c`: 78% under GCC, 82% under clang),
and a local macOS run is only an approximation of the gate. Linux-only code (the
cgroup and affinity reading in `src/batch.c`) is counted only on Linux. Functions are
not given a threshold -- one uncovered function out of two hundred is not a quantity
worth steering by.

A line may be left out only when no test can reach it: a defensive check that
validation upstream makes impossible, or a size check that can fail only where
`size_t` is 32 bits. It is marked in the code with `LCOV_EXCL_START -- <reason>` and
`LCOV_EXCL_STOP` around the block, and the reason says why it cannot run. Everything
else that is uncovered is a missing test, not an exclusion.

Coverage is measured on Linux and macOS only. Lines under `#ifdef _WIN32` (the CRT file
calls, the Win32 thread pool) are not in any number; they are compiled and tested by
CI's Windows build, which measures no coverage.

Locally: `make coverage` (stb_image build) or `make coverage-cmake` (native decoders),
then `python3 scripts/check_coverage.py docs/coverage/cmake/native.info` for the gate
itself. Both targets print the per-area table.

### Installed package and `pkg-config`

`cmake --install` writes `libphash.h`, `phash_version.h`, the library, the exported
`phash::phash` CMake package and `libphash.pc`. Both consumer routes are covered by
`scripts/smoke_install.sh static|shared` (also `make install-test`), which installs into
a throwaway prefix, builds a consumer through `find_package(phash)` and through
`pkg-config`, then **moves the prefix** and repeats the `pkg-config` build from the new
location.

That last step guards relocatability. `libphash.pc.in` writes

```
prefix=@CMAKE_INSTALL_PREFIX@
libdir=${prefix}/@CMAKE_INSTALL_LIBDIR@
includedir=${prefix}/@CMAKE_INSTALL_INCLUDEDIR@
```

so that redefining `prefix` moves everything else with it — `pkg-config --define-prefix`
guesses `prefix` from the `.pc` file's own location, which is how relocatable and
relocated (packaged, then unpacked elsewhere) install trees are consumed. An absolute
`@CMAKE_INSTALL_FULL_LIBDIR@` would be a configure-time path that `--define-prefix`
cannot touch.

Three caveats worth knowing:

- Plain `pkg-config` does **not** redefine the prefix unless asked (`--define-prefix`,
  or a build of pkg-config/pkgconf configured to do it by default, as on Windows). The
  `.pc` makes relocation possible; the consumer still opts into it.
- The behavioural half of the smoke check (move the tree, rebuild) is **not** a
  sufficient regression guard on its own, which is why `smoke_install.sh` also asserts
  on the text of the generated `.pc`. `pkgconf` (3.0.6, what Homebrew installs as
  `pkg-config`) implements `--define-prefix` by string-replacing the old prefix inside
  absolute variable values as well, so it produces correct output even from a `.pc`
  with an absolute `libdir`. freedesktop `pkg-config` only redefines the
  `prefix` variable and leaves an absolute `libdir` stale — the same `.pc` is relocatable
  under one implementation and not the other. Assert on the file, not just the output.
- `GNUInstallDirs` allows `CMAKE_INSTALL_LIBDIR`/`CMAKE_INSTALL_INCLUDEDIR` to be
  absolute paths, and some distribution toolchain files set them that way. Such a value
  cannot be expressed relative to `${prefix}`, so `CMakeLists.txt` emits it verbatim and
  prints a `STATUS` message saying the `.pc` will not be relocatable. That is a property
  of the requested layout, not a bug to paper over.

### Vendoring libphash with `add_subdirectory()`

Besides the installed package, libphash supports being dropped into another project's
source tree:

```cmake
add_subdirectory(third_party/libphash)
target_link_libraries(my_app PRIVATE phash)
```

Both parent configurations are supported and covered end to end (configure → build →
run a real PNG through `ph_load_from_file`) by
`scripts/smoke_add_subdirectory.sh`, which CI runs alongside `smoke_install.sh`:

- `BUILD_SHARED_LIBS=OFF` (static parent),
- `BUILD_SHARED_LIBS=ON` (shared parent).

Two rules keep this working:

- **The parent owns the global build settings.** `BUILD_SHARED_LIBS` and
  `BUILD_TESTING` are global CMake variables, not options of the vendored codecs, yet
  the codecs have to see them `OFF` while they configure. `CMakeLists.txt` therefore
  saves the parent's values, forces its own, and restores them immediately
  (`phash_push/pop_global_build_flags()`). Forcing them without restoring would silently
  turn off a parent's shared build and its `ctest`.
- **Never force a dependency pin into the parent's cache.** libpng's
  `find_package(ZLIB REQUIRED)` has to be steered at the bundled zlib-ng, which is
  done by pre-setting `ZLIB_INCLUDE_DIR`/`ZLIB_LIBRARY` (and pre-creating the
  `ZLIB::ZLIB` alias) — but as **normal, directory-scope variables**. `vendor/libpng`
  is a child scope and inherits them, so nothing has to enter the cache. As `CACHE …
  FORCE` entries they would persist into the *next* configure of the same build tree,
  where libphash's own "did the parent bring its own zlib?" check
  (`DEFINED CACHE{ZLIB_LIBRARY}`) would fire on libphash's own pin: libphash would stand
  aside, the `zlib-ng` target would never be created, and the cached absolute path to its
  archive would stay on libpng's link interface — a file dependency nothing produces
  (`No rule to make target 'phash_build/vendor/zlib-ng/libz.a'`) on every incremental
  build. The smoke script re-configures deliberately to catch that.

`ZLIB_LIBRARY` also holds the zlib-ng **target name**, not a path to `libz.a`: CMake
resolves a target name to the real artifact plus a build-order dependency, whereas the
archive's file name is a guess that is wrong on MSVC and wrong for any build in which
zlib-ng comes out shared. `FindZLIB` never puts that value on a link line here — it is
guarded by `if(NOT ZLIB_LIBRARY)` and only feeds
`find_package_handle_standard_args()`; libpng links `ZLIB::ZLIB`, i.e. the target.

### Formatting
`.clang-format` starts from the LLVM style and sets the rest explicitly, so that a practice
the code follows is held by the gate rather than by habit.
- **Rule**: Run `make format` before every commit; the CI `format-check` job fails on any
  file that differs from what clang-format would print.
- **Layout**: 4-space indent, 100 columns (80 would rewrap a third of the tree for no gain
  in a C API with `ph_` prefixes), `{` on the same line, one statement per line --
  no single-line `if`/loop/`case` bodies.
- **Braces everywhere** (`InsertBraces`): every `if`/`for`/`while` body is a block, so a
  second statement added under an unbraced `if` cannot silently run unconditionally.
- **Preprocessor nesting is visible** (`IndentPPDirectives: AfterHash`): `#    include`
  inside `#if` shows the level of the conditional it belongs to.
- **Tables stay tables**: consecutive `#define`s, trailing comments, short `case` lines
  and the `\` of a multi-line macro are aligned.
- **Includes are ordered by machine** (`IncludeBlocks: Regroup`): the file's own header,
  then the library's headers, then third-party, then system, each group sorted.
- **One spelling**: `const` on the left (`const char *`), `*` next to the name, upper-case
  hex digits (`0xFF`), a trailing comma after the last enumerator (adding one is then a
  one-line diff), `return x;` without parentheses, LF line endings, a newline at the end.
- **Left alone on purpose**: string literals are never split (a message stays greppable),
  and macro bodies are formatted like the rest of the code.
- **Blame across reformatting**: commits that only reformat are listed in
  `.git-blame-ignore-revs`; run `git config blame.ignoreRevsFile .git-blame-ignore-revs`
  once, and `git blame` shows the commit that wrote a line rather than the one that
  reindented it.
- **Version**: clang-format **23**, pinned (`pip install clang-format==23.1.1`).
  Different major versions format the same code differently, so `make format` refuses
  to run with any other major. The pin is raised only in a commit of its own, together
  with whatever reformatting the new version produces.
- **Scope**: `src/`, `include/`, all of `tests/` (including `tests/fuzz/`) and
  `examples/`. `scripts/format.sh` holds both the scope and the version; `make format`
  and the CI `format-check` job both run it (`scripts/format.sh --check` for the CI
  check).
- **Line endings and whitespace**: `.gitattributes` keeps every text file LF on every
  platform and marks image fixtures and fuzz inputs binary; `.editorconfig` gives an
  editor the same indentation, final newline and no trailing whitespace before
  clang-format runs.

## Naming Conventions

- **External linkage ⇒ `ph_`.** Every function and variable of ours that is not `static`
  is named `ph_*`, public or internal: the library shares one C namespace with whatever
  links it. `scripts/check_exported_symbols.sh` fails on any other name (the vendored
  `stbi_*`/`stbir_*` instantiations excepted).
- **`static` ⇒ prefix optional.** A `static` function is invisible outside its file;
  both `ph_*` and unprefixed names are in use, and neither is renamed to match the other.
- **Types**: `ph_*_t` (e.g., `ph_context_t`).
- **Macros and constants**: `PH_*`, upper case with underscores (e.g., `PH_DIGEST_MAX_BYTES`).
- **Include guards**: `PH_<PATH>_H` for the file's path from `include/`, `src/` or
  `tests/src/` (e.g., `PH_HASHES_HASHES_H` for `src/hashes/hashes.h`).
- **Files**: lower case with underscores (e.g., `color_moments.c`).

## Conversions and casts

The library builds with `-Wconversion -Wsign-conversion`: an implicit conversion that can
change a value -- a signed int turning into a huge `size_t`, a `double` narrowing into a
`float` or a byte -- is a warning. A cast is not the way to silence one. The order of
preference:

1. **The right type from the start.** A size, count, offset or loop index over a buffer
   is `size_t`; a value that is naturally small and signed (a pixel, a weight, a clamped
   coordinate) stays `int` and is never mixed with unsigned arithmetic.
2. **One conversion at the boundary.** Dimensions arrive as `int` from the public API and
   the configuration and are validated there. Where such a value enters size or address
   arithmetic, it goes through `ph_size()` (`src/safety.h`) -- once, into a `size_t`
   local -- which names the precondition and checks it in debug builds.
3. **A cast only with its reason.** A cast is right when the code knows something the
   type system does not: a value already range-checked, a narrowing that rounding makes
   exact, a type a third-party API insists on. The reason is stated next to it or is
   evident from the check directly above it. A cast whose only purpose is to make a
   warning go away is a defect: it hides exactly the value the warning is about.

Warnings see implicit conversions, not explicit ones, so two checks watch the casts.
`scripts/check_casts.py` counts the explicit casts in each file under `src/` (the stb
instantiations and `(void)` discards aside) against `scripts/explicit_casts.txt`; any
difference fails `format-check` until the list is updated with
`scripts/check_casts.py --update` in the same change, so an added cast is always in
the diff for review. And clang-tidy's `bugprone-misplaced-widening-cast`, in the
`strict-warnings` job, catches a cast that widens the result of an arithmetic which has
already overflowed in the narrower type, such as `(uint64_t)(a * b)` on two ints.

## Headers and includes

- `include/libphash.h` is the whole public API. Everything under `src/` is internal and
  split by subsystem: `context.h`, `arena.h`, `safety.h`, `bytes.h`, `digest.h`, `fileio.h`,
  `batch.h`, `loader.h`, `image/image.h`, `hashes/hashes.h`, `loaders/backends.h`.
- A file includes the headers whose names it uses, spelled by their path from `src/`
  (`#include "image/image.h"`), never through a sibling's includes and never with `../`.
- The vendored single-file stb headers are the one exception: they are included by their
  path relative to the including file (`#include "../vendor/stb_image.h"`), so that
  `vendor/` is not an include directory and no other vendored header can be picked up
  by a bare name.

## CI matrix (`.github/workflows/ci.yml`)

One job per concern, all triggered on push to `main` or to a `release/**` branch,
and on any pull request targeting either:

| Job | What it checks |
|---|---|
| `format-check` | `scripts/format.sh --check` — `clang-format --dry-run --Werror` with the pinned clang-format 23 over `src/`, `include/`, `tests/`, `examples/`; `scripts/check_docs_coverage.sh`; `scripts/check_final_state_voice.sh`, which fails on tracker ids, paths into local planning notes and release-cycle wording (a feature "since" a version) in tracked text; `shellcheck --severity=warning` over `scripts/*.sh`; `scripts/check_casts.py`, the explicit-cast count per file in `src/` against `scripts/explicit_casts.txt`; and `scripts/check_coverage.py --check-docs`, which keeps the coverage table below equal to `scripts/coverage_thresholds.txt`. Fast, no build, catches these before the slower jobs run. |
| `build-and-test` | Full vendored build (libjpeg-turbo + libpng + libwebp + zlib-ng) across linux-x86_64 (gcc, clang), linux-arm64, macos-arm64. `PHASH_STRICT_DEPS=ON`, so a decoder silently falling back to stb_image is a hard configure failure, not a quiet pass. |
| `strict-warnings` | The full vendored build with `PHASH_WARNINGS_AS_ERRORS=ON` (gcc, clang): any warning in libphash's own sources, tests or benchmark fails it. The only job with `-Werror`, so a newer compiler's new warning never breaks a build from source. Its clang leg also runs clang-tidy's `bugprone-misplaced-widening-cast` over `src/`. |
| `coverage-cmake` | `scripts/coverage_cmake.sh` — line and branch coverage of the vendored decoder build, checked against `scripts/coverage_thresholds.txt` (see "Coverage standard"); the HTML report is published as a downloadable artifact. |
| `minimal-build` | Zero-dependency build (every `PHASH_USE_*` off, stb_image only) on ubuntu-24.04, macos-latest, windows-latest. |
| `c-standard-matrix` | Full test suite under `-DCMAKE_C_STANDARD=11/17/23`, gcc+clang, Linux+macOS (no Windows — see the Toolchains section above for why). |
| `build-and-test-32bit` | The native PNG backend (libpng) built `-m32`, catching `size_t`/`int`-width overflow bugs a 64-bit build can't reach. |
| `benchmark` | Regression gate against the PR's base commit — see the Benchmarks section below. |
| `sanitizers` | ASan+UBSan via CMake, `-fno-sanitize-recover=all` (first report aborts the run). |
| `tsan` | ThreadSanitizer over the threaded batch path (`src/batch.c`) and the "one context per thread" contract. |
| `valgrind` | The allocation-failure test suite (`tests/src/test_alloc_failure.c`) under Valgrind — independent of ASan/LSan, which don't mix with it. |
| `fuzz` | A short (90s) libFuzzer run per PR — a fast regression check, not real corpus exploration; see "Fuzzing" below for the real thing. |
| `install-smoke-test` | `scripts/smoke_install.sh` and `scripts/smoke_add_subdirectory.sh` — both consumer routes (`find_package`, pkg-config, `add_subdirectory()`), both link configurations; `scripts/check_exported_symbols.sh`, which fails unless the shared library exports exactly the functions of `include/libphash.h` (no internal helper, no `stb_image`, no vendored decoder); `scripts/build_examples.sh`, which builds and runs `examples/` against the shared library. |

Every build job starts the same way: a plain `actions/checkout`, then the local
composite action [`.github/actions/setup-build`](../.github/actions/setup-build/action.yml),
which fetches the vendored submodules and installs the job's packages (apt on Linux,
Homebrew on macOS). A job passes only what differs — its package list, or
`submodules: 'false'` for the stb-only jobs that never read `vendor/`. The checkout
itself cannot move into the action, because a local action is read from the working
copy. Configure and build steps stay in the jobs, since their arguments are what
distinguishes one job from another.

Two more workflows, and Dependabot, run on their own schedule rather than per push:

- **`.github/workflows/fuzz-nightly.yml`** — a 30-minute libFuzzer run, sharing the
  same growing corpus cache across every night and every PR's short run. See
  "Fuzzing" below.
- **`.github/workflows/stb-freshness-check.yml`** — monthly, checks whether the two
  copied-in stb headers (`vendor/stb_image.h`, `vendor/stb_image_resize2.h`) have
  drifted from upstream and opens a tracking issue if so. See `SECURITY.md`'s
  "Vendored dependencies" section.
- **`.github/dependabot.yml`** — weekly PRs bumping the five vendored decoder
  submodules and the GitHub Actions themselves.

### Running CI on a branch

A release branch validates itself: `ci.yml` triggers on push to `release/**`, so
every merge into one runs the full matrix. Runs are free here — the repository is
public and uses standard runners, so every leg bills zero job-minutes.

For a branch that is *not* `main` or `release/**`, open a draft pull request against
`release/**`. For a `pull_request` event GitHub takes the workflow definition from the
merge ref, so this works even when the branch is the only place the workflow change
exists.

`workflow_dispatch` ("Run workflow" in the Actions UI) is the one route that does not
work from an arbitrary branch: GitHub registers it from the copy of the workflow file
on the **default branch**, and ignores a declaration that exists only elsewhere. The
same applies to `schedule`, Dependabot, and issue/PR templates — all of them are read
from the default branch only.

## Testing Strategy

### 1. Unit Tests (`tests/src/test_*.c`)
Each module should have a corresponding test file. We use a simple `test_macros.h` for assertions.

The PNG and WebP decoder fixtures in `tests/data/png/` — one PNG per pixel format a decoder
converts (gray at 1, 4 and 8 bits, RGB at 8 and 16 bits, palette with and without `tRNS`,
RGBA) and broken files that fail at the header, in truncated image data and on a bad
checksum — are generated by `python3 scripts/gen_png_fixtures.py` (standard library only).
Their pixels follow formulas that `tests/src/test_png_variants.c` recomputes. The files are
committed; regenerating them with another zlib may change the compressed bytes, never the
pixels.

**Golden hashes.** `tests/src/test_golden_hashes.c` compares every algorithm on every
fixture, exactly, with `tests/data/golden_hashes.<jpeg>.txt` — one file per JPEG decoder
(`libjpegturbo`, `stbjpeg`), because the two round their IDCT differently; nothing else
in a build changes a value, so there is no tolerance and no per-platform file. A build
without a WebP decoder skips the WebP fixtures. Regenerate only after a deliberate change
to an algorithm's output, both files, from builds that decode WebP:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target test_golden_hashes
./build/test_golden_hashes --update
cmake -B build-stb -DPHASH_USE_LIBJPEG_TURBO=OFF && cmake --build build-stb --target test_golden_hashes
./build-stb/test_golden_hashes --update
```

`--update` writes the file only when every fixture and algorithm succeeded. A difference
on one platform alone is not a reason to regenerate: it means that platform computes
differently, which is the defect to find.

### 2. Stability Tests (`tests/src/test_stability.c`)
A colour load and a grayscale load of the same file must hash identically wherever both
reach gray through the library's own weights (every PNG decoder, WebP, JPEG through
stb_image). JPEG through libjpeg-turbo returns the luma it decoded from YCbCr, so there the
bound is 3 bits.

### 3. Fuzzing (`tests/fuzz/fuzz_load.c`)

A libFuzzer harness over `ph_decode_buffer()` — the same single entry point every
format-decoding path funnels through (see `docs/architecture.md`). Build it with
`-DPHASH_BUILD_FUZZERS=ON`; this is a configure-time error under GCC, since libFuzzer
needs compiler-rt, which only Clang ships:

```bash
cmake -B build-fuzz -DPHASH_BUILD_FUZZERS=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang
cmake --build build-fuzz --target fuzz_load -j
./build-fuzz/fuzz_load -max_total_time=60 tests/fuzz/corpus/
```

CI runs this two ways: a 90-second smoke run on every PR (the `fuzz` job above, meant
to catch a fast regression, not explore the input space) and a 30-minute run nightly
(`fuzz-nightly.yml`) against a corpus that persists and grows across both. Report a
crash found this way through `SECURITY.md`'s reporting channel if it looks like a real
memory-safety issue, not a public issue.

### 4. Benchmarks (`tests/src/bench_hash.c`)

Used for performance regression testing. It is a measuring tool, not a test: it asserts
nothing, and `make test`/`ctest` do not run it. `make benchmark` builds and runs it;
run it directly with:

```bash
./bench_hash hash tests/data/photo.jpeg 100    # every algorithm, on a loaded image
./bench_hash load tests/data/photo.jpeg 100    # decode only, grayscale and RGB
./bench_hash load tests/data/photo.jpeg 100 3  # the same at PH_DECODE_SCALE_EIGHTH
./bench_hash full tests/data/photo.jpeg 100    # decode + pHash
./bench_hash --json smoke                      # fixed CI configuration
```

`smoke` times one decode per format — `photo.jpeg` requested as grayscale and as RGB (two
decoder paths), `photo_complex.png`, and `photo.webp` when the build has libwebp — then
every algorithm on the loaded `photo.jpeg`. Its JSON carries a `schema` number that says
what the metrics measure; it changes whenever a metric keeps its name but starts timing
different work, so two runs are comparable only when their schemas match.

#### Measurement methodology

Every mode warms up before measuring and then times each iteration separately,
reporting `min_ms`, `median_ms`, `p90_ms` and `avg_ms`:

- **Warmup** is `max(3, iterations/10)` discarded iterations. For the decode
  modes this also pulls the file into the page cache, which is deliberate: the
  numbers are meant to describe decode cost, and disk latency would only add
  variance. It follows that these benchmarks do **not** measure cold-cache I/O.
- **`min_ms` is the number to compare across builds.** It is the best available
  estimate of how fast the code can run with OS noise removed. `median_ms` shows
  the typical case; `p90_ms` shows how noisy the machine was during the run — a
  `p90_ms` far above `median_ms` means the environment, not the code, changed.
- **A hashing row is the first hash computed on a loaded image**, the cost a caller pays
  after a load: the grayscale conversion and the area-sum grid shared by aHash, pHash,
  wHash and BMH are included. The context caches both until the image changes, so the
  benchmark drops them before every iteration, outside the timed region; timing
  repeated hashes on one context would measure only the work left once they exist.
- **`avg_ms` should not be used for comparisons.** It is a mean over the whole
  loop, so a single scheduler preemption shifts it by tens of percent. It is
  kept in the JSON only for schema compatibility with older baselines.

#### Measured noise floor

The point of the above is that a benchmark number is only useful if its
run-to-run spread is smaller than the regression it is supposed to detect.
Measured on an idle arm64 macOS machine by running
`scripts/bench_regression_gate.sh` with the **same binary as both sides** — a
comparison whose true answer is 0% for every metric:

| Comparison metric | Gate runs | False regressions at 25% | Max observed deviation |
|---|---|---|---|
| `avg_ms`, 50 iterations | 5 | **3 metrics in 1 run** | **45.3%** |
| `min_ms`, 200 iterations (the gate's configuration) | 7 | 0 | 6.7% (typically under 4%) |

`avg_ms` cannot tell a real regression from runner noise; `min_ms` over 200 iterations
can.

The gate's default threshold is therefore **10%**: comfortably above the
measured floor, still far below the cost of an accidental extra decode pass.
The gate runs warning-only (`STRICT=0`) on CI, because a shared runner's floor
is higher than measured here.

#### Running the gate

The CI `benchmark` job builds two trees in the same job and runs
`scripts/bench_regression_gate.sh` on them, so runner noise lands on both sides. Which
commit is the base depends on the event:

| Event | Base |
|---|---|
| pull request | the PR's base commit |
| push to `main` or `release/**` | the previous tip of the pushed branch: everything the push brought in is compared at once |
| manual run (*Run workflow*) | the `bench_base_ref` input — a branch, tag or commit, `main` by default |

A push that creates the branch has no previous tip, and the job says so in a notice
instead of comparing.

The report lists every metric either side measured. Two rules flag a regression: one
metric more than 10% slower, or the median change over all metrics both sides have more
than 5% slower — an even slowdown of the whole pipeline that keeps each metric under 10%.
The median is unmoved by one noisy metric, so its threshold can sit lower. A metric on
one side only is shown as *new* or *gone* and is not a regression. The *spread* column
is how far apart the runs of the noisier side landed, `(max - min) / median` of their
`min_ms`; a flag on a metric with a spread near the change is worth a rerun before a
search for the cause. When the two sides report different `schema` numbers the report
says so and compares nothing. GitHub offers the manual run only for workflows declared on the
default branch.

Locally, build both versions and pass the two binaries, the one under test first:

```bash
scripts/bench_regression_gate.sh build/bench_hash ../base/build/bench_hash 5 10 report.md
```

### 5. Sanitizers (ASan + UBSan)

```bash
make debug        # rebuilds with -O0 -g -fsanitize=address,undefined
                  # NOTE: this cleans and rebuilds; it does NOT run the tests
make test         # ...so always run the suite afterwards
```

CI runs the same pair through CMake in the `sanitizers` job, with
`-fno-sanitize-recover=all` so the first report aborts the test.

**Known vendor exemption — `-fno-sanitize=alignment` on `src/image/stb_resize_impl.c`.**
The vendored `vendor/stb_image_resize2.h` packs its filter coefficients with
deliberately unaligned 64-bit moves: `STBIR_MOVE_2` in `stbir__pack_coefficients`
casts a `float*` to `stbir_uint64*`, and the coefficient stride is frequently odd
(`coeffs += coefficient_width`, `pc += 7`), so every other move lands on a
4-mod-8 address. The buffer is stb's *own* internal bump allocation — 16-byte
aligned at its base — and the buffers we hand to `stbir_resize*` are always
16-byte aligned, so nothing about our call sites is at fault. The pattern is
present verbatim in current upstream master (v2.18), i.e. a version bump does not
help. Untreated it produces 5 `runtime error: load/store of misaligned address
... for type 'stbir_uint64'` per test-suite run, which is exactly the kind of
constant noise that lets a real finding of ours slip through.

The vendored implementation therefore lives in its own translation unit,
`src/image/stb_resize_impl.c`, which contains nothing but the
`#define STB_IMAGE_RESIZE_IMPLEMENTATION` / `#include` pair, and *only that file*
is compiled with `-fno-sanitize=alignment` (`STB_NOSAN_CFLAGS` in the `Makefile`,
`set_source_files_properties(...)` in `CMakeLists.txt`). Because the exempt TU has
no code of ours in it, alignment violations in `libphash` itself are still
reported normally — as are all other UBSan checks, including in that file.

A runtime `UBSAN_OPTIONS=suppressions=...` file does not work here: the suppression is
silently ignored under `-fno-sanitize-recover=all`, so the report still fires and the
process still aborts.

If you add another file that includes a vendored header with known UB, prefer the
same shape (isolated TU + narrowest possible `-fno-sanitize=<check>`) over a
blanket suppression, and document it here.

**Known vendor patch — OOM handling in `vendor/stb_image_resize2.h`.**
Under ASan/UBSan (and generally whenever `assert()`-style debug allocators are
in play) `stbir__alloc_internal_mem_and_build_samplers()` switches to
`STBIR__SEPARATE_ALLOCATIONS`, where every internal buffer is `malloc()`'d one
at a time instead of via one merged block. Upstream's out-of-memory handling
in that mode has three independent bugs, each patched in place in the vendored
header with a `/* libphash local patch (not upstream): ... */` comment
explaining the reasoning at the point of the change (search the file for that
marker — there are five call sites):

- `stbir__info` (and, one level down, its `split_info` array and each split's
  `ring_buffers` pointer array) is raw, unzeroed memory right after its own
  allocation. If a *later* allocation in the same call fails,
  `stbir__free_internal_mem()` walks these structures by count
  (`info->splits`, `info->alloc_ring_buffer_num_entries`) and frees whatever
  garbage pointers it finds in not-yet-populated fields — a segfault. Fixed by
  zeroing each of these blocks immediately after its own successful
  allocation, so an unpopulated field is a real `NULL` the free path can skip.
- `stbir__free_internal_mem()` itself indexes into a split's `ring_buffers`
  array without checking whether that array's own allocation succeeded,
  dereferencing a null `float**` when it did not. Fixed by guarding that loop.
- A handful of intermediate buffers (`vertical`'s gather/prescatter
  contributors and coefficients, `horizontal`/`vertical` contributors and
  coefficients, and a small internal 15-byte sentinel block used only to keep
  the two-pass allocation loop's bookkeeping truthy) are recorded onto the
  persistent `info` struct — the thing `stbir__free_internal_mem()` actually
  knows how to free — only long after they are allocated. If a *subsequent*
  allocation fails in between, the block is unreachable from `info` and
  leaks. Fixed by mirroring each of these onto `info` immediately after its
  own successful allocation, instead of waiting for the later bulk copy.

The allocation-failure harness (`tests/src/alloc_shim.h` +
`tests/src/test_alloc_failure.c`), which fails a chosen allocation ordinal and checks
for crashes/leaks, exercises all five of its scenarios, including under sanitizers;
without the patch two of them crash or leak.

This diverges from upstream `stb_image_resize2` (present verbatim in current
upstream master) and **must be re-applied and re-verified against
`test_alloc_failure` under `make debug && make test`** on the next bump of
this vendored file — a version bump alone will silently drop the patch and
reopen the crash/leak.

**Known vendor patch — the reason reported for an allocation failure in
`vendor/stb_image.h`.** `src/loader.c` classifies a failed `stbi_load*()` by
comparing `stbi_failure_reason()` against string literals from the vendored
header (`ph_stb_oom_reasons[]`, `ph_stb_unsupported_reasons[]`). Upstream loses
that reason in two independent places, so a decode that failed purely because
`malloc()` returned NULL is reported to the caller as a verdict on the file:

- The three `stbi_zlib_decode_*malloc*` entry points return NULL when their own
  initial `stbi__malloc()` fails **without calling `stbi__err()` at all**, while
  the PNG caller assumes they did (`return 0; // zlib should set error`).
  `stbi_failure_reason()` then still holds whatever unrelated string ran last —
  in our own call sequence, `"no SOI"`, left by the JPEG probe inside the
  `stbi_info_from_memory()` that `ph_decode_stb_mem()` runs first to read the
  dimensions. An out-of-memory PNG arrives as `PH_ERR_CORRUPT_DATA`. Fixed by
  setting `"outofmem"` there, through a `stbi__errpc()` spelled exactly like the
  file's existing `stbi__errpuc()`/`stbi__errpf()`.
- The format probes that allocate (`stbi__jpeg_test`, `stbi__jpeg_info`,
  `stbi__gif_info_raw`) report an allocation failure by returning 0 — the same
  value that means "this is not my format". `stbi__load_main()`/
  `stbi__info_main()` cannot tell the two apart, move on to the next format, and
  the dispatch's own `"unknown image type"` overwrites `"outofmem"`; a perfectly
  valid JPEG arrives as `PH_ERR_UNSUPPORTED_FORMAT`. Fixed with a thread-local
  `stbi__g_probe_outofmem` flag, set by those three probes, cleared at the top of
  each dispatch and read only where the dispatch is about to give up — the
  success path is untouched.

Each change carries the same `/* libphash local patch (not upstream): ... */`
marker as the resize patch above (search the file for it). Without the patch, 5 of
`test_alloc_failure`'s 83 failure points misreport. As with the resize patch, **a bump
of this vendored file must re-apply and re-verify it** against `test_alloc_failure`, and
record the new pair of hashes in `THIRD-PARTY-NOTICES.md`. `src/loader.c` classifies a
decode failure by comparing against error strings that live inside `stb_image.h`, so a
bump also has to be reviewed against `ph_stb_unsupported_reasons[]` and
`test_stb_failure_classification`.
See SECURITY.md's "Vendored dependencies" section for how this project tracks the two
copied-in stb headers against upstream.

## Adding New Features

1.  **Header**: Add the public signature to `include/libphash.h`.
2.  **Implementation**: 
    - Add hash algorithms to `src/hashes/`.
    - Add image processing kernels to `src/image/`.
    - Add new decoders to `src/loaders/`.
3.  **Build**: `Makefile` and `CMakeLists.txt` are configured to detect new files in these directories automatically.
4.  **Documentation**: Update `docs/algorithms.md` or `docs/architecture.md` and the function comments in the header (Doxygen style).
