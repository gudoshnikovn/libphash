# libphash

A high-performance, portable C library for Perceptual Image Hashing. 

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## Core Features

- **Multiple Algorithms**: `aHash`, `dHash`, `pHash` (DCT), `wHash` (Wavelet), `mHash`, `BMH`, `Radial`, and `ColorHash`.
- **High-Performance Decoders**: Bundled support for `libjpeg-turbo`, `libpng`, and `spng` with SIMD acceleration (NEON/SSE).
- **Fast Grayscale Loading**: Native decoders can perform grayscale conversion during decompression, saving time and memory.
- **Zero-Allocation Processing**: Uses a context-based scratchpad for internal operations.
- **FFI-Friendly**: Clean C API with opaque pointers, optimized for Python, Rust, or Node.js.
- **Cross-Platform**: Optimized for ARM64 (Apple Silicon, Raspberry Pi) and x86_64.

---

## 🚀 Performance Modes

libphash can be built in two primary configurations:

| Mode | Decoders | Dependencies | Best For |
| :--- | :--- | :--- | :--- |
| **High Performance** (Default) | `libjpeg-turbo`, `libpng`/`spng` | Self-contained (vendor submodules) | Production, massive datasets |
| **Minimal** | `stb_image` (fallback) | Zero | Embedded, quick scripts |

---

## 🛠 Building

### Recommended (CMake)
Recommended for managing bundled high-performance decoders.
```bash
mkdir build && cd build
cmake .. -DPHASH_BUILD_TESTS=ON
make -j$(nproc)
ctest
```

### Portable (Makefile)
Fast and simple for minimal builds.
```bash
# Default build (gcc)
make -j8

# Using specific compiler
make CC=clang test
```

---

## 📖 Documentation

For detailed technical information, see the [**Technical Documentation Index**](docs/README.md).
- [Architecture Overview](docs/architecture.md)
- [Development Guide & Coding Standards](docs/development.md)
- [Algorithmic Deep Dive](docs/algorithms.md)

---

## 💻 Usage Example

```c
#include <libphash.h>
#include <stdio.h>

int main() {
    ph_context_t *ctx = NULL;
    uint64_t hash = 0;

    ph_create(&ctx);
    
    // Enable fast grayscale loading (skips RGB conversion)
    ph_context_set_load_grayscale(ctx, 1);
    
    if (ph_load_from_file(ctx, "photo.jpg") == PH_SUCCESS) {
        ph_compute_phash(ctx, &hash);
        printf("pHash: %016llx\n", (unsigned long long)hash);
    }

    ph_free(ctx);
    return 0;
}
```

## Language Bindings

- **Python**: [python-libphash](https://github.com/gudoshnikovn/python-libphash) (`pip install python-libphash`)

## License

This project is licensed under the MIT License.
Includes `stb_image`, `libjpeg-turbo`, `libpng`, and `spng` in the `vendor/` directory.
