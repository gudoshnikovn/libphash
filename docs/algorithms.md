# Algorithmic Deep Dive

This document explains the mathematical foundations and tuning parameters of the hashes implemented in `libphash`.

## 1. aHash (Average Hash)
- **Concept**: Downscale to 8x8, convert to grayscale, compute average luminance, and set bits based on whether a pixel is above/below the average.
- **Strength**: High speed, very simple.
- **Weakness**: Sensitive to brightness/contrast shifts.

## 2. pHash (Perceptual Hash)
- **Concept**: Downscale to 32x32, perform a Discrete Cosine Transform (DCT), and use the top-left 8x8 coefficients.
- **Tuning**: 
    - `phash_dct_size`: Default 32. Larger sizes capture more detail but are slower.
    - `phash_reduction_size`: Default 8. Determines final hash length (8x8=64 bits).
- **Strength**: Highly robust against scaling, rotation (< 5°), and moderate compression.

## 3. dHash (Difference Hash)
- **Concept**: Compares adjacent pixels horizontally or vertically.
- **Strength**: Faster than pHash, better at detecting duplicates than aHash.

## 4. wHash (Wavelet Hash)
- **Concept**: Uses Discrete Wavelet Transform (Haar) to analyze image in frequency and spatial domains simultaneously.
- **Modes**:
    - `PH_WHASH_FAST`: Single-level decomposition.
    - `PH_WHASH_FULL`: Multi-level decomposition (more accurate, slower).

## 5. mHash (Marr Hash)
- **Concept**: Configurable block-based logical grid hash utilizing `ph_context_set_block_params`.
- **Strength**: Strikes a robust balance between structural integrity and high comparison speed.

## 6. BMH (Block Mean Hash)
- **Concept**: Divides the image into blocks (default 16x16 or configurable) and computes the mean luminance for each, yielding a highly detailed 256-bit digest (`ph_digest_t`).
- **Use Case**: Need significantly higher entropy and low collision rates compared to standard 64-bit bounds.

## 7. ColorHash & Color Moments Hash
- **Concept**: 
  - `ColorHash` (`ph_compute_color_hash`): Compresses color distribution into a 64-bit numerical representation limit. 
  - `Color Moments` (`ph_compute_color_moments_hash`): Comprehensive statistical digest capturing invariant spatial color statistics.
- **Use Case**: Detecting identical geometric shapes layered with diverse color grading (e.g., recolored product photography).

## 8. Radial Hash
- **Concept**: Rotationally invariant spatial sampling that captures the distribution of variance along angular projections.
- **Tuning**:
    - `radial_projections`: Number of angular slices (default 40).
    - `radial_samples`: Number of radial samples per projection (default 128).
- **Strength**: Unmatched robustness against rotation (up to 360°) and flipping.
- **Use Case**: Applications where image orientation is unpredictable (e.g., user-uploaded photos, satellite imagery).

## Comparison Summary

| Algorithm | Speed | Rotation | Noise | Scaling | Output Format |
|---|---|---|---|---|---|
| aHash | ⭐⭐⭐⭐⭐ | ❌ | ⭐ | ⭐⭐⭐ | 64-bit |
| dHash | ⭐⭐⭐⭐⭐ | ❌ | ⭐⭐ | ⭐⭐⭐⭐ | 64-bit |
| pHash | ⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | 64-bit |
| mHash | ⭐⭐⭐⭐ | ⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ | 64-bit |
| wHash | ⭐⭐ | ⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ | 64-bit |
| Radial| ⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐ | Digest |
| BMH   | ⭐⭐⭐ | ⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ | Digest (256-bit) |
