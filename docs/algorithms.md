# Algorithmic Deep Dive

What each hash in `libphash` computes, what it is good for, and how to tune it.

Two companion documents carry the parts this one deliberately does not:

- **[`references.md`](references.md)** — the sources. Full citations, links, and how far
  each one can be trusted.
- **[`algorithm-provenance.md`](algorithm-provenance.md)** — the analysis. What each
  source specifies, what this code does, and every place the two differ, classified as a
  defect, a deliberate choice, or something the source leaves open.
- **[`methodology.md`](methodology.md)** — the verification methodology this project
  works to: what counts as a defect and how the properties are measured.

Where an algorithm below is known to depart from its source, this page says so and links
there rather than quietly describing the behavior as if it were intended.

## Threat model: what these hashes are not

Every hash here is **deterministic and unkeyed**. The same file, hashed with the same
context settings, gives the same value on every operating system, CPU architecture and
compiler the library builds on (see "Same hash on every machine" below for the one
exception), with no shared secret — which is exactly what deduplication needs, and
exactly what makes all of them trivial to defeat on purpose.

An attacker who wants two visually different images to collide, or one image to stop
matching its own copy, can arrange it. This is not a weakness of any particular algorithm
in this library: it follows from being deterministic and public, and it has been
demonstrated against traditional and neural hashes alike (see [DC20] in
[`references.md`](references.md), which produces exact collisions between unrelated images
under minimal perturbation and notes that an attacker can thereby poison a duplicate-image
lookup table). Swapping in a neural embedding does not fix it either — those are not
trained for adversarial robustness, and adversarial examples are that family's oldest
known failure mode.

**Use these hashes for:** finding duplicates and near-duplicates in a collection you
control, clustering, cache keys, "have I seen this before" in a trusted pipeline.

**Do not use them for:** anything where someone benefits from a wrong answer — copyright
enforcement, content moderation, blocklists, authentication of an image's integrity. The
academic literature has algorithms for that problem, and they look different: they are
**keyed**, so that an attacker who cannot guess the key cannot aim at the hash. Venkatesan
et al. 2000 is the canonical example, and its key is not an optional extra — the paper
calls its randomized rounding "the crucial source of randomness in the hash function's
output". No such algorithm is implemented here, and the honest reason is that a keyed hash
solves a different problem from the one this library is for.

If you need a filter in an adversarial setting, the usual shape is two stages: a fast
deterministic hash like these to reduce a corpus to a candidate set, then a heavier and
harder-to-steer comparison over those candidates. The first stage is what this library is
good at; the second is not in scope.

## Same hash on every machine

A hash is a function of the decoded pixels and the context settings, and the library
computes it the same way everywhere: no fused multiply-add contraction, one plain loop
for pHash's DCT, integer area averaging and grayscale conversion, exact histogram
intersection. A build for arm64 or x86-64, with GCC, Clang or MSVC, with or without SIMD,
gives the same bits; `tests/src/test_golden_hashes.c` holds every algorithm to that
exactly, with no tolerance.

The one thing that changes the pixels is **the JPEG decoder**. libjpeg-turbo (the bundled
decoder in the CMake build) and stb_image (the zero-dependency fallback) round their
inverse DCT differently, so the same JPEG reaches the hash functions as slightly
different pixels. `ph_get_build_info()` names the decoder (`jpeg=libjpeg-turbo` or
`jpeg=stb`). PNG and WebP decode to the same pixels in every build.

For a hash database this means:

- Hashes from builds with the same JPEG decoder can be compared by equality.
- Across the two JPEG decoders, compare by distance with a threshold, never by equality.
  On the test fixtures the 64-bit hashes (aHash, dHash, pHash, wHash) and BMH come out
  the same; mHash differs by 3–4 of 576 bits, and Radial, ColorHash and ColorMoments
  by small amounts in a few of their features.
- Settings that change the pixels a hash sees — `decode_scale`, gamma, the grayscale
  weights, auto-orientation — are part of a hash's identity just like the algorithm's
  own parameters. Store them with the hashes.

## Attribution at a glance

| Algorithm | Author | Source | Known to diverge |
|---|---|---|---|
| aHash | Neal Krawetz | blog post, 2011 | no |
| dHash | David Oftedal, described by Neal Krawetz | blog post, 2013 | no |
| pHash | pHash project; documented by Zauner; coefficient rule from Coskun & Sankur | thesis, 2010 | no — follows the reference implementation |
| wHash | this library, after ImageHash | **none** — see below | n/a — justified by measurement |
| mHash | pHash (construction); Marr & Hildreth 1980 (operator) | implementation + paper | no |
| BMH | Yang, Gu & Niu | paper, 2006 | no |
| Radial | De Roover, De Vleeschouwer, Lefèbvre & Macq | paper, 2005 | no |
| ColorHash | Swain & Ballard (method); this library (quantization) | paper, 1991 — **not read** | n/a — no conformance claimed |
| ColorMoments | Stricker & Orengo | paper, 1995 | **yes** — color space (RGB, not HSV) |

One cross-cutting caveat: no source specifies a resampling filter. aHash, pHash, wHash and
BMH reduce by an exact area average (the mean of the source area behind each output
pixel), dHash through stb_image_resize2's Mitchell filter; neither is the filter ImageHash
uses, and nothing in the sources is violated.

One of the nine — wHash — has no primary source. For those, "correct" can only mean measured
robustness, discrimination and separability — never conformance to a specification,
because there is none.

## Constants no source defines

Nine algorithms and none of their primary sources specify a grayscale formula, and most
leave the tie-break and the bit layout unstated too. Where a source is silent, this
library still has to pick *something*. Each choice below is deliberate and is repeated in
the delta table of the algorithm(s) it touches in `docs/algorithm-provenance.md`.

**Grayscale coefficients.** `PH_GRAY_R/G/B` = 38/75/15 over 128 (`src/image/image.h`), an
integer approximation of the **ITU-R BT.601** luma coefficients (0.299/0.587/0.114) —
cited as an external standard, not because any source here asks for it. The closer
77/150/29-over-256 approximation separates no better: the same on photographs, a little
worse for aHash, wHash, BMH and mHash on synthetic images
([measured](theory/preparation.md#why-38-75-and-15)), so 38/75/15 is used. Used by every
algorithm that reduces to grayscale: aHash, dHash, pHash, wHash, mHash, BMH, Radial. ColorHash and ColorMoments work in color and never call
this path.

**Alpha.** None of the sources hash transparent images; they describe what a picture
looks like, and a transparent pixel looks like whatever is behind it. The color stored
under alpha 0 is invisible and arbitrary — one encoder writes black, another white — so
by default an image with alpha (an alpha channel or a PNG `tRNS` chunk, from any decoder,
or RGBA given to `ph_load_from_pixels()`) is composited onto mid-gray at load time,
`(c·a + 128·(255 − a) + 127) / 255` per channel, and every algorithm sees the visible
image only: two copies differing only in the color under alpha 0 hash identically, where
with alpha dropped they land as far apart as unrelated images. Each background costs a
different algorithm: on white wHash, on black BMH, on gray aHash and ColorMoments; gray is
the one on which neither median hash collapses
([measured](theory/preparation.md#which-background)). `ph_context_set_alpha_mode()` chooses
white, black, or `PH_ALPHA_IGNORE` — hash the stored color whatever its alpha, which is
what ImageHash and PIL's `convert("L")` do.

**Tie-break at the threshold (`value == threshold`).** Two different rules are in force,
and unifying them would mean breaking one of two things that already have a stronger
answer than "pick a convention":

| Algorithm(s) | Rule | Why |
|---|---|---|
| BMH | `>=` | Matches Zauner's equation 3.9, the one place among all nine sources that states a direction. |
| aHash | `>=` | Unpinned by its source ("above or below" leaves it unstated) and no reference implementation is cited to defer to — `>=`, to agree with BMH, the only source that states a direction. |
| pHash, wHash (`ph_median_bitpack()`/`ph_median_bitpack_from()`, shared) | `>` | Each is pinned to its own reference implementation's code instead: pHash's `ph_dct_imagehash()` and ImageHash's `whash()` both use `>`. Overriding a real reference implementation to chase a uniform convention would be the wrong kind of consistency. |
| mHash | `>` | pHash's `ph_mh_imagehash()` construction; no inequality direction is stated in either source, so this is a choice, not a conformance claim. |

**Bit order.** Never affects Hamming distance — everything here is internally
consistent — but it decides what the hash looks like in hex, which matters for
portability and comparison against a foreign implementation.

| Algorithm | Order | Source says |
|---|---|---|
| aHash | MSB first, `1ULL << (63 - i)`, row-major | Krawetz states this exact order ("left to right, top to bottom using big-endian") — the only one of the nine where the source actually specifies a layout. |
| dHash | MSB first, `1ULL << (63 - i)`, row-major | Same source, same statement. |
| pHash | LSB first, `1ULL << i`, row-major DCT block order | Undefined by Zauner or Krawetz; a choice, not verified against pHash's own code. |
| wHash | LSB first, `1ULL << i`, row-major low band | Undefined; no primary source to check against, and not verified against ImageHash's own layout either. |
| mHash | MSB first within each byte, windows in raster order | Undefined by either source; matches how pHash's own page describes packing the result, so kept for that reason alone. |
| BMH | LSB first within each byte, blocks in raster order | The paper defines a bit *sequence* (equation 3.9), not a byte layout — undefined, recorded as a choice. |
| ColorHash | not bit-packed — 108 one-byte histogram bins | n/a |
| ColorMoments | not bit-packed — 18-byte signed 16-bit fixed-point vector | n/a |
| Radial | not bit-packed — 40-byte min-max-quantized DCT vector | n/a |

---

## 1. aHash (Average Hash)

- **Call**: `ph_compute_ahash()`.
- **Concept**: downscale to 8×8, convert to grayscale, compute the mean luminance, set
  one bit per pixel for above/below the mean.
- **Output**: 64-bit.
- **Strength**: among the cheapest here, and very good at finding a known image again.
- **Weakness**: rotation and cropping, which move content between the 8×8 cells.
  Brightness and contrast keep the cells' order and barely move it; gamma moves the mean
  against the cells, by a few bits at its strongest, more than wHash's median
  ([measured](theory/ahash.md#what-changes-the-hash)).
- **Conformance**: follows its source, including the bit order.

## 2. dHash (Difference Hash)

- **Call**: `ph_compute_dhash()`.
- **Concept**: downscale to 9×8 and compare each pixel with its right-hand neighbor,
  giving 8 differences per row over 8 rows.
- **Output**: 64-bit.
- **Strength**: as cheap as aHash on small images, and on photographs it separates
  copies from different images more clearly than aHash
  ([measured](theory/dhash.md#copies-and-different-images)).
- **Weakness**: rotation and cropping, more steeply than aHash; and pairs of nearly equal
  neighbors, which small edits tip, so copies sit a few bits further away.
- **Conformance**: follows its source exactly, including the direction of the comparison
  (`1` means the left pixel is darker than the right).

## 3. pHash (DCT-based)

- **Call**: `ph_compute_phash()`.
- **Concept**: downscale to 32×32, take the two-dimensional type-II DCT, keep the
  low-frequency 8×8 block, and threshold against its median raised by 0.1 % of the AC
  coefficients' range. The margin keeps coefficients crowding the median — common on
  images with little low-frequency structure — from deciding bits by noise: false matches
  at 95 % recall on 800 photographs 14.2 % → 6.8 %, and the values differ from pHash's
  own `ph_dct_imagehash()`.
- **Output**: 64-bit.
- **Tuning**: `ph_context_set_phash_params(dct_size, reduction_size)` — `dct_size`
  `reduction_size`..32, default 32, which is also the maximum (a smaller DCT costs the
  same, since the pass over the image dominates, and separates a little worse);
  `reduction_size` 4–8, default 8 (giving 8×8 = 64 bits). Below 4 the hash is
  degenerate: over 400 photographs, `reduction_size` 3 gives 70 distinct hashes and 2
  gives 4, against 304 at 4 and 350 at 8. On photographs each smaller block separates
  copies from different images worse ([measured](theory/phash.md#parameters)).
- **Strength**: robust to scaling, compression, blur and noise on photographs, and on
  them it separates copies from different images more clearly than the other 64-bit
  hashes ([measured](theory/phash.md#copies-and-different-images)).
- **Weakness**: rotation and cropping, at least as steeply as aHash; and images whose
  low frequencies hold little, such as regular patterns and near-flat fields, where a
  few coefficients decide the hash and small edits move many bits.
- **The DC coefficient**: DCT(0,0) is thresholded like the other 63 but takes no part in
  choosing the threshold, which is what pHash's `ph_dct_imagehash()` does. Since DC is
  above that threshold for any ordinary image, its bit is always 1 and the hash is
  effectively 63 bits wide, as in pHash. Excluding DC from the median changes no bit
  except on an exact tie: a median is not dragged by an outlier. See
  [`algorithm-provenance.md`](algorithm-provenance.md) §3, which also records why the 8×8
  block is not at DCT(1,1) despite both written descriptions saying so.

## 4. wHash (Wavelet Hash)

- **Call**: `ph_compute_whash()`.
- **Concept**: Haar wavelet decomposition down to an 8×8 low-frequency (LL) band,
  thresholded against its median. The LL band is the image averaged over an 8×8 grid,
  scaled, so the hash comes close to aHash's grid thresholded at its median instead of
  its mean ([measured](theory/whash.md#what-the-wavelet-adds)).
- **Output**: 64-bit.
- **Modes** (`ph_context_set_whash_mode()`):
  - `PH_WHASH_FAST` (default) — a fixed 16×16 scale, one decomposition level.
  - `PH_WHASH_FULL` — scale chosen as the largest power of two fitting the image,
    cascaded down to 8×8, as the reference implementation does. About three times
    slower on a large image, for the same hash on most photographs and a few bits apart
    on the rest.
- `ph_context_set_whash_remove_max_haar_ll()` — see the ImageHash-compatibility note below.
- **Strength**: robust to scaling, compression, blur, noise and brightness on
  photographs, as aHash is, and every hash has half of its bits set
  ([measured](theory/whash.md#what-changes-the-hash)).
- **Weakness**: rotation and cropping; and images with uniform areas, whose equal values
  tie at the median.
- **No primary source, deliberately.** wHash has no paper, and it is *not* the ICIP 2000
  algorithm of Venkatesan et al. that is often cited for wavelet hashing — that one is
  keyed, and its key is not optional. No paper describes an unkeyed deterministic wavelet
  hash because there is nothing for the security literature to prove about one. So wHash
  is justified by measurement instead: separability 4.10 on the synthetic corpus, third
  best of the nine, behind only BMH and aHash.
- ImageHash zeroes the coarsest LL band by default (`remove_max_haar_ll`). Under a median
  threshold that operation only subtracts the image mean and cannot change a bit except by
  rounding a tie, so it is off by default here. `ph_context_set_whash_remove_max_haar_ll()`
  enables it for callers mirroring ImageHash's configuration.

## 5. mHash (Marr–Hildreth)

- **Call**: `ph_compute_mhash()`.
- **Concept**: normalize to 512×512, equalize the histogram, correlate with a
  Laplacian-of-Gaussian kernel (the Mexican hat of Marr & Hildreth 1980), sum the response
  over 16×16 blocks into a 31×31 grid, and emit nine bits per 3×3 window of that grid,
  each thresholded against its window's mean.
- **Output**: `ph_digest_t`, 72 bytes (576 bits). Compare with
  `ph_hamming_distance_digest()`.
- **Tuning**: `ph_context_set_mhash_params(alpha, level, size)` — `alpha` and `level` set
  the kernel's scale (pHash's own two parameters), `size` the normalization preset. The
  defaults are 2, 1 and 512, and are the reference implementation's; across 24
  combinations nothing reliably beats them. `alpha` > 1, `level` ≥ 0, with the kernel
  (`2 · 4 · alpha^level + 1` on a side) at most 65; `size` 62–4096, the cost roughly
  quadratic in it.
- **Strength**: 576 bits, so different images gather tightly around half of them, and
  edits that keep the order of the gray levels (brightness, contrast, gamma) move few,
  since the image is equalized first.
- **Weakness**: it records where fine detail lies to within a few pixels, so a turn of a
  degree or two or a crop of a few percent moves it nearly as far as an unrelated image,
  and a small local edit moves it no more than a rescale; it separates copies from
  different images least well of the bit hashes. It is also among the most expensive —
  13 to 20 times aHash (see "Cost" below). Measured on
  [the mHash page](theory/mhash.md#what-changes-the-hash).

## 6. BMH (Block Mean Hash)

- **Call**: `ph_compute_bmh()`.
- **Concept**: divide the image into a grid of blocks, take the mean of each, and
  threshold the block values.
- **Output**: `ph_digest_t`, `block_size²` bits — 256 bits at the default 16×16.
- **Tuning**: `block_size` via `ph_context_set_block_params`, 2..32 (32×32 bits is the
  largest grid that fits a digest; the lower bound is 2, since a single block's mean
  equals itself and can't threshold against a median).
- **Use case**: a finer layout of light and dark than the 64-bit hashes describe. The
  256 bits separate copies from different images by a wider *d′* than aHash's 64, but
  rotation and cropping move more of them, and at the threshold that accepts 95 % of the
  copies the 8×8 grid lets fewer different photographs through
  ([measured](theory/bmh.md#block_size)).
- **Threshold**: the **median** of the block means, as the paper specifies, which is what
  makes the bit distribution balanced by construction. This puts the library at odds with
  OpenCV's `BlockMeanHash`, which thresholds on the mean (in a variable it calls `median`);
  expect BMH values to differ from OpenCV's. See
  [`algorithm-provenance.md`](algorithm-provenance.md) §6.

## 7. ColorHash and ColorMoments

Both need color: they return `PH_ERR_REQUIRES_COLOR` on a grayscale image.

- **ColorHash** (`ph_compute_color_hash`) — a color histogram: every pixel is counted
  into one of 108 bins of the opponent color space (red–green × blue–yellow × light–dark,
  6 × 6 × 3), and two of them are compared with **`ph_histogram_intersection()`**, not with
  a bit or vector metric. A color histogram with histogram intersection, after Swain &
  Ballard (1991), implemented from secondary descriptions of the paper, so no conformance
  to it is claimed; the quantization is this library's, chosen by measurement over sixteen
  candidates. Separability on the test corpus: 4.01.
  **Blind spots**, both inherent to a histogram and both asserted in the tests: it ignores
  where the colors are, so a 90° rotation does not move it at all and neither does
  shuffling the pixels; and flat colors that share a chroma bin and an intensity third —
  black against dark gray, light gray against white — are indistinguishable. The
  converse: neutral gray is the corner of four chroma bins, so a tint of one level moves
  near-gray pixels to another bin, and an image with large neutral areas can score
  against its tinted copy as low as against a different image
  ([measured](theory/color-hash.md#gray-on-a-corner)).
- **ColorMoments** (`ph_compute_color_moments_hash`) — the mean, standard deviation and
  skewness of each channel: nine features in an 18-byte digest, each a signed 16-bit
  big-endian fixed-point number in units of 1/128. Follows the formulas of Stricker &
  Orengo as a restatement gives them, including the sign of the skewness, which is the
  direction of the asymmetry. Compare with `ph_l2_distance()`, which decodes the pairs.
  **Deliberate divergence**: the moments are taken on RGB where the source uses HSV.
  Its distance is dominated by tone: a brightness change of 15 % moves it further than
  turning every hue by 30° ([measured](theory/color-moments.md#tone-against-color)).
- **Use case**: telling apart images that are structurally identical but colored
  differently — recolored product photography, for instance — where the luminance hashes
  move a few bits at most. ColorHash is the one for that; ColorMoments takes most mild
  recolorings for copies.

## 8. Radial Hash

- **Concept**: the variance of pixel values along projection lines through the image
  center, one per degree over 180°; that 180-element vector is standardized, transformed
  with a 1-D DCT, and its first 40 coefficients are the hash.
- **Output**: `ph_digest_t`, 40 bytes.
- **Compare with `ph_radial_similarity()`**, not with `ph_similarity_digest()` or the
  distance functions: the digest is quantized coefficients, not a bit vector. The score is
  the peak of the cross-correlation, and `PH_RADIAL_PCC_THRESHOLD` (0.9) is the source's
  cut — a documented starting point, not a tuned recommendation for your corpus.
- **Tuning**: `ph_context_set_radial_params(projections, samples, sigma)`:
  - `projections` — number of **angles**, default 180, 40–4096.
  - `samples` — default 128 samples per projection, 2–4096.
  - The projections cost `projections × samples` samples whatever the image size (the
    blur before them is what grows with the image), and the digest converges long before
    the ceilings: at 4096 × 4096 it lies within one unit per coefficient of a 16384²
    grid, and the default already scores 0.999 against it. Raising either value past the
    low thousands buys time, not information
    ([measured](theory/radial.md#projections-and-samples)).
  - `sigma` — Gaussian-blur σ applied before the projections, default 3.5, (0, 64/3].
  - gamma (`ph_context_set_gamma()`) — default 1.0 (identity), affects Radial only.
- **Rotation: a few degrees, plus an exact half turn — not arbitrary rotation.** On
  `tests/data/photo.jpeg` the score falls below 0.9 at about 5°, scores like an unrelated
  image from 15° to well past a quarter turn, and returns to 0.99 at 180°, because a
  projection line at α and at α+180 is the same line. The variance profile itself follows
  any turn; its DCT does not, and the digest is the DCT
  ([measured](theory/radial.md#turning-the-image); why in
  [`algorithm-provenance.md`](algorithm-provenance.md) §7).
- **No angular structure, no score**: an image whose variance is nearly the same at every
  angle — a blank one, or a radially symmetric one — has nothing for this descriptor to
  describe and hashes to all zeroes. `ph_radial_similarity()` answers any comparison with
  such a digest with `PH_ERR_NO_STRUCTURE` instead of inventing a score. "Nearly the same"
  is relative to the image's own contrast: below a 1 % spread of the variance profile
  across angles the digest would describe rounding noise (against the same image with ±2
  levels of noise it correlates at 0.4–0.7), while a faint pattern of one gray level still
  gets a digest of its own.

## Computing several `uint64_t` hashes at once

aHash, dHash, pHash and wHash all reduce to grayscale first. `ph_compute_multi()` takes a
bitwise-OR of `ph_hash_flags_t` and computes any combination of them in one call. The work
over the full image is done once per context, whichever algorithms ask for it: the
grayscale conversion, and one area-average pass onto a 32×32 grid of exact sums, from
which aHash (8×8), pHash (32×32 at the default `dct_size`), wHash (16×16) and BMH (16×16
at the default `block_size`) take their working images bit for bit as a direct area
average would give them. dHash resamples on its own (Mitchell, see §2). On a 20-megapixel
photograph the four hashes cost 7.5 ms together: dHash's own resampling pass about 4.9 ms,
the other three with the shared grayscale and area pass 2.5 ms. Results are identical to calling the individual `ph_compute_*`
functions yourself, which share the same cached work — the same saving
`ph_hash_files()`/`ph_hash_buffers()` get for each file of a batch. mHash, BMH, Radial,
ColorHash and ColorMoments are not part of this — their digests don't fit a `uint64_t`,
and BMH's median threshold plus the color algorithms' `PH_ERR_REQUIRES_COLOR` failure
mode don't fit the "stops at the first failure, other slots partially written" contract
either — call them directly.

## Cost

A hash's time grows with the **source image's pixel count**, not with the size of the
hash: before the algorithm proper runs on a few thousand values, the grayscale
conversion and the reduction (or, for mHash and Radial, a blur; for the color hashes, a
pass over every pixel) read the whole decoded image. Measured as the first hash on a
freshly loaded image, so the grayscale conversion and the area pass shared by aHash,
pHash, wHash and BMH are included (`bench_hash hash`; CMake Release, every bundled
decoder, Apple M3 Pro, minimum of 300 and 30 runs, default parameters):

| Algorithm | 400×400 (`photo.jpeg`) | 5472×3648 (`photo_large.jpeg`) |
|---|---|---|
| aHash | 0.05 ms | 2.6 ms |
| dHash | 0.05 ms | 6.3 ms |
| pHash | 0.05 ms | 2.5 ms |
| wHash, fast | 0.05 ms | 2.6 ms |
| wHash, full | 0.09 ms | 7.8 ms |
| BMH | 0.05 ms | 2.6 ms |
| ColorHash | 0.13 ms | 16.7 ms |
| ColorMoments | 0.24 ms | 29.8 ms |
| mHash | 0.92 ms | 33.1 ms |
| Radial | 0.60 ms | 49.4 ms |
| *decoding the JPEG, for scale* | *0.23 ms* | *54.0 ms* |

For the cheap hashes the decode is most of a load-and-hash; for mHash, Radial and the
color hashes on a large image the hash is comparable to it or larger.
`ph_context_set_decode_scale()` shrinks both for JPEG
([measured](theory/preparation.md#decoding-at-a-reduced-scale)).

## Comparison summary

Ratings are relative and qualitative — they come from experience with the library, not
from a measured benchmark. Where a rating depends on a divergence noted above, it is
marked. For measured robustness numbers, see the property tests described in the
[verification methodology](methodology.md); for time, "Cost" above.

| Algorithm | Rotation | Noise | Scaling | Output |
|---|---|---|---|---|
| aHash | ✗ | ★ | ★★★ | 64-bit |
| dHash | ✗ | ★★ | ★★★★ | 64-bit |
| pHash | ★★★ | ★★★★ | ★★★★★ | 64-bit |
| mHash | ★ | ★★★ | ★★★★ | digest, 576-bit |
| wHash | ★ | ★★★ | ★★★★ | 64-bit |
| Radial | ★★ — small angles, see §8 | ★★ | ★★★ | digest, 40 bytes |
| BMH | ★ | ★★★ | ★★★★ | digest, 256-bit default |
| ColorHash | ★★★★★ | ★★★ | ★★★★★ | digest, 108 bytes |
| ColorMoments | ★★★★ | ★★★ | ★★★★★ | digest, 18 bytes |

The two color hashes are insensitive to rotation and scaling for a reason that is worth
stating: they discard spatial layout entirely. That makes them robust and, on their own,
weak discriminators — use them alongside a structural hash, not instead of one.

## Comparing digests: which function for which hash

Every digest says what it holds in its `kind` field, and each comparison function accepts
one kind and refuses the others: Hamming distance and similarity for the bit hashes,
peak correlation for Radial, histogram intersection for ColorHash, L2 distance for
ColorMoments. [Comparing hashes](theory/comparing.md) gives the table, the formulas, the
text form of a stored digest, and the thresholds measured for all nine algorithms.
