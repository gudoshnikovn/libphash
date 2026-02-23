# Architecture Overview

`libphash` is a high-performance C library designed for perceptual image hashing. It prioritizes speed, minimal dependencies, and architectural flexibility.

## Core Components

### 1. Loader Subsystem (`src/loader.c`)
Handles image decoding from files and memory buffers.
- **Backends**: Supports `libjpeg-turbo`, `libpng`, `spng`, and fallback to `stb_image`.
- **Zero-Copy**: Uses `mmap` where possible to minimize memory overhead.
- **Fast Grayscale**: Decoders are configured to perform grayscale conversion during decompression if requested, avoiding a separate RGB-to-gray pass.

### 2. Image Processing Kernels (`src/image.c`)
Low-level primitives for image manipulation:
- **Resizing**: Area sampling (box filter) for downscaling, bilinear interpolation for upscaling.
- **Grayscale**: SIMD-accelerated (NEON/SSE) color conversion using ITU-R BT.601 coefficients.
- **Filtering**: 3x3 Gaussian blur and Laplacian sharpening.
- **Gamma Correction**: LUT-based gamma normalization (2.2).

### 3. Hash Algorithms (`src/hashes/`)
Divided into specific implementations:
- `ahash.c`: Average Hash (frequency-based).
- `phash.c`: DCT-based hash (robust against moderate scaling/rotation).
- `dhash.c`: Gradient-based hash (extremely fast).
- `whash.c`: Wavelet-based (Haar) hash (multi-resolution).
- `radial.c`: Rotationally invariant hash.
- `color_moments.c`: Statistical color distribution.

## Data Flow

```mermaid
graph TD
    File[Image File/Memory] --> Loader[Loader Subsystem]
    Loader -->|Raw Pixels| Context[ph_context_t]
    Context -->|Downscale/Blur/Gamma| Preproc[Pre-processing]
    Preproc -->|Grayscale Buffer| Algos[Hash Algorithms]
    Algos -->|64-bit/Digest| Output[Resulting Hash]
```

## Key Structures

### `ph_context_t`
The central object containing:
- Loaded pixel data.
- Metadata (width, height, channels).
- Pre-computed LUTs (Gamma).
- Configurable parameters (DCT size, block size, weights).
- Scratchpad memory for zero-allocation processing.
