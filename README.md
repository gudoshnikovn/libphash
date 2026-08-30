# libphash

A high-performance, portable C library for Perceptual Image Hashing.

`libphash` is designed for speed and efficiency, providing a robust set of algorithms for image fingerprinting with native, SIMD-accelerated decoders and a zero-fragmentation memory model.

## Language Bindings

* **Python**: [python-libphash](https://github.com/gudoshnikovn/python-libphash) (`pip install python-libphash`)
* *More bindings (Node.js, Rust, Go) are in development.*

---

## Core Features

* **Multiple Algorithms**: `aHash`, `dHash`, `pHash` (DCT-based), `wHash` (Wavelet), `mHash`, `BMH`, `Radial`, and `ColorHash`.
* **High-Performance Decoders**: Built-in support for `libjpeg-turbo`, `libpng`, `spng`, and `libwebp` with SIMD acceleration (NEON/SSE) and `mmap` optimization.
* **Fast Grayscale Loading**: Native decoders can perform grayscale conversion during decompression, significantly reducing CPU cycles and memory overhead.
* **Zero-Fragmentation Arena**: Optimized context-based **Arena Allocator** for internal operations, ensuring predictable performance in high-load environments.
* **FFI-Friendly**: Clean C API with opaque pointers, designed for seamless integration with Python, Rust, Node.js, and Go.
* **Cross-Platform**: Optimized for ARM64 (Apple Silicon, Raspberry Pi) and x86_64.

---

## Performance Modes

`libphash` can be built in two primary configurations:

| Mode | Decoders | Dependencies | Best For |
| --- | --- | --- | --- |
| **High Performance** (Default) | `libjpeg-turbo`, `libpng`/`spng`, `libwebp` | Self-contained (vendor submodules) | Production, massive datasets, server-side processing |
| **Minimal** | `stb_image` (fallback) | Zero | Embedded systems, quick scripts, simple builds |

---

## Building & Installation

### Recommended (CMake)

Best for managing bundled high-performance decoders and system integration.

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install

```

### Portable (Makefile)

Fast and simple for minimal builds or static linking.

```bash
# Build static library
make -j8

# Run tests
make test

```

---

## Usage Example

### C Code

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

### Compiling & Linking

To compile your application with `libphash`:

```bash
gcc main.c -o my_app -lphash

```

---

## License & Credits

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for the full text.

### Third-Party Software

`libphash` bundles several high-performance libraries to ensure zero-dependency builds:

* **libjpeg-turbo (v3.1.4)**: IJG, BSD-3-Clause, zlib.
* **libpng (v1.6.56)**: libpng License 2.0.
* **libwebp (v1.6.0)**: WebP License (BSD 3-Clause).
* **spng (v0.7.4)**: BSD 2-Clause License.
* **zlib-ng (v2.3.3)**: zlib License.
* **stb_image (v2.30)**: Public Domain / MIT.
* **stb_image_resize2 (v2.18)**: Public Domain / MIT.

For detailed licensing information regarding these components, please refer to [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
