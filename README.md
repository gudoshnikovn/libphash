# libphash

A perceptual image hashing library in C: nine algorithms, native SIMD-accelerated
JPEG/PNG/WebP decoders, and a hash of a 400×400 photo in 0.05 ms after a 0.23 ms decode
(numbers under "Performance" below).

- **Try it in two minutes** — download a prebuilt archive and run an example:
  [Try it](#try-it).
- **Build it into a program** — [Building & Installation](#building--installation), then
  [Usage](#usage-example) and the [examples](examples/README.md).
- **Choose an algorithm, or understand one** — [`docs/theory/choosing.md`](docs/theory/choosing.md);
  everything else is indexed in [`docs/`](docs/README.md).

**Upgrading from 1.x?** See [`CHANGELOG.md`](CHANGELOG.md) for what changed and
[`MIGRATION.md`](MIGRATION.md) for what to do about it — 2.0.0 changes some hash values
silently (no error, no warning), most importantly because EXIF auto-orientation is now
on by default.

## What this library is for

**Finding duplicate and near-duplicate images in a collection you control.** Deduplicating
an archive, clustering a catalog, keying a cache, answering "have I stored this before"
inside a trusted pipeline. Every design decision in this library is made for that job.

**It is not built to withstand someone trying to fool it.** Every hash here is
deterministic and unkeyed — the same file and settings give the same value on any
machine (for JPEG, with the same decoder; see
[`docs/theory/comparing.md`](docs/theory/comparing.md#same-hash-on-every-machine)), with no shared
secret — and that property, which is what makes deduplication work at all, is also
what makes the hashes straightforward to attack on purpose. Anyone who benefits from a
wrong answer can construct a visually different image with a matching hash, or perturb an
image so it stops matching its own copy.

So: do not use `libphash` as a content-moderation filter, a copyright blocklist, or an
integrity check on untrusted input. Those are authentication problems, the literature
solves them with **keyed** algorithms whose key is load-bearing rather than optional, and
none is implemented here. Reaching for a neural embedding instead does not close the gap —
published collision attacks cover learned hashes too.

The reasoning, the citations, and what follows from this choice for how the algorithms are
verified are in [`docs/theory/perceptual-hashing.md`](docs/theory/perceptual-hashing.md#what-a-perceptual-hash-is-not)
and [`docs/algorithm-provenance.md`](docs/algorithm-provenance.md).

## Language Bindings

* **Python**: [python-libphash](https://github.com/gudoshnikovn/python-libphash) (`pip install python-libphash`)
* *More bindings (Node.js, Rust, Go) are in development.*

---

## Core Features

* **Multiple Algorithms**: `aHash`, `dHash`, `pHash` (DCT-based), `wHash` (Wavelet), `mHash`, `BMH`, `Radial`, `ColorHash`, and `ColorMoments`. Every one of them is traced to its source in [`docs/references.md`](docs/references.md), and every known divergence from that source is written down in [`docs/algorithm-provenance.md`](docs/algorithm-provenance.md).
* **High-Performance Decoders**: Built-in support for `libjpeg-turbo`, `libpng`, and `libwebp` with SIMD acceleration (NEON/SSE) and `mmap` optimization.
* **Broad Format Fallback**: JPEG/PNG/WebP are decoded by the SIMD-accelerated native backends above; anything else — BMP, GIF, TGA, PSD, HDR, PIC, PNM — falls back to the bundled `stb_image` decoder automatically, no configuration needed. Not covered: TIFF (unsupported by `stb_image`) and animated GIF beyond the first frame (only the first frame is hashed). Animated WebP is rejected outright (not decoded to a frame) when the native WebP backend is in use.
* **Fast Grayscale Loading**: Native decoders can perform grayscale conversion during decompression: 47 ms instead of 54 ms for a 20-megapixel JPEG, and one byte per pixel instead of three.
* **Zero-Fragmentation Arena**: Optimized context-based **Arena Allocator** for internal operations, ensuring predictable performance in high-load environments.
* **Decompression-Bomb Protection**: Images are rejected with `PH_ERR_IMAGE_TOO_LARGE` before any pixel buffer is allocated if they exceed a configurable pixel-count limit (256 Mi = 268,435,456 pixels by default; tune or disable via `ph_context_set_max_pixels()`).
* **Automatic EXIF/WebP Orientation**: on by default — a hash describes what a viewer displays, not the raw sensor buffer. Opt out with `ph_context_set_auto_orient(ctx, 0)` if you need hashes of the stored (unrotated) pixels, e.g. to match hashes stored by 1.x; see [`MIGRATION.md`](MIGRATION.md).
* **Batch API**: `ph_hash_files()`/`ph_hash_buffers()` hash many images across an optional internal thread pool, and `ph_compute_multi()` computes several of the four `uint64_t` algorithms (aHash/dHash/pHash/wHash) in one call sharing the same grayscale conversion.
* **Detailed error codes**: `ph_error_t` distinguishes an unsupported format, corrupt data, an unavailable decoder, an I/O failure, and an oversized image, instead of one generic failure — see `include/libphash.h`.
* **Digest helpers**: `ph_digest_to_hex()`/`ph_digest_from_hex()`/`ph_hash_to_hex()`/`ph_hash_from_hex()` for storing/transmitting hashes as text, `ph_similarity()`/`ph_similarity_digest()` for a normalized [0,1] score alongside the raw distance functions.
* **FFI-Friendly**: Clean C API with opaque pointers, flat structs and fixed-width enums, designed for FFI bindings from any language.
* **Cross-Platform**: Linux (x86-64, arm64, 32-bit x86), macOS arm64 and Windows x86-64, built and tested by CI on every push, with the same hash values on all of them — see [`docs/development.md`](docs/development.md#supported-platforms).

---

## Performance

Time to hash an image that is already loaded, and to decode it, on an Apple M3 Pro (CMake
Release build with the bundled decoders, one thread, minimum of 30–300 runs):

| | 400×400 JPEG | 5472×3648 JPEG (20 Mpx) |
| --- | --- | --- |
| decode | 0.23 ms | 54 ms |
| aHash, pHash, wHash, BMH | 0.05 ms each | 2.6 ms each |
| dHash | 0.05 ms | 6.3 ms |
| ColorHash, ColorMoments | 0.13, 0.24 ms | 17, 30 ms |
| mHash, Radial | 0.92, 0.60 ms | 33, 49 ms |

A hash's cost grows with the image's pixel count, since the reduction to its working size
reads every pixel; for large JPEGs `ph_context_set_decode_scale()` cuts both. The batch API
spreads files over every CPU. The full table and the method are in
[`docs/theory/choosing.md`](docs/theory/choosing.md#cost).

## Build configurations

| Configuration | Decoders | Dependencies | For |
| --- | --- | --- | --- |
| **Full** (CMake default) | `libjpeg-turbo`, `libpng`, `libwebp` | none outside the repository (vendored submodules) | the fastest decoding; what the prebuilt archives contain |
| **Minimal** (Makefile, or CMake without submodules) | `stb_image` | none | the smallest dependency footprint; WebP only through a system libwebp (`make USE_WEBP=1`) |

---

## Try it

Download the archive for your platform from the
[Releases page](https://github.com/gudoshnikovn/libphash/releases) (static build, for
example `libphash-<version>-linux-x86_64.tar.gz`), unpack it, and build the
[`basic_hash.c`](examples/basic_hash.c) example against it:

```bash
tar xf libphash-<version>-<platform>.tar.gz
cc basic_hash.c -o basic_hash \
    $(PKG_CONFIG_PATH=libphash-<version>-<platform>/lib/pkgconfig pkg-config --cflags --libs libphash)
./basic_hash photo.jpg        # prints the pHash, e.g. "pHash: 7eb66192bc394247"
```

[`compare_two_images.c`](examples/compare_two_images.c) prints how far apart two images
are; the other examples are listed in [`examples/README.md`](examples/README.md).

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
for FFI bindings or any consumer that doesn't want to build the vendored
decoders itself. Each archive also carries a build provenance attestation:
`gh attestation verify <archive> --repo gudoshnikovn/libphash` confirms it was
built by this repository's release workflow (see `SECURITY.md`, "Verifying a
release artifact").

**CPU baseline.** The x86-64 archives need SSE4.2 and POPCNT (the x86-64-v2 level: every
x86-64 CPU since 2009–2011); the arm64 archives need ARMv8-A with Advanced SIMD, which
every arm64 CPU has. Nothing requires AVX or AVX2; libjpeg-turbo, libwebp and zlib-ng
detect wider instruction sets at run time and use them where the CPU has them.

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

# Install libphash.a, the headers and libphash.pc (default PREFIX=/usr/local)
make install PREFIX=$HOME/.local
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

Full, compiling versions of this and five more — comparing two images, the digest
algorithms and their metrics, batch hashing, error handling, a CMake consumer — are in
[`examples/`](examples/README.md). CI builds and runs them against the current header in
every run.

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
(`-lphash_jpeg -lpng16 -lwebpdecoder -lz`, or nothing extra for a minimal/stb_image-only
build) — you don't need to track that list by hand. See `MIGRATION.md` if you're
moving a 1.x integration that linked by hand onto either of these.

---

## License & Credits

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for the full text.

### Third-Party Software

`libphash` bundles several high-performance libraries to ensure zero-dependency builds:

* **[libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) (v3.2.0)**: IJG, BSD-3-Clause, zlib.
* **[libpng](https://github.com/pnggroup/libpng) (v1.6.59)**: libpng License 2.0.
* **[libwebp](https://github.com/webmproject/libwebp) (v1.6.0)**: WebP License (BSD 3-Clause).
* **[zlib-ng](https://github.com/zlib-ng/zlib-ng) (v2.3.3)**: zlib License.
* **[stb_image](https://github.com/nothings/stb) (v2.30)**: Public Domain / MIT.
* **[stb_image_resize2](https://github.com/nothings/stb) (v2.18)**: Public Domain / MIT.

For detailed licensing information regarding these components, please refer to [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
