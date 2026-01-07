# libphash

A high-performance, portable C library for Perceptual Hashing. Designed for image similarity detection with zero external dependencies (except for the included `stb_image`).

## Features

- **Multiple Algorithms**:
  - `aHash` (Average Hash): Fast, based on average intensity.
  - `dHash` (Difference Hash): Fast, resistant to aspect ratio changes.
  - `pHash` (Perceptual Hash): Robust, uses Discrete Cosine Transform (DCT).
- **FFI-Friendly**: Clean C API with opaque pointers, making it easy to wrap in Python (ctypes/cffi), Rust, or Node.js.
- **Thread-Safe**: No global state.
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
    
    ph_load_from_file(ctx, "image1.jpg");
    ph_compute_phash(ctx, &hash1);
    
    ph_load_from_file(ctx, "image2.jpg");
    ph_compute_phash(ctx, &hash2);

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

- **Opaque Pointer**: `ph_context_t` is an opaque struct. In high-level languages, treat it as a `void*` or `uintptr_t`.
- **Memory Management**: Always call `ph_free()` to release image data and context memory allocated on the C heap.
- **Error Handling**: Functions return `ph_error_t` (int). `0` always indicates `PH_SUCCESS`.

## License

This project is licensed under the MIT License - see the LICENSE file for details.
Includes `stb_image` by Sean Barrett (Public Domain/MIT).
