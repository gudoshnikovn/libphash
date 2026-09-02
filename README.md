# libphash

A high-performance, portable C library for Perceptual Image Hashing.

`libphash` is designed for speed and efficiency, providing a robust set of algorithms for image fingerprinting with native, SIMD-accelerated decoders and a zero-fragmentation memory model.

## What this library is for

**Finding duplicate and near-duplicate images in a collection you control.** Deduplicating
an archive, clustering a catalogue, keying a cache, answering "have I stored this before"
inside a trusted pipeline. Every design decision in this library is made for that job.

**It is not built to withstand someone trying to fool it.** Every hash here is
deterministic and unkeyed — the same image gives the same value on any machine, with no
shared secret — and that property, which is what makes deduplication work at all, is also
what makes the hashes straightforward to attack on purpose. Anyone who benefits from a
wrong answer can construct a visually different image with a matching hash, or perturb an
image so it stops matching its own copy.

So: do not use `libphash` as a content-moderation filter, a copyright blocklist, or an
integrity check on untrusted input. Those are authentication problems, the literature
solves them with **keyed** algorithms whose key is load-bearing rather than optional, and
none is implemented here. Reaching for a neural embedding instead does not close the gap —
published collision attacks cover learned hashes too.

The reasoning, the citations, and what follows from this choice for how the algorithms are
verified are in [`docs/algorithms.md`](docs/algorithms.md#threat-model-what-these-hashes-are-not)
and [`docs/algorithm-provenance.md`](docs/algorithm-provenance.md).

## Language Bindings

* **Python**: [python-libphash](https://github.com/gudoshnikovn/python-libphash) (`pip install python-libphash`)
* *More bindings (Node.js, Rust, Go) are in development.*

---

## Core Features

* **Multiple Algorithms**: `aHash`, `dHash`, `pHash` (DCT-based), `wHash` (Wavelet), `mHash`, `BMH`, `Radial`, `ColorHash`, and `ColorMoments`. Every one of them is traced to its source in [`docs/references.md`](docs/references.md), and every known divergence from that source is written down in [`docs/algorithm-provenance.md`](docs/algorithm-provenance.md).
* **High-Performance Decoders**: Built-in support for `libjpeg-turbo`, `libpng`, `spng`, and `libwebp` with SIMD acceleration (NEON/SSE) and `mmap` optimization.
* **Broad Format Fallback**: JPEG/PNG/WebP are decoded by the SIMD-accelerated native backends above; anything else — BMP, GIF, TGA, PSD, HDR, PIC, PNM — falls back to the bundled `stb_image` decoder automatically, no configuration needed. Not covered: TIFF (unsupported by `stb_image`) and animated GIF/WebP beyond the first frame (only the first frame is hashed).
* **Fast Grayscale Loading**: Native decoders can perform grayscale conversion during decompression, significantly reducing CPU cycles and memory overhead.
* **Zero-Fragmentation Arena**: Optimized context-based **Arena Allocator** for internal operations, ensuring predictable performance in high-load environments.
* **Decompression-Bomb Protection**: Images are rejected with `PH_ERR_IMAGE_TOO_LARGE` before any pixel buffer is allocated if they exceed a configurable pixel-count limit (256 megapixels by default; tune or disable via `ph_context_set_max_pixels()`).
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

* **libjpeg-turbo (v3.2.0)**: IJG, BSD-3-Clause, zlib.
* **libpng (v1.6.58)**: libpng License 2.0.
* **libwebp (v1.6.0)**: WebP License (BSD 3-Clause).
* **spng (v0.7.4)**: BSD 2-Clause License.
* **zlib-ng (v2.3.3)**: zlib License.
* **stb_image (v2.30)**: Public Domain / MIT.
* **stb_image_resize2 (v2.18)**: Public Domain / MIT.

For detailed licensing information regarding these components, please refer to [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
