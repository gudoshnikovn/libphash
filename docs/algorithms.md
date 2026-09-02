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
| pHash | pHash project; documented by Zauner; coefficient rule from Coskun & Sankur | thesis, 2010 | **yes** — DC coefficient |
| wHash | this library, after ImageHash | **none** — see below | n/a — justified by measurement |
| mHash | this library | **none** | n/a — the *name* is wrong, see below |
| BMH | Yang, Gu & Niu | paper, 2006 | **yes** — mean instead of median |
| Radial | De Roover, De Vleeschouwer, Lefèbvre & Macq | paper, 2005 | **yes** — three divergences |
| ColorHash | Johannes Buchner (ImageHash) | **none** | n/a |
| ColorMoments | Stricker & Orengo | paper, 1995 | **yes** — skew sign, colour space |

One cross-cutting caveat: `ph_resize_lanczos()`, used by aHash and dHash, does **not**
resample with Lanczos — it takes stb_image_resize2's default, which is Mitchell for a
downscale. No source specifies a filter, so nothing is violated, but the name is wrong
and the filter is not the one ImageHash uses.

Three of the nine have no primary source. For those, "correct" can only mean measured
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
- **⚠ Known divergence**: this implementation includes the DC coefficient DCT(0,0) in
  both the median and the hash bits, where both sources exclude it by name. It is the
  image mean, dwarfs the AC terms, and drags the median. See
  [`algorithm-provenance.md`](algorithm-provenance.md) §3.

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

## 5. mHash

- **Concept**: the sign of a 3×3 four-neighbour discrete Laplacian, sampled on a stride-2
  grid of an 18×18 reduction. Structure only — it responds to edges, not to levels.
- **Output**: 64-bit.
- **Tuning**: none. It uses a fixed 18×18 grid and **ignores** `ph_context_set_block_params`,
  which affects BMH only.
- **⚠ The name is wrong and is kept only for API compatibility.** mHash is *not* a
  Marr–Hildreth hash: there is no Gaussian, no scale parameter and no zero-crossing
  detection, which is what Marr–Hildreth means. It is also not pHash's
  `ph_mh_imagehash()`, which is a genuinely different algorithm producing 576 bits. This
  hash is original to this library and has no source; judge it by measurement.

## 6. BMH (Block Mean Hash)

- **Concept**: divide the image into a grid of blocks, take the mean of each, and
  threshold the block values.
- **Output**: `ph_digest_t`, `block_size²` bits — 256 bits at the default 16×16.
- **Tuning**: `block_size` via `ph_context_set_block_params`, 1..22 (22×22 bits is the
  largest grid that fits a digest).
- **Use case**: when 64 bits are not enough entropy and a lower collision rate is worth
  the extra bytes.
- **⚠ Known divergence**: the source thresholds against the **median** of the block
  means; this implementation uses their arithmetic mean. The median is what makes the bit
  distribution balanced by construction, which is the property the paper relies on. See
  [`algorithm-provenance.md`](algorithm-provenance.md) §6.

## 7. ColorHash and ColorMoments

Both need colour: they return `PH_ERR_REQUIRES_COLOR` on a grayscale image.

- **ColorHash** (`ph_compute_color_hash`) — classifies every pixel as black, grey, or one
  of six hue bins split into faint and bright, then encodes the 14 resulting fractions in
  3 bits each. 64-bit output, 42 significant bits. No primary source; it is ImageHash's
  `colorhash`, for which even ImageHash cites nothing.
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
  centre, at a series of angles.
- **Output**: `ph_digest_t`, one byte per projection.
- **Tuning**:
  - `radial_projections` — default 40.
  - `radial_samples` — default 128 samples per projection.
- **⚠ This is the algorithm furthest from its source**, with three divergences, and the
  claim previously made on this page — robustness to rotation up to 360° — **does not
  hold today**:
  - The source applies a DCT to the radial variance vector and keeps the first 40
    coefficients of 180 angles. This implementation takes 40 *angles* and no DCT; the
    40 was transplanted from the wrong place.
  - The source compares two hashes by the peak of cross-correlation, which is precisely
    what turns a rotation — a cyclic shift of the vector — into a match. `libphash`
    compares digests element-wise, so **rotation invariance is not delivered**.
  - The default gamma is 2.2 where the pHash authors suggest 1.

  Until those are fixed, treat Radial as a variance-profile descriptor, not as a
  rotation-invariant hash. See [`algorithm-provenance.md`](algorithm-provenance.md) §7.

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
| mHash | ★★★★ | ★ | ★★★ | ★★★★ | 64-bit |
| wHash | ★★ | ★ | ★★★ | ★★★★ | 64-bit |
| Radial | ★ | ✗ — see §8 | ★★ | ★★★ | digest |
| BMH | ★★★ | ★ | ★★★ | ★★★★ | digest, 256-bit default |
| ColorHash | ★★★ | ★★★★ | ★★★ | ★★★★★ | 64-bit (42 used) |
| ColorMoments | ★★★ | ★★★★ | ★★★ | ★★★★★ | digest, 9 bytes |

The two colour hashes are insensitive to rotation and scaling for a reason that is worth
stating: they discard spatial layout entirely. That makes them robust and, on their own,
weak discriminators — use them alongside a structural hash, not instead of one.
