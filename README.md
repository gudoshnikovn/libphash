# libphash

A high-performance, portable C library for Perceptual Image Hashing.

`libphash` is designed for speed and efficiency, providing a robust set of algorithms for image fingerprinting with native, SIMD-accelerated decoders and a zero-fragmentation memory model.

**Upgrading from 1.x?** See [`CHANGELOG.md`](CHANGELOG.md) for what changed and
[`MIGRATION.md`](MIGRATION.md) for what to do about it — 2.0.0 changes some hash values
silently (no error, no warning), most importantly because EXIF auto-orientation is now
on by default.

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
* **Broad Format Fallback**: JPEG/PNG/WebP are decoded by the SIMD-accelerated native backends above; anything else — BMP, GIF, TGA, PSD, HDR, PIC, PNM — falls back to the bundled `stb_image` decoder automatically, no configuration needed. Not covered: TIFF (unsupported by `stb_image`) and animated GIF beyond the first frame (only the first frame is hashed). Animated WebP is rejected outright (not decoded to a frame) when the native WebP backend is in use.
* **Fast Grayscale Loading**: Native decoders can perform grayscale conversion during decompression, significantly reducing CPU cycles and memory overhead.
* **Zero-Fragmentation Arena**: Optimized context-based **Arena Allocator** for internal operations, ensuring predictable performance in high-load environments.
* **Decompression-Bomb Protection**: Images are rejected with `PH_ERR_IMAGE_TOO_LARGE` before any pixel buffer is allocated if they exceed a configurable pixel-count limit (256 megapixels by default; tune or disable via `ph_context_set_max_pixels()`).
* **Automatic EXIF/WebP Orientation**: on by default — a hash describes what a viewer displays, not the raw sensor buffer. Opt out with `ph_context_set_auto_orient(ctx, 0)` if you need the old behavior; see [`MIGRATION.md`](MIGRATION.md).
* **Batch API**: `ph_hash_files()`/`ph_hash_buffers()` hash many images across an optional internal thread pool, and `ph_compute_multi()` computes several of the four `uint64_t` algorithms (aHash/dHash/pHash/wHash) in one call sharing the same grayscale conversion.
* **Detailed error codes**: `ph_error_t` distinguishes an unsupported format, corrupt data, an unavailable decoder, an I/O failure, and an oversized image, instead of one generic failure — see `include/libphash.h`.
* **Digest helpers**: `ph_digest_to_hex()`/`ph_digest_from_hex()`/`ph_hash_to_hex()` for storing/transmitting hashes as text, `ph_similarity()`/`ph_similarity_digest()` for a normalized [0,1] score alongside the raw distance functions.
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

### Prebuilt binaries

Each tagged release (`vX.Y.Z`) publishes prebuilt archives on the
[GitHub Releases page](https://github.com/gudoshnikovn/libphash/releases) for
linux-x86_64, linux-arm64, macos-arm64, and windows-x86_64 — both a
static (`libphash-X.Y.Z-<platform>.tar.gz`/`.zip`) and a shared
(`libphash-X.Y.Z-<platform>-shared.tar.gz`/`.zip`) build, each containing
`include/`, `lib/` (plus `LICENSE` and `THIRD-PARTY-NOTICES.md`), and a
`SHA256SUMS.txt` covering every archive in the release. Every archive is
compiled with the full vendored decoder set (`libjpeg-turbo`, `libpng`,
`libwebp`, `zlib-ng`) and smoke-tested against a clean extraction before
publishing — see `.github/workflows/release.yml`. This is the quickest path
for FFI bindings (e.g. `python-libphash`) or any consumer that doesn't want to
build the vendored decoders itself.

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

int main(void) {
    ph_context_t *ctx = NULL;
    uint64_t hash = 0;

    if (ph_create(&ctx) != PH_SUCCESS) {
        fprintf(stderr, "ph_create failed\n");
        return 1;
    }

    // Enable fast grayscale loading (skips RGB conversion)
    ph_context_set_load_grayscale(ctx, 1);

    ph_error_t err = ph_load_from_file(ctx, "photo.jpg");
    if (err == PH_SUCCESS) {
        err = ph_compute_phash(ctx, &hash);
        if (err == PH_SUCCESS)
            printf("pHash: %016llx\n", (unsigned long long)hash);
        else
            fprintf(stderr, "hash failed: %s\n", ph_get_error_string(err));
    } else {
        fprintf(stderr, "load failed: %s\n", ph_get_error_string(err));
    }

    ph_free(ctx);
    return 0;
}
```

Full, compiling versions of this and a two-image comparison example are in
[`examples/`](examples/) — they're built and run in CI, so they're guaranteed to still
work with the current header.

### Compiling & Linking

`libphash` links several backend libraries when built with the default vendored
decoder set, so a plain `-lphash` is not enough on its own. Use whichever of these
matches how you built/installed it:

```bash
# pkg-config (works for either build system, after `make install`/`cmake --install`)
cc main.c $(pkg-config --cflags --libs libphash) -o my_app
```

```cmake
# CMake, after `find_package`-able install (see "Recommended (CMake)" above)
find_package(phash 2 REQUIRED)
target_link_libraries(my_app PRIVATE phash::phash)
```

Both forms pull in whatever the installed build was actually configured with
(`-lturbojpeg -lpng16 -lwebp -lz`, or nothing extra for a minimal/stb_image-only
build) — you don't need to track that list by hand. See `MIGRATION.md` if you're
moving a 1.x integration that linked by hand onto either of these.

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
