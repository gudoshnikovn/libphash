# libphash

A high-performance, portable C library for Perceptual Hashing. Designed for image similarity detection with zero external dependencies (except for the included `stb_image`).

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## Language Bindings

Official and community-supported wrappers:
- **Python**: [python-libphash](https://github.com/gudoshnikovn/python-libphash) (`pip install python-libphash`)

---

## Features

- **Comprehensive Algorithm Suite**:
  - `aHash` (Average Hash): Fast, based on average pixel intensity.
  - `dHash` (Difference Hash): Extremely fast, resistant to aspect ratio changes.
  - `pHash` (Perceptual Hash): High precision, uses optimized Discrete Cosine Transform (DCT).
  - `mHash` (Median Hash): Robust against non-linear image adjustments.
  - `bmh` (Block Mean Hash): Divides image into blocks for localized analysis.
  - `wHash` (Wavelet Hash): Frequency-based hashing using Wavelet transform (if implemented).
- **High Performance**: 
  - Internal **Bilinear Interpolation** for high-quality image scaling.
  - **Lazy-loading** grayscale cache to avoid redundant conversions.
  - Pre-computed trigonometric tables for DCT.
- **FFI-Friendly**: Clean C API with opaque pointers, optimized for Python (ctypes/cffi), Rust, or Node.js.
- **Thread-Safe**: No global state (optimized one-time internal initialization).
- **Cross-Platform**: Compatible with GCC, Clang, and MSVC.



## Architecture

The library follows a strict separation between public API and internal implementation:
- `include/libphash.h`: Public interface and error codes.
- `src/internal.h`: Internal structures and image processing helpers.
- `src/hashes/`: Core hash algorithm implementations.

## Building

To build the static library and run tests, you only need `make` and a C compiler:

```bash
# Build the library (libphash.a)
make

# Run all tests
make test

# Clean build artifacts
make clean

```

## Usage Example (C)

```c
#include <libphash.h>
#include <stdio.h>

int main() {
    ph_context_t *ctx = NULL;
    uint64_t hash1, hash2;

    ph_create(&ctx);
    
    // Load and compute pHash for first image
    ph_load_from_file(ctx, "image1.jpg");
    ph_compute_phash(ctx, &hash1);
    
    // Load and compute pHash for second image
    ph_load_from_file(ctx, "image2.jpg");
    ph_compute_phash(ctx, &hash2);

    // Calculate similarity
    int distance = ph_hamming_distance(hash1, hash2);
    printf("Hamming Distance: %d\n", distance);

    if (distance < 5) {
        printf("Images are very similar!\n");
    }

    ph_free(ctx);
    return 0;
}

```

## FFI Integration Notes

* **Opaque Pointer**: `ph_context_t` is an opaque struct. In high-level languages, treat it as a `void*` or `uintptr_t`.
* **Memory Management**: Always call `ph_free()` to release image data and context memory allocated on the C heap.
* **Error Handling**: Functions return `ph_error_t` (int). `0` (PH_SUCCESS) indicates success.

## License

This project is licensed under the MIT License - see the LICENSE file for details.
Includes `stb_image` by Sean Barrett (Public Domain/MIT).

