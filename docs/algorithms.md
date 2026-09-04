# Algorithmic Deep Dive

What each hash in `libphash` computes, what it is good for, and how to tune it.

Two companion documents carry the parts this one deliberately does not:

- **[`references.md`](references.md)** — the sources. Full citations, links, and how far
  each one can be trusted.
- **[`algorithm-provenance.md`](algorithm-provenance.md)** — the analysis. What each
  source specifies, what this code does, and every place the two differ, classified as a
  defect, a deliberate choice, or something the source leaves open. It also records the
  verification methodology this project works to.

Where an algorithm below is known to depart from its source, this page says so and links
there rather than quietly describing the behaviour as if it were intended.

## Threat model: what these hashes are not

Every hash here is **deterministic and unkeyed**. The same image produces the same hash on
any machine, with no shared secret — which is exactly what deduplication needs, and
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

## Attribution at a glance

| Algorithm | Author | Source | Known to diverge |
|---|---|---|---|
| aHash | Neal Krawetz | blog post, 2011 | no |
| dHash | David Oftedal, described by Neal Krawetz | blog post, 2013 | no |
| pHash | pHash project; documented by Zauner; coefficient rule from Coskun & Sankur | thesis, 2010 | no — follows the reference implementation |
| wHash | this library, after ImageHash | **none** — see below | n/a — justified by measurement |
| mHash | pHash (construction); Marr & Hildreth 1980 (operator) | implementation + paper | no |
| BMH | Yang, Gu & Niu | paper, 2006 | no |
| Radial | De Roover, De Vleeschouwer, Lefèbvre & Macq | paper, 2005 | **yes** — two divergences |
| ColorHash | Swain & Ballard (method); this library (quantisation) | paper, 1991 — **not read** | n/a — no conformance claimed |
| ColorMoments | Stricker & Orengo | paper, 1995 | **yes** — skew sign, colour space |

One cross-cutting caveat: `ph_resize_lanczos()`, used by aHash and dHash, does **not**
resample with Lanczos — it takes stb_image_resize2's default, which is Mitchell for a
downscale. No source specifies a filter, so nothing is violated, but the name is wrong
and the filter is not the one ImageHash uses.

One of the nine — wHash — has no primary source. For those, "correct" can only mean measured
robustness, discrimination and separability — never conformance to a specification,
because there is none.

---

## 1. aHash (Average Hash)

- **Concept**: downscale to 8×8, convert to grayscale, compute the mean luminance, set
  one bit per pixel for above/below the mean.
- **Output**: 64-bit.
- **Strength**: the fastest thing here, and very good at finding a known image again.
- **Weakness**: sensitive to anything that moves the mean — brightness, contrast, gamma.
- **Conformance**: follows its source, including the bit order.

## 2. dHash (Difference Hash)

- **Concept**: downscale to 9×8 and compare each pixel with its right-hand neighbour,
  giving 8 differences per row over 8 rows.
- **Output**: 64-bit.
- **Strength**: as fast as aHash and markedly better at it — gradients survive brightness
  and contrast changes that defeat an average.
- **Conformance**: follows its source exactly, including the direction of the comparison
  (`1` means the left pixel is darker than the right).

## 3. pHash (DCT-based)

- **Concept**: downscale to 32×32, take the two-dimensional type-II DCT, keep the
  low-frequency 8×8 block, and threshold against its median.
- **Output**: 64-bit.
- **Tuning**:
  - `phash_dct_size` — default 32. Larger captures more detail and costs more.
  - `phash_reduction_size` — default 8, giving 8×8 = 64 bits.
- **Strength**: robust to scaling and moderate compression; the usual first choice when
  aHash and dHash are not tolerant enough.
- **The DC coefficient**: DCT(0,0) is thresholded like the other 63 but takes no part in
  choosing the threshold, which is what pHash's `ph_dct_imagehash()` does. Since DC is
  above that threshold for any ordinary image, its bit is always 1 and the hash is
  effectively 63 bits wide — again as pHash. Changed in 2.0.0, and it changed no hash
  value on any test fixture: contrary to the usual explanation, a median is not dragged by
  an outlier. See [`algorithm-provenance.md`](algorithm-provenance.md) §3, which also
  records why the 8×8 block was *not* moved to DCT(1,1) despite both written descriptions
  saying so.

## 4. wHash (Wavelet Hash)

- **Concept**: Haar wavelet decomposition; threshold the low-frequency band against its
  median.
- **Output**: 64-bit.
- **Modes**:
  - `PH_WHASH_FAST` (default) — a fixed 16×16 scale, one decomposition level.
  - `PH_WHASH_FULL` — scale chosen as the largest power of two fitting the image,
    cascaded down to 8×8. Slower, more faithful to the reference implementation.
- **No primary source, deliberately.** wHash has no paper, and it is *not* the ICIP 2000
  algorithm of Venkatesan et al. that is often cited for wavelet hashing — that one is
  keyed, and its key is not optional. No paper describes an unkeyed deterministic wavelet
  hash because there is nothing for the security literature to prove about one. So wHash
  is justified by measurement instead: separability 4.34 on the synthetic corpus, second
  best of the nine. Kept on those grounds rather than replaced.
- One known difference from ImageHash: that implementation zeroes the coarsest LL band by
  default, so its hash describes local structure rather than overall brightness; this one
  does not.

## 5. mHash (Marr–Hildreth)

- **Concept**: normalise to 512×512, equalise the histogram, correlate with a
  Laplacian-of-Gaussian kernel (the Mexican hat of Marr & Hildreth 1980), sum the response
  over 16×16 blocks into a 31×31 grid, and emit nine bits per 3×3 window of that grid,
  each thresholded against its window's mean.
- **Output**: `ph_digest_t`, 72 bytes (576 bits). Compare with
  `ph_hamming_distance_digest()`.
- **Tuning**: `ph_context_set_mhash_params(alpha, level, size)` — `alpha` and `level` set
  the kernel's scale (pHash's own two parameters), `size` the normalisation preset. The
  defaults are 2, 1 and 512, and are the reference implementation's; a sweep over 24
  combinations on two corpora found nothing that reliably beats them.
- **Strength**: a coarse-structure edge descriptor, indifferent to colour — a colour shift
  moves 25 of 576 bits on the test photograph where an unrelated image moves 288.
- **Weakness**: the most expensive hash here (1.8 ms against 0.04 ms for aHash), and it
  notices a small local edit *less* than it notices a rescale — see
  [`algorithm-provenance.md`](algorithm-provenance.md) §5.
- **Changed completely in 2.0.0.** This used to be 64 bits of something that was not a
  Marr–Hildreth hash at all: the sign of a four-neighbour discrete Laplacian on an 18×18
  grid, no Gaussian and no scale. The signature changed with it, and `PH_HASH_MHASH` is
  gone from the multi-hash bitfield — 576 bits do not fit a `uint64_t`.

## 6. BMH (Block Mean Hash)

- **Concept**: divide the image into a grid of blocks, take the mean of each, and
  threshold the block values.
- **Output**: `ph_digest_t`, `block_size²` bits — 256 bits at the default 16×16.
- **Tuning**: `block_size` via `ph_context_set_block_params`, 1..22 (22×22 bits is the
  largest grid that fits a digest).
- **Use case**: when 64 bits are not enough entropy and a lower collision rate is worth
  the extra bytes.
- **Threshold**: the **median** of the block means, as the paper specifies, which is what
  makes the bit distribution balanced by construction. Changed in 2.0.0 — it was the
  arithmetic mean, so every BMH value moves. Note that this puts the library at odds with
  OpenCV's `BlockMeanHash`, which thresholds on the mean (in a variable it calls `median`);
  expect BMH values to differ from OpenCV's. See
  [`algorithm-provenance.md`](algorithm-provenance.md) §6.

## 7. ColorHash and ColorMoments

Both need colour: they return `PH_ERR_REQUIRES_COLOR` on a grayscale image.

- **ColorHash** (`ph_compute_color_hash`) — a colour histogram: every pixel is counted
  into one of 108 bins of the opponent colour space (red–green × blue–yellow × light–dark,
  6 × 6 × 3), and two of them are compared with **`ph_histogram_intersection()`**, not with
  a bit or vector metric. A colour histogram with histogram intersection, after Swain &
  Ballard (1991) — the paper could not be obtained, so it is implemented from secondary
  descriptions and no conformance to it is claimed; the quantisation is this library's,
  chosen by measurement over sixteen candidates.
  **Changed completely in 2.0.0**, signature included: it used to be a 42-bit port of
  ImageHash's `colorhash`, for which even ImageHash cites nothing. Separability on the
  test corpus went from 1.89 to 3.95. `PH_HASH_COLOR_HASH` is gone from the multi-hash
  bitfield — call the function directly.
  **Blind spots**, both inherent to a histogram and both asserted in the tests: it ignores
  where the colours are, so a 90° rotation does not move it at all and neither does
  shuffling the pixels; and flat colours that share a chroma bin and an intensity third —
  black against dark grey, light grey against white — are indistinguishable.
- **ColorMoments** (`ph_compute_color_moments_hash`) — the mean, standard deviation and
  skewness of each channel, 9 bytes. Follows the formulas of Stricker & Orengo.
  **⚠ Known divergence**: the sign of the skewness is discarded, which is the direction
  of the asymmetry — half of what the third moment tells you. The moments are also taken
  on RGB where the source uses HSV.
- **Use case**: telling apart images that are structurally identical but coloured
  differently — recoloured product photography, for instance — where the luminance hashes
  agree by design.

## 8. Radial Hash

- **Concept**: the variance of pixel values along projection lines through the image
  centre, one per degree over 180°; that 180-element vector is standardised, transformed
  with a 1-D DCT, and its first 40 coefficients are the hash.
- **Output**: `ph_digest_t`, 40 bytes.
- **Compare with `ph_radial_similarity()`**, not with `ph_similarity_digest()` or the
  distance functions: the digest is quantised coefficients, not a bit vector. The score is
  the peak of the cross-correlation, and `PH_RADIAL_PCC_THRESHOLD` (0.9) is the source's
  cut — a documented starting point, not a tuned recommendation for your corpus.
- **Tuning**:
  - `radial_projections` — number of **angles**, default 180, 40–131072.
  - `radial_samples` — default 128 samples per projection.
- **Changed in 2.0.0**: the DCT the source specifies is now applied and the 40 is back on
  the coefficient count rather than the angle count; the variance vector is standardised
  before the transform; and the source's comparison is exposed. Radial digests from 1.x do
  not carry over.
- **Rotation: a few degrees, plus an exact half turn — not arbitrary rotation.** Measured
  on `tests/data/photo.jpeg` against a 0.69 baseline for an unrelated image: 1° → 0.993,
  3° → 0.944, 5° → 0.870, 15° → 0.437, 90° → 0.243, 180° → 0.993. That is what the
  algorithm's source delivers and what this page's earlier "up to 360°" claim got wrong;
  the half turn matches because a projection line at α and at α+180 is the same line. See
  [`algorithm-provenance.md`](algorithm-provenance.md) §7 for why the transform does not
  carry a larger rotation.
- **Remaining divergence**: the default gamma is 2.2 where pHash defaults to 1.0, and the
  blur is a fixed 3×3 kernel (σ ≈ 0.707) where pHash defaults σ to 3.5.
- **Blind spot worth knowing**: an image whose variance is the same at every angle — a
  radially symmetric one — has no angular structure for this descriptor, and hashes to all
  zeroes. Two such images compare as identical.

## Comparison summary

Ratings are relative and qualitative — they come from experience with the library, not
from a measured benchmark. Where a rating depends on a divergence noted above, it is
marked. For measured numbers, see the property tests described in the verification
methodology.

| Algorithm | Speed | Rotation | Noise | Scaling | Output |
|---|---|---|---|---|---|
| aHash | ★★★★★ | ✗ | ★ | ★★★ | 64-bit |
| dHash | ★★★★★ | ✗ | ★★ | ★★★★ | 64-bit |
| pHash | ★★★ | ★★★ | ★★★★ | ★★★★★ | 64-bit |
| mHash | ★ | ★ | ★★★ | ★★★★ | digest, 576-bit |
| wHash | ★★ | ★ | ★★★ | ★★★★ | 64-bit |
| Radial | ★ | ★★ — small angles, see §8 | ★★ | ★★★ | digest, 40 bytes |
| BMH | ★★★ | ★ | ★★★ | ★★★★ | digest, 256-bit default |
| ColorHash | ★★★★ | ★★★★★ | ★★★ | ★★★★★ | digest, 108 bytes |
| ColorMoments | ★★★ | ★★★★ | ★★★ | ★★★★★ | digest, 9 bytes |

The two colour hashes are insensitive to rotation and scaling for a reason that is worth
stating: they discard spatial layout entirely. That makes them robust and, on their own,
weak discriminators — use them alongside a structural hash, not instead of one.
