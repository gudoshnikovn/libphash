# aHash — Average Hash

Shrink the image to 8×8, and record for each of the 64 cells whether it is brighter or
darker than the average. The simplest of the nine algorithms, and the cheapest: it shows
the whole idea of perceptual hashing in two lines of arithmetic.

| | |
|---|---|
| **Call** | [`ph_compute_ahash()`](../api/hash64.md#ph_compute_ahash), or [`PH_HASH_AHASH`](../api/hash64.md#PH_HASH_AHASH) in [`ph_compute_multi()`](../api/hash64.md#ph_compute_multi) |
| **Output** | 64-bit hash, compared with [`ph_hamming_distance()`](../api/compare.md#ph_hamming_distance) |
| **Source** | Neal Krawetz, "Looks Like It", 2011 ([provenance](../algorithm-provenance.md#1-ahash--average-hash)) |
| **Cost** | 0.05 ms on a 400×400 image, 2.6 ms on 20 Mpx, after decoding |

## The steps

Every picture on this page is computed by the library from the same photograph, and the
hash at the end is what `ph_compute_ahash()` returns for it.

![The four stages of aHash on the example photograph](../assets/generated/ahash/pipeline.light.svg#only-light)
![The four stages of aHash on the example photograph](../assets/generated/ahash/pipeline.dark.svg#only-dark)

**1. Grayscale.** Each pixel becomes one luminance value,

$$
Y = \left\lfloor \frac{38R + 75G + 15B}{128} \right\rfloor ,
$$

an integer approximation of the ITU-R BT.601 weights. [`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights)
changes them.

**2. Reduce to 8×8.** The image is divided into an 8×8 grid of equal areas, regardless of
its aspect ratio, and each cell becomes the mean of the pixels it covers, computed exactly
and rounded to the nearest integer. Nothing is sampled or skipped: every pixel of the image
counts towards exactly the cells it overlaps, in proportion to the overlap.

![The 8×8 grid with its values](../assets/generated/ahash/grid.light.svg#only-light){ width="420" }
![The 8×8 grid with its values](../assets/generated/ahash/grid.dark.svg#only-dark){ width="420" }

**3. Compare with the mean.** With the 64 values $p_0 \dots p_{63}$ read left to right and
top to bottom, and their mean $\bar p$, each cell contributes one bit:

$$
b_i = \begin{cases} 1 & p_i \ge \bar p \\ 0 & p_i < \bar p \end{cases}
$$

The comparison is exact — $64\,p_i \ge \sum_j p_j$ in integers — so a pixel only ties with
the mean when it is equal to it, and a tie sets the bit.

![The 64 bits](../assets/generated/ahash/bits.light.svg#only-light){ width="420" }
![The 64 bits](../assets/generated/ahash/bits.dark.svg#only-dark){ width="420" }

**4. Pack the bits.** The first cell goes into the most significant bit:

$$
h = \sum_{i=0}^{63} b_i \cdot 2^{\,63-i} .
$$

## Bit layout

Reading the hash in hexadecimal reads the grid row by row: the first two hex digits are
the top row, the last two the bottom row.

![Which bit each cell sets](../assets/generated/ahash/bit-order.light.svg#only-light){ width="420" }
![Which bit each cell sets](../assets/generated/ahash/bit-order.dark.svg#only-dark){ width="420" }

## What changes the hash

The bits record which side of the mean each cell is on, so an edit that keeps that order
keeps the hash, and an edit that moves content between cells does not.

![Bits that differ from the original under eight transforms](../assets/generated/ahash/robustness.light.svg#only-light)
![Bits that differ from the original under eight transforms](../assets/generated/ahash/robustness.dark.svg#only-dark)

- **Recompression, resizing, noise and blur** leave the 8×8 averages where they were: the
  hash does not move until the image is blurred to a smear.
- **Brightness and contrast** scale every cell and the mean together. The order is kept,
  and the hash with it, until values start to clip at white.
- **Rotation and cropping** move content from one cell to another, and the hash follows
  them: a 5° turn or a 10 % crop already moves a handful of bits. aHash is not the
  algorithm for collections where images are rotated or reframed.

These are measurements on one photograph. How the nine algorithms compare over a whole
corpus is on [choosing an algorithm](../algorithms.md#comparison-summary).

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/ahash/robustness-table.md"

## Settings that affect it

aHash has no parameters of its own: the grid is always 8×8. Four context settings change
the pixels it reads.

| Setting | Effect on aHash |
|---|---|
| [`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) | the grayscale formula of step 1 |
| [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale) | a native decoder converts to grayscale itself, which can move a value by one level and, rarely, a bit |
| [`ph_context_set_auto_orient()`](../api/loading.md#ph_context_set_auto_orient) | on by default: the hash describes the image as displayed, after its EXIF rotation |
| [`ph_context_set_alpha_mode()`](../api/loading.md#ph_context_set_alpha_mode) | how transparent pixels are composited before grayscale |

## In code

Hashing two images and counting the bits their hashes differ in, for aHash and the other
three 64-bit algorithms at once:

```c title="examples/hash_distance.c"
--8<-- "examples/hash_distance.c"
```

## Where it comes from

aHash was described by Neal Krawetz in a blog post in 2011; there is no paper. The post
fixes the 8×8 reduction, the grayscale step, the mean and the bit order, and leaves three
things open, which this implementation pins: the resampling filter (an exact area
average), the grayscale weights (BT.601) and the rule for a pixel equal to the mean (the
bit is set). With those, the implementation follows the post exactly, bit order included.
The comparison, with the numbers behind each choice, is in
[provenance § 1](../algorithm-provenance.md#1-ahash--average-hash).
