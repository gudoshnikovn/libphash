# Development Guide

This guide outlines the standards and procedures for contributing to `libphash`.

## Build Environment

### Toolchains
The project supports `gcc`, `clang`, and `msvc`. The `Makefile` allows compiler overrides:
```bash
make CC=clang
```

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
| Bundled TurboJPEG | `PHASH_USE_TURBOJPEG=ON` | *n/a* (stb only) | Makefile has no native JPEG path |
| Bundled libpng | `PHASH_USE_LIBPNG=ON` | *n/a* (stb only) | mutually exclusive with `PHASH_USE_SPNG` |
| spng instead of libpng | `PHASH_USE_SPNG=OFF` | *n/a* | raw `-D` flag, not an `option()` |
| libwebp | `PHASH_USE_WEBP=ON` | `USE_WEBP=0` | Makefile path expects a system libwebp |
| zlib-ng instead of system zlib | `PHASH_USE_ZLIB_NG=ON` | *n/a* | |
| **Batch thread pool** | `PHASH_ENABLE_THREADS=ON` | `PHASH_ENABLE_THREADS=1` | **matched in R15**; was `0` in the Makefile |
| Shared library | `PHASH_BUILD_SHARED=OFF` | *n/a* (static `libphash.a` only) | |
| Tests | `PHASH_BUILD_TESTS=ON` | always built by `all` | |
| `-march=native` | `PHASH_OPTIMIZE_NATIVE=OFF` | *n/a* (fixed `-msse4.2` / `-march=armv8-a+simd`) | |
| libFuzzer harnesses | `PHASH_BUILD_FUZZERS=OFF` | *n/a* | requires Clang |
| Test-only mock decoder | `PHASH_ENABLE_MOCK_BACKEND=OFF` | `PHASH_ENABLE_MOCK_BACKEND=0` | must never be on in a shipped build |
| Strict dependency handling | `PHASH_STRICT_DEPS=OFF` | *n/a* | |

Both spellings of the thread switch accept the same off-ramp:

```bash
make PHASH_ENABLE_THREADS=0                  # portable build without -pthread
cmake -S . -B build -DPHASH_ENABLE_THREADS=OFF
```

**Why the thread default was changed (R15/L13).** The Makefile used to default to
`PHASH_ENABLE_THREADS=0` "to keep the portable build free of pthread linkage". The
practical effect was not a leaner build but a dead code path: `src/batch.c`'s worker
pool was compiled out of every local `make test` and every `make coverage` run, so the
threaded half of `ph_hash_files()`/`ph_hash_buffers()` was never executed or measured
locally — which is how the Windows thread-pool defect (H1) survived. The default is now
`1`, i.e. `-pthread` *is* a dependency of the portable build. That is a deliberate
trade: a pthread implementation is present on every platform the Makefile targets
(it uses `uname` and POSIX tooling throughout), and correctness coverage of the
concurrent path is worth more than the dependency.

### Instrumented build modes (Makefile)

`debug` and `coverage` are switch-driven (`PHASH_SANITIZE=1`, `PHASH_COVERAGE=1`) and
re-invoke `make` rather than listing `clean` as a sibling prerequisite. The old
`debug: clean all` shape raced under `-jN`: `clean` deleted object files while other
jobs were compiling them. Prefer the same shape for any future instrumented mode.

### Installed package and `pkg-config`

`cmake --install` writes `libphash.h`, `phash_version.h`, the library, the exported
`phash::phash` CMake package and `libphash.pc`. Both consumer routes are covered by
`scripts/smoke_install.sh static|shared` (also `make install-test`), which installs into
a throwaway prefix, builds a consumer through `find_package(phash)` and through
`pkg-config`, then **moves the prefix** and repeats the `pkg-config` build from the new
location.

That last step is the regression guard for R15/L11. `libphash.pc.in` writes

```
prefix=@CMAKE_INSTALL_PREFIX@
libdir=${prefix}/@CMAKE_INSTALL_LIBDIR@
includedir=${prefix}/@CMAKE_INSTALL_INCLUDEDIR@
```

so that redefining `prefix` moves everything else with it — `pkg-config --define-prefix`
guesses `prefix` from the `.pc` file's own location, which is how relocatable and
relocated (packaged, then unpacked elsewhere) install trees are consumed. It previously
substituted `@CMAKE_INSTALL_FULL_LIBDIR@`, an absolute configure-time path that
`--define-prefix` cannot touch.

Three caveats worth knowing:

- Plain `pkg-config` does **not** redefine the prefix unless asked (`--define-prefix`,
  or a build of pkg-config/pkgconf configured to do it by default, as on Windows). The
  fix makes relocation *possible*; the consumer still opts into it.
- The behavioural half of the smoke check (move the tree, rebuild) is **not** a
  sufficient regression guard on its own, which is why `smoke_install.sh` also asserts
  on the text of the generated `.pc`. `pkgconf` (3.0.6, what Homebrew installs as
  `pkg-config`) implements `--define-prefix` by string-replacing the old prefix inside
  absolute variable values as well, so it produces correct output even from the broken
  `@CMAKE_INSTALL_FULL_LIBDIR@` form. freedesktop `pkg-config` only redefines the
  `prefix` variable and leaves an absolute `libdir` stale — the same `.pc` is relocatable
  under one implementation and not the other. Assert on the file, not just the output.
- `GNUInstallDirs` allows `CMAKE_INSTALL_LIBDIR`/`CMAKE_INSTALL_INCLUDEDIR` to be
  absolute paths, and some distribution toolchain files set them that way. Such a value
  cannot be expressed relative to `${prefix}`, so `CMakeLists.txt` emits it verbatim and
  prints a `STATUS` message saying the `.pc` will not be relocatable. That is a property
  of the requested layout, not a bug to paper over.

### Formatting
We use `clang-format` with a custom style (based on LLVM with minor tweaks).
- **Indentation**: 4 spaces.
- **Rule**: Run `make format` before every commit.

## Naming Conventions

- **Public APIs**: Prefix with `ph_` (e.g., `ph_compute_ahash`).
- **Internal Helper Functions**: Standard C naming, not exposed in `libphash.h`.
- **Types**: Suffix with `_t` (e.g., `ph_context_t`).
- **Files**: Lowercase with underscores (e.g., `color_hsv.c`).

## Testing Strategy

### 1. Unit Tests (`tests/test_*.c`)
Each module should have a corresponding test file. We use a simple `test_macros.h` for assertions.

### 2. Stability Tests (`tests/test_stability.c`)
Ensures that different loading modes (RGB vs Grayscale) and different architectures (NEON vs Scalar) produce bit-exact or near-exact results.

### 3. Benchmarks (`tests/src/test_benchmark.c`)

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
| `avg_ms`, 50 iterations (before) | 5 | **3 metrics in 1 run** | **45.3%** |
| `min_ms`, 200 iterations (now) | 7 | 0 | 6.7% (typically under 4%) |

The old configuration could not tell a real regression from runner noise, so
flipping `STRICT=1` on it would have failed pull requests at random.

The gate's default threshold is therefore **10%**: comfortably above the
measured floor, still far below the cost of an accidental extra decode pass.
On a shared CI runner the floor is higher than measured here, which is why
`STRICT=0` (warning-only) stays in place until the signal has been observed
across several real pull requests.

### 4. Sanitizers (ASan + UBSan)

```bash
make debug        # rebuilds with -O0 -g -fsanitize=address,undefined
                  # NOTE: this target is `clean all` — it does NOT run the tests
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
help. Untreated it produced 5 `runtime error: load/store of misaligned address
... for type 'stbir_uint64'` per test-suite run, which is exactly the kind of
constant noise that lets a real finding of ours slip through.

The vendored implementation therefore lives in its own translation unit,
`src/image/stb_resize_impl.c`, which contains nothing but the
`#define STB_IMAGE_RESIZE_IMPLEMENTATION` / `#include` pair, and *only that file*
is compiled with `-fno-sanitize=alignment` (`STB_NOSAN_CFLAGS` in the `Makefile`,
`set_source_files_properties(...)` in `CMakeLists.txt`). Because the exempt TU has
no code of ours in it, alignment violations in `libphash` itself are still
reported normally — as are all other UBSan checks, including in that file.

A runtime `UBSAN_OPTIONS=suppressions=...` file was tried first and rejected: the
suppression is silently ignored under `-fno-sanitize-recover=all`, so the report
still fires and the process still aborts — the CI job would stay red.

If you add another file that includes a vendored header with known UB, prefer the
same shape (isolated TU + narrowest possible `-fno-sanitize=<check>`) over a
blanket suppression, and document it here.

## Adding New Features

1.  **Header**: Add the public signature to `include/libphash.h`.
2.  **Implementation**: 
    - Add hash algorithms to `src/hashes/`.
    - Add image processing kernels to `src/image/`.
    - Add new decoders to `src/loaders/`.
3.  **Build**: `Makefile` and `CMakeLists.txt` are configured to detect new files in these directories automatically.
4.  **Documentation**: Update `docs/algorithms.md` or `docs/architecture.md` and the function comments in the header (Doxygen style).
