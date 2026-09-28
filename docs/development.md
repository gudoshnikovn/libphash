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
  libpng/spng, libwebp, zlib-ng) from `vendor/`, `install()`/`find_package(phash)`
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
| Bundled libpng | `PHASH_USE_LIBPNG=ON` | *n/a* (stb only) | mutually exclusive with `PHASH_USE_SPNG` |
| spng instead of libpng | `PHASH_USE_SPNG=OFF` | *n/a* | mutually exclusive with `PHASH_USE_LIBPNG` (configure-time error if both are on) |
| libwebp | `PHASH_USE_WEBP=ON` | `USE_WEBP=0` | Makefile path expects a system libwebp |
| zlib-ng instead of system zlib | `PHASH_USE_ZLIB_NG=ON` | *n/a* | |
| **Batch thread pool** | `PHASH_ENABLE_THREADS=ON` | `PHASH_ENABLE_THREADS=1` | matches CMake's default |
| Shared library | `PHASH_BUILD_SHARED=OFF` | *n/a* (static `libphash.a` only) | |
| Tests | `PHASH_BUILD_TESTS=ON` | always built by `all` | |
| `-march=native` | `PHASH_OPTIMIZE_NATIVE=OFF` | *n/a* (fixed `-msse4.2` / `-march=armv8-a+simd`) | |
| libFuzzer harnesses | `PHASH_BUILD_FUZZERS=OFF` | *n/a* | requires Clang |
| Test-only mock decoder | `PHASH_ENABLE_MOCK_BACKEND=OFF` | `PHASH_ENABLE_MOCK_BACKEND=0` | must never be on in a shipped build |
| Strict dependency handling | `PHASH_STRICT_DEPS=OFF` | *n/a* | |
| Coverage instrumentation | `PHASH_COVERAGE=OFF` | `PHASH_COVERAGE=0` | same flag name, independent implementations — see below for why one build alone isn't enough |

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
  `#ifdef PH_USE_LIBJPEG_TURBO` / `PH_USE_LIBPNG` / `PH_USE_SPNG` / `PH_USE_WEBP` in
  `src/loaders/{jpeg,png,webp}.c` doesn't exist in that binary at all — those
  backends compile down to nothing but their `ph_can_use_*()` stub. The overall
  percentage this target reports (currently ~95% lines) does **not** include the
  native decoders, no matter how high it reads.
- **`make coverage-cmake`** (`scripts/coverage_cmake.sh`) runs two separate CMake
  `-DPHASH_COVERAGE=ON` + `ctest` passes — one with the default vendored decoder
  set (libjpeg-turbo + libpng + libwebp + zlib-ng, i.e. what CI's `build-and-test` job
  and releases ship), one with `PHASH_USE_SPNG=ON`/`PHASH_USE_LIBPNG=OFF` (the
  alternative PNG backend, mutually exclusive with libpng so it needs its own
  configure) — then merges both `lcov` traces into one report under
  `docs/coverage/cmake/html/index.html`. This is what actually exercises the
  `max_pixels` checks, `png_error_fn`/`png_warning_fn` + the `longjmp` that carries
  libpng's error message out, `spng_strerror()` branches, and the `pitch`/
  `alloc_size` overflow guards in `jpeg.c` — overflow-hardening code that no coverage
  number from the Makefile-only target ever measured.

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
We use `clang-format` with a custom style (based on LLVM with minor tweaks).
- **Indentation**: 4 spaces.
- **Rule**: Run `make format` before every commit.
- **Version**: clang-format **23**, pinned (`pip install clang-format==23.1.1`).
  Different major versions format the same code differently, so `make format` refuses
  to run with any other major. The pin is raised only in a commit of its own, together
  with whatever reformatting the new version produces.
- **Scope**: `src/`, `include/`, all of `tests/` (including `tests/fuzz/`) and
  `examples/`. `scripts/format.sh` holds both the scope and the version; `make format`
  and the CI `format-check` job both run it (`scripts/format.sh --check` for the CI
  check).

## Naming Conventions

- **Public APIs**: Prefix with `ph_` (e.g., `ph_compute_ahash`).
- **Internal Helper Functions**: Standard C naming, not exposed in `libphash.h`.
- **Types**: Suffix with `_t` (e.g., `ph_context_t`).
- **Files**: Lowercase with underscores (e.g., `color_moments.c`).

## CI matrix (`.github/workflows/ci.yml`)

One job per concern, all triggered on push to `main` or to a `release/**` branch,
and on any pull request targeting either:

| Job | What it checks |
|---|---|
| `format-check` | `scripts/format.sh --check` — `clang-format --dry-run --Werror` with the pinned clang-format 23 over `src/`, `include/`, `tests/`, `examples/`; `scripts/check_docs_coverage.sh`; and `scripts/check_final_state_voice.sh`, which fails on tracker ids, paths into local planning notes and release-cycle wording (a feature "since" a version) in tracked text. Fast, no build, catches these before the slower jobs run. |
| `build-and-test` | Full vendored build (libjpeg-turbo + libpng/spng + libwebp + zlib-ng) across linux-x86_64 (gcc, clang, and a spng variant), linux-arm64, macos-arm64. `PHASH_STRICT_DEPS=ON`, so a decoder silently falling back to stb_image is a hard configure failure, not a quiet pass. |
| `coverage-cmake` | `scripts/coverage_cmake.sh` — merged lcov report across the vendored and spng decoder sets; published as a downloadable artifact. |
| `minimal-build` | Zero-dependency build (every `PHASH_USE_*` off, stb_image only) on ubuntu-latest, macos-latest, windows-latest. |
| `c-standard-matrix` | Full test suite under `-DCMAKE_C_STANDARD=11/17/23`, gcc+clang, Linux+macOS (no Windows — see the Toolchains section above for why). |
| `build-and-test-32bit` | The native PNG backend (libpng) built `-m32`, catching `size_t`/`int`-width overflow bugs a 64-bit build can't reach. |
| `benchmark` | Regression gate against the PR's base commit — see the Benchmarks section below. |
| `sanitizers` | ASan+UBSan via CMake, `-fno-sanitize-recover=all` (first report aborts the run). |
| `tsan` | ThreadSanitizer over the threaded batch path (`src/batch.c`) and the "one context per thread" contract. |
| `valgrind` | The allocation-failure test suite (`tests/src/test_alloc_failure.c`) under Valgrind — independent of ASan/LSan, which don't mix with it. |
| `fuzz` | A short (90s) libFuzzer run per PR — a fast regression check, not real corpus exploration; see "Fuzzing" below for the real thing. |
| `install-smoke-test` | `scripts/smoke_install.sh` and `scripts/smoke_add_subdirectory.sh` — both consumer routes (`find_package`, pkg-config, `add_subdirectory()`), both link configurations. |

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

### 2. Stability Tests (`tests/src/test_stability.c`)
Ensures that different loading modes (RGB vs Grayscale) and different architectures (NEON vs Scalar) produce bit-exact or near-exact results.

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

### 4. Benchmarks (`tests/src/test_benchmark.c`)

Used for performance regression testing. Run with:

```bash
./test_benchmark hash tests/data/photo.jpeg 100    # hashing only, on a loaded image
./test_benchmark load tests/data/photo.jpeg 100    # decode only, grayscale and RGB
./test_benchmark full tests/data/photo.jpeg 100    # decode + pHash
./test_benchmark --json smoke                      # fixed CI configuration
```

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
