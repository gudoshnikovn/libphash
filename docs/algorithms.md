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

## 5. ColorHash
- **Concept**: Computes statistical moments (mean, variance) across HSV or RGB channels.
- **Use Case**: When image content is similar but colors differ significantly (e.g., product photos).

---

## Comparison Summary

| Algorithm | Speed | Rotation | Noise | Scaling |
|---|---|---|---|---|
| aHash | ⭐⭐⭐⭐⭐ | ❌ | ⭐ | ⭐⭐⭐ |
| dHash | ⭐⭐⭐⭐ | ❌ | ⭐⭐ | ⭐⭐⭐⭐ |
| pHash | ⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| wHash | ⭐⭐ | ⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ |
| Radial | ⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐ |
