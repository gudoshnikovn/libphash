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

### 3. Benchmarks (`tests/test_benchmark.c`)
Used for performance regression testing. Run with:
```bash
./test_benchmark hash tests/photo.jpeg 100
```

## Adding New Features

1.  **Header**: Add the public signature to `include/libphash.h`.
2.  **Implementation**: 
    - Add hash algorithms to `src/hashes/`.
    - Add image processing kernels to `src/image/`.
    - Add new decoders to `src/loaders/`.
3.  **Build**: `Makefile` and `CMakeLists.txt` are configured to detect new files in these directories automatically.
4.  **Documentation**: Update `docs/algorithms.md` or `docs/architecture.md` and the function comments in the header (Doxygen style).
