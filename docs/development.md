# Development Guide

This guide outlines the standards and procedures for contributing to `libphash`.

## Build Environment

### Toolchains
The project supports `gcc`, `clang`, and `msvc`. The `Makefile` allows compiler overrides:
```bash
make CC=clang
```

### Build Configurations (CMake)
The C engine utilizes modular CMake flags allowing deterministic inclusions of external decoders:
- `PHASH_USE_TURBOJPEG` (ON/OFF): Configures bundled `libjpeg-turbo`.
- `PHASH_USE_LIBPNG` (ON/OFF): Configures bundled `libpng`.
- `PHASH_USE_WEBP` (ON/OFF): Toggles `libwebp` external submodules.
- `PHASH_USE_SPNG` (ON/OFF): Configures `spng` integration.

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

## Adding New Features

1.  **Header**: Add the public signature to `include/libphash.h`.
2.  **Implementation**: 
    - Add hash algorithms to `src/hashes/`.
    - Add image processing kernels to `src/image/`.
    - Add new decoders to `src/loaders/`.
3.  **Build**: `Makefile` and `CMakeLists.txt` are configured to detect new files in these directories automatically.
4.  **Documentation**: Update `docs/algorithms.md` or `docs/architecture.md` and the function comments in the header (Doxygen style).
