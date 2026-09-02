# Algorithm provenance

Where each of the nine hashes in `libphash` comes from, what its source actually
specifies, what this implementation does, and where the two differ.

This document exists because the library previously had only one reference point for
correctness — "score no worse than ImageHash" — and that is not a specification. A
difference from ImageHash is not evidence of a bug, and an agreement with it is not
evidence of correctness. What follows separates the two.

## How to read this

Every algorithm gets the same five headings: **Author**, **Primary source**, **What
the source specifies**, **What this implementation does**, and **Delta**. Each row of a
Delta table is classified as one of:

- **defect** — contradicts a formula or step the primary source states explicitly.
- **deliberate** — differs from the source, but for a reason recorded here.
- **undefined** — the source does not say, so there is nothing to conform to.

A defect listed here is *not* fixed by this document. Each becomes its own task, with
its own before/after measurement. This document ends at the finding.

### Source trust ranking

Applied when sources disagree, strongest first:

1. Peer-reviewed paper or thesis by the algorithm's author.
2. Technical report or preprint.
3. Code published by the algorithm's author.
4. Any third-party implementation (ImageHash, OpenCV, a blog restatement).

A third-party implementation is a hint about where to look. It is never itself the
basis for a claim of correctness. Where a paper could not be read directly (IEEE and
SPIE paywalls), the restatement used is named inline and its rank is stated, so the
weight of each claim stays visible.

### What could not be read directly

These primary documents are paywalled and were **not** retrieved. Every statement
attributed to them here comes from the named restatement, not from the paper:

| Paper | Restatement used | Rank of restatement |
|---|---|---|
| Yang, Gu, Niu (IIH-MSP 2006) | Zauner's thesis §3.1.4, which reproduces all four methods step by step | thesis (rank 1) citing the paper |
| De Roover et al. (ICIP 2005); Lefèbvre et al. (EUSIPCO 2002); Standaert et al. (ITCC 2005) | Zauner's thesis §3.1.3 and §3.2.3 | thesis (rank 1) citing the papers |
| Stricker & Orengo (SPIE 1995) | N. Keen, *Color Moments*, University of Edinburgh CVonline course notes, 2005 | student coursework (rank 4) |
| Venkatesan, Koon, Jakubowski, Moulin (ICIP 2000) | not needed — see wHash, the hypothesis was rejected on other grounds |

The Stricker & Orengo formulas therefore rest on the weakest evidence in this
document. They are marked as such in that section.

---

## 1. aHash — Average Hash

**Author:** Neal Krawetz.

**Primary source:** ["Looks Like It"](https://www.hackerfactor.com/blog/index.php?/archives/432-Looks-Like-It.html),
The Hacker Factor Blog, 26 May 2011. **This is a blog post, not a paper.** There is no
peer-reviewed publication of aHash; the post is the author's own description and is
therefore rank 3 — the best that exists for this algorithm.

**What the source specifies:**

1. "Reduce size" — shrink to 8×8, 64 pixels, ignoring aspect ratio.
2. "Reduce color" — convert to grayscale.
3. "Average the colors" — the **mean** of the 64 values.
4. "Compute the bits" — "each bit is simply set based on whether the color value is
   above or below the mean."
5. "Construct the hash" — "The order does not matter, just as long as you are
   consistent. (I set the bits from left to right, top to bottom using big-endian.)"

The post specifies no resampling filter, no grayscale coefficients, and no rule for a
pixel exactly equal to the mean.

**What this implementation does** (`src/hashes/ahash.c`): grayscale via BT.601-approximate
integer weights (38/75/15 over 128, configurable), resize to 8×8 through
`ph_resize_lanczos()`, mean of the
64 bytes truncated to `uint8_t`, bit set when `pixel > avg`, bit index `63 - i` in
row-major order — that is, MSB first, left to right, top to bottom, big-endian.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| Resampling filter | undefined | Source says only "shrink". Note that `ph_resize_lanczos()` **does not use Lanczos**: it calls `stbir_resize_uint8_linear()`, which for a downscale resolves to stb's `STBIR_DEFAULT_FILTER_DOWNSAMPLE` — **Mitchell**. The name is wrong and so is the assumption that this matches ImageHash's `LANCZOS`. Nothing in the source is violated, but see the naming defect below. |
| Grayscale coefficients | undefined | Source says only "convert to a grayscale". |
| Ties (`pixel == avg` → 0) | undefined | "Above or below" leaves the tie unstated. |
| Average truncated to `uint8_t` before comparison | undefined | Loses at most one level; source does not specify precision. |

**Verdict: conforms.** Including the bit order, which the source explicitly leaves free
but happens to describe exactly as implemented here.

---

## 2. dHash — Difference Hash

**Author:** David Oftedal proposed it as a comment on Krawetz's 2011 post; Neal Krawetz
named, described and evaluated it. Attribution should name both.

**Primary source:** ["Kind of Like That"](https://www.hackerfactor.com/blog/index.php?/archives/529-Kind-of-Like-That.html),
The Hacker Factor Blog, 21 January 2013. Again a blog post, rank 3, and again the only
description by the people responsible for the algorithm.

**What the source specifies:**

1. Shrink to **9×8** — 72 pixels — ignoring aspect ratio.
2. Convert to grayscale.
3. "The 9 pixels per row yields 8 differences between adjacent pixels. Eight rows of
   eight differences becomes 64 bits."
4. "I use a '1' to indicate that P[x] < P[x+1] and set the bits from left to right, top
   to bottom using big-endian."

**What this implementation does** (`src/hashes/dhash.c`): resize to 9×8 through
`ph_resize_lanczos()` (Mitchell — see aHash), bit set when `row[col] < row[col+1]`, bit index `63 - (row*8 + col)`.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| Resampling filter | undefined | As for aHash, including the misnamed helper. |
| Grayscale coefficients | undefined | As for aHash. |

**Verdict: conforms**, down to the direction of the comparison and the bit order, both
of which the source states explicitly.

---

## 3. pHash — DCT-based hash

**Author:** No single one. The construction is described independently by Neal Krawetz
(blog, 2011) and by the pHash library, whose DCT hash Zauner records as "inspired by" a
DCT-based *video* hash by Coskun and Sankur.

**Primary sources:**

- Christoph Zauner, *Implementation and Benchmarking of Perceptual Image Hash
  Functions*, Diplomarbeit, University of Applied Sciences Hagenberg, July 2010 —
  [PDF](https://www.phash.org/docs/pubs/thesis_zauner.pdf). §3.1.1 gives the DCT
  definitions, §3.2.1 documents what pHash's `ph_dct_imagehash()` actually computes.
  **Rank 1** and the best source available for this algorithm.
- B. Coskun and B. Sankur, "Robust video hash extraction", *Proc. Signal Processing and
  Communications Applications Conference*, IEEE, April 2004, pp. 292–295 — the origin
  of the "select 64 low-frequency coefficients, omitting the lowest" rule, cited by
  Zauner as [9].
- Krawetz, "Looks Like It" (as above) — an independent description of the same idea
  with different details.

**What the sources specify:**

Zauner §3.2.1, on pHash:

> The method `ph_dct_imagehash()` first converts the image to grey scale using only its
> luminance. […] Then a mean filter is applied to the image. A kernel with dimension
> 7x7 is used. […] After this operation the image is resized to 32x32 pixels.
> Consequently, a DCT matrix is generated and the two-dimensional type-II DCT
> coefficients are calculated using matrix multiplications. […] As proposed in [9], **64
> low-frequency DCT coefficients, omitting the lowest frequency coefficients**, are
> selected for hash extraction. pHash therefore selects 8x8 transform coefficients. The
> coefficient DCT(1, 1) being the upper left corner of the matrix and the coefficient
> DCT(8/8) being the lower right corner.

and the thresholding rule, Zauner equation 3.10:

> Once the median m of the 64 DCT coefficients has been determined […]
> h_i = 0 if C_i < m, 1 if C_i ≥ m.

Krawetz says the same thing about the DC term and quotes David Starkweather of pHash
directly:

> Compute the average value […] using only the 8x8 DCT low-frequency values and
> **excluding the first term since the DC coefficient can be significantly different
> from the other values and will throw off the average** […]
>
> "the dct hash is based on the low 2D DCT coefficients starting at the second from
> lowest, **leaving out the first DC term**. This excludes completely flat image
> information (i.e. solid colors) from being included in the hash description."

The two sources disagree on the threshold: Krawetz says **mean**, Zauner/pHash says
**median**. They agree, independently, that the **DC term is excluded**.

**What this implementation does** (`src/hashes/phash.c`): grayscale, box resize to 32×32,
type-II DCT by matrix multiplication with the same matrix definition as Zauner's
equation 3.3, coefficients **(0,0) through (7,7) — the DC term included** — median of
those 64, bit set when `value > median`.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| **DC coefficient DCT(0,0) included in both the median and the hash bits** | **defect** | Contradicts both sources, which exclude it for the same stated reason: the DC term is the image mean, is far larger than the AC terms, and drags the median. One of the 64 bits is also close to determined — DCT(0,0) is non-negative and usually the largest value, so its bit is almost always 1. Matches ImageHash exactly, which is why comparing against ImageHash could never have revealed it. |
| No 7×7 mean prefilter before the resize | deliberate | `ph_resize_box()` already averages over each source region, which is a low-pass step of a similar kind. Not identical to a 7×7 mean at full resolution; worth measuring rather than assuming. |
| Threshold is the median | deliberate | The two sources disagree; the rank-1 source (Zauner/pHash) says median. |
| `>` rather than `≥` | undefined in effect | Zauner's 3.10 says `≥`. With floating-point coefficients an exact tie against the median only occurs for an even count where two middle values are equal, so this changes a bit only on degenerate input (e.g. a solid-colour image, where all AC terms are 0). Real but negligible; fold into the DC-term fix. |
| Box resampling | undefined | No source specifies a filter. Zauner's account of pHash has a 7×7 mean filter and then a resize, so a box filter is at least the same kind of operation. |

---

## 4. wHash — Wavelet hash

**Author:** Johannes Buchner, as `whash` in the ImageHash library.

**Primary source: none.** ImageHash's README cites a
[blog post](https://fullstackml.com/wavelet-image-hash-in-python-3504fdd282b5) by
Alexander Petrov, not a paper. No academic publication describes this construction.
Rank 4 is the best available, and the honest statement is that wHash has no primary
source.

**Hypothesis rejected.** Venkatesan, Koon, Jakubowski and Moulin, "Robust Image
Hashing", *ICIP 2000*, vol. 3, pp. 664–666, DOI
[10.1109/ICIP.2000.899541](https://doi.org/10.1109/ICIP.2000.899541), is a real paper
and is often named as the origin of wavelet-based image hashing. It is **not** the
source of this algorithm: it builds a hash from statistics of randomly tiled wavelet
subbands under a secret key, followed by error-correction decoding. None of that —
keying, random tiling, ECC — appears in ImageHash's `whash` or here. It belongs in a
related-work list, not in an attribution header.

**What the reference implementation does** (ImageHash `whash`, the thing this was ported
from, rank 4): grayscale; resize to `image_scale`, the largest power of two not
exceeding the smaller image dimension; divide by 255; Haar `wavedec2` to level
`log2(image_scale) − log2(hash_size)`; **with `remove_max_haar_ll=True` by default, zero
the LL coefficients of a full decomposition before the main one**; median of the
remaining `hash_size × hash_size` low band; bit set when `value > median`.

**What this implementation does** (`src/hashes/whash.c`): two modes.

- `PH_WHASH_FAST` (default): box resize to a fixed 16×16, divide by 255, one Haar level
  (orthonormal, both sums and differences divided by √2), take the top-left 8×8, median,
  `>`.
- `PH_WHASH_FULL`: `image_scale` = largest power of two ≤ the smaller dimension, floored
  at 8; cascade Haar levels down to 8×8; same median and comparison.

**Delta** — against the reference implementation, since there is no source to conform to:

| Difference | Class | Note |
|---|---|---|
| `remove_max_haar_ll` not implemented | deliberate, undocumented until now | ImageHash removes the coarsest LL band so the hash describes local structure rather than overall brightness. Omitting it makes wHash more like aHash than intended. There is no source that says which is correct — this is a candidate for the measurable-property criterion, not for a formula check. |
| Default mode fixes the scale at 16×16 | deliberate | `PH_WHASH_FULL` implements the power-of-two rule. Speed/robustness trade-off. |
| Box resampling, where the reference implementation resamples with PIL's `LANCZOS` | undefined | |

---

## 5. mHash

**Author:** this library. **Primary source: none.**

**The name is a misattribution and must change.** `mHash` was added in v1.2.0 in a
commit titled "Introduce Marr-Hildreth Hash (mHash)", and `docs/algorithms.md` still
calls it "Marr Hash". It is not a Marr–Hildreth hash, on two independent counts.

Marr–Hildreth means the Laplacian of Gaussian: per Zauner §3.1.2 and definition 3.5,
∇²g is sampled at a chosen scale σ, convolved with the image, and **edges are the
zero-crossings** of the result. What `src/hashes/mhash.c` computes is the *sign* of a
3×3 four-neighbour discrete Laplacian, sampled on a stride-2 grid of an 18×18 box-resized
image. There is no Gaussian, no scale parameter, and no zero-crossing detection — only
`4·centre − (up + down + left + right) > 0`. The introducing commit says as much
("a Laplacian kernel approximation"); the name overstates it.

Nor does it match the algorithm that actually bears the name in the library it was
presumably taken from. Zauner §3.2.2 documents pHash's `ph_mh_imagehash()`: grayscale,
Canny–Deriche blur with σ = 1.0, resize to **512×512**, histogram equalisation over 256
levels, then an LoG kernel with scale α = 2 at level 1, producing a **576-bit** hash.
Ours produces 64 bits from an 18×18 grid. These are different algorithms.

The plain 4-neighbour Laplacian itself is textbook (Zauner's definition 3.3, after
Bovik's *The Essential Guide to Image Processing*). Marr and Hildreth's "Theory of edge
detection" (1980) is the source for LoG and should be cited **only** if the
implementation is changed to actually use it.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| Documented and named as Marr–Hildreth | **defect (naming/documentation)** | The hash itself is self-consistent and may well be useful; the claim about what it is, is false. Fix is to rename or to re-describe honestly — not to change the maths. Public API compatibility makes `ph_compute_mhash` hard to rename in 2.0.0; the description is what has to change. |
| `docs/algorithms.md` calls it "block-based […] utilizing `ph_context_set_block_params`" | **defect (documentation)** | Already contradicted by `include/libphash.h`, which correctly records that mHash uses a fixed 18×18 grid and ignores that setting. |
| No primary source | — | To be evaluated only by measurable properties. |

---

## 6. BMH — Block Mean Value hash

**Author:** Bian Yang, Fan Gu, Xiamu Niu.

**Primary source:** "Block Mean Value Based Image Perceptual Hashing", *Proc.
International Conference on Intelligent Information Hiding and Multimedia Signal
Processing (IIH-MSP)*, IEEE, 2006, pp. 167–172, ISBN 0-7695-2745-0. Paywalled and not
read directly; the steps below are Zauner's §3.1.4 reproduction of the paper's method 1,
which he implemented into pHash as part of the thesis. Rank 1 restatement of a rank 1
source.

**What the source specifies (method 1):**

1. Convert to grayscale and normalise to a preset size.
2. Divide the pixels into N non-overlapping blocks I₁…I_N, N being the bit length.
3. Encrypt the block indices with a secret key K to permute the scan order.
4. Compute the mean of each block, giving M₁…M_N, and **obtain the median value M_d of
   the mean value sequence**.
5. Equation 3.9: `h(i) = 0 if M_i < M_d, 1 if M_i ≥ M_d`.

Methods 2–4 add overlapping blocks and rotation; neither is implemented here, and both
are out of scope. Step 3 is omitted by pHash's own implementation too, and the paper
does not name an encryption algorithm.

**What this implementation does** (`src/hashes/bmh.c`): grayscale, `ph_resize_box()`
straight to `block_size × block_size` (default 16×16 → 256 bits), **arithmetic mean** of
the resulting values, bit set when `value >= avg`, packed LSB-first within each byte.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| **Threshold is the arithmetic mean, not the median of the block means** | **defect** | Directly contradicts step 4 and equation 3.9. The median is what makes the bit distribution balanced by construction — exactly 50% ones — which is the property the paper relies on. With the mean, a dark image with a few bright blocks produces a lopsided hash and loses discriminative power. |
| No preset normalisation size; the image is resampled straight to the block grid | deliberate | The paper says "normalize the original image into a preset size" and then block-average; pHash uses 256×256. A box resample to 16×16 equals the block means only when the source dimensions are a multiple of 16 — otherwise source pixels are weighted across block boundaries. Fixing this means adding the intermediate normalisation step. |
| Key-permuted block order omitted | deliberate | Also omitted by pHash. It is a security feature (unpredictability under a key), not a perceptual one, and the paper leaves the cipher unspecified. |
| `≥` at the threshold | conforms | Matches equation 3.9. |
| Bit packing LSB-first within a byte | undefined | The paper defines a bit sequence, not a byte layout. |

---

## 7. Radial — Radial variance hash

**Authors:** Frédéric Lefèbvre, Benoît Macq and Jean-Didier Legat proposed the original
RASH; Christophe De Roover, Christophe De Vleeschouwer, Lefèbvre and Macq published the
radial *variance* algorithm that pHash implements.

**Primary sources:**

- C. De Roover, C. De Vleeschouwer, F. Lefèbvre, B. Macq, "Robust image hashing based on
  radial variance of pixels", *Proc. ICIP*, vol. 3, IEEE, Sept. 2005, pp. 77–80 — the
  algorithm actually implemented by pHash (Zauner [35], and §3.2.3: "pHash implements
  the algorithm as proposed in [35]").
- F. Lefèbvre, B. Macq, J.-D. Legat, "RASh: RAdon Soft Hash algorithm", *Proc. EUSIPCO*,
  vol. I, Sept. 2002, pp. 299–302 — the predecessor, which the same authors later
  reported as suffering "some troubles".
- F.-X. Standaert et al., "Practical evaluation of a radial soft hash algorithm", *Proc.
  ITCC*, vol. 2, IEEE, April 2005, pp. 89–94.

All three paywalled; content below from Zauner §3.1.3 and §3.2.3.

**What the sources specify:**

Definition 3.6 — the radial variance vector, for **α = 0, 1, …, 179**, over Γ(α), the
one-pixel-wide strip of pixels on the projection line through the image centre:

> R[α] = ( Σ I²(x,y) / #Γ(α) ) − ( Σ I(x,y) / #Γ(α) )²

180 rather than 360 angles because the Radon transform is symmetric. And then, crucially:

> Finally, in [35], the perceptual image hash function was further improved by **applying
> the DCT to the radial variance vector. The first 40 coefficients of the transformed
> radial variance vector form the so-called radial hash vector** in the end. This omits
> redundant components of the radial variance vector and efficiently decorrelates it.

pHash's implementation, per §3.2.3: 40-byte hash; Gaussian blur σ and gamma correction,
for which "the authors suggest 1 for both variables"; N = 180 angles by default;
**comparison by peak of cross-correlation (PCC)** with a default threshold of 0.9; and,
uniquely among the four hashes in the thesis, **no normalisation of image resolution**.

**What this implementation does** (`src/hashes/radial.c`): 3×3 Gaussian blur, gamma
correction with a default of **2.2**, then for each of `radial_projections` angles
(**default 40**) spread over [0, π), sample `radial_samples` points (**default 128**)
bilinearly along the line through the image centre out to `min(w,h)/2`, compute the
variance with exactly the source's formula, then normalise each variance by the maximum
across projections, take the square root, and quantise to a byte. Digests are compared
by `ph_hamming_distance_digest()` or `ph_l2_distance()`.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| **The DCT of the radial variance vector is not applied** | **defect** | The final and, per the source, the *improving* step of the algorithm is missing. Without it the output is the raw variance profile, still correlated across neighbouring angles, and the decorrelation the paper credits for the improvement never happens. |
| **40 projections instead of 180** | **defect** | The 40 in the source is the number of **DCT coefficients kept from a 180-element vector**, not the number of angles. The constant was transplanted to the wrong place: `PH_RADIAL_PROJECTIONS = 40` produces a 40-element hash by sampling only 40 angles, which is a coarser angular resolution *and* skips the transform. Fixing the previous row should fix this one. |
| **Comparison is element-wise (Hamming or L2), not the peak of cross-correlation** | **defect** | A rotation cyclically shifts the radial variance vector; PCC over shifts is precisely what turns that into invariance. With element-wise distance there is no rotation invariance at all — which makes `docs/algorithms.md`'s "Unmatched robustness against rotation (up to 360°)" a false claim, and one no current test checks. |
| **Default gamma 2.2 where the pHash authors suggest 1** | **defect** | Already filed as its own task. The thesis is direct: "the authors suggest 1 for both variables". Applying a 2.2 correction by default means the reference and this library see different pixel values before the variance is even computed. |
| 128 samples per projection, bilinearly interpolated | deliberate | The source integrates over the pixels of a one-pixel-wide strip, whose count varies with the angle and the image size; a fixed sample count is a different estimator of the same quantity. Cheaper and resolution-independent, but it is an approximation, not the definition. |
| Radius capped at `min(w,h)/2` | deliberate | Keeps every projection inside the image. The source does not normalise resolution and does not discuss the cap. |
| Normalise by the maximum and take the square root | deliberate | Not in the source. It exists to fit the variances into bytes. Note it also destroys the absolute scale that the DCT step would otherwise act on. |
| 3×3 box-weight Gaussian, not a σ-parameterised one | undefined | The source has σ as a parameter; here the kernel is fixed. |

Radial is the algorithm furthest from its source, and the only one where the source's
own comparison function is missing.

---

## 8. ColorHash — HSV histogram hash

**Author:** Johannes Buchner, as `colorhash` in ImageHash.

**Primary source: none.** ImageHash's README gives no reference for `colorhash` — no
paper, not even a blog post. The implementation *is* the specification, and it is rank
4. This should be stated plainly in the attribution rather than implied to be more.

**What the reference implementation does:** convert to the PIL "L" intensity
`(299R + 587G + 114B)/1000` and to PIL's HSV, in which H, S and V are all 0–255; classify
each pixel as black if intensity < 32, else grey if saturation < 85, else into one of 6
hue bins, split into "faint" (saturation < 170) and "bright"; produce 14 fractions —
black and grey over all pixels, the 12 hue buckets over the coloured pixels only — and
encode each in `binbits = 3` bits.

**What this implementation does** (`src/hashes/color_hsv.c`): the same, computed inline
from RGB with the same thresholds (32, 85, 170), the same 6 hue bins, the same 14
values, and `ph_pack_3bit_values()` writing 42 bits MSB-first from bit index 41
downwards.

**Delta:** none identified against the reference implementation. There is no primary
source against which to find one.

---

## 9. ColorMoments

**Authors:** Markus Stricker and Markus Orengo.

**Primary source:** "Similarity of color images", *Proc. SPIE 2420, Storage and Retrieval
for Image and Video Databases III*, 1995, pp. 381–392, DOI
[10.1117/12.205308](https://doi.org/10.1117/12.205308). **Paywalled and not read.** The
formulas below come from N. Keen, *Color Moments*, University of Edinburgh CVonline
course notes, 10 February 2005 — rank 4. This is the weakest evidence in this document
and the statements below should be re-checked against the paper before anything is
changed on their basis.

**What the source specifies** (per that restatement): three central moments per colour
channel i over N pixels p_ij —

- mean `E_i = (1/N) Σ p_ij`
- standard deviation `σ_i = sqrt( (1/N) Σ (p_ij − E_i)² )`
- skewness `s_i = cbrt( (1/N) Σ (p_ij − E_i)³ )`

giving a 9-element feature vector; computed **in HSV**; and compared by a weighted sum
of absolute differences, `d = Σ_i w_i1|ΔE_i| + w_i2|Δσ_i| + w_i3|Δs_i|`, with the weights
left to the user.

**What this implementation does** (`src/hashes/color_moments.c`): exactly those three
formulas, in `double`, including the signed cube root — `cbrt()`, not `pow(x, 1/3)`, so
a negative third moment is handled correctly. Computed on the **raw RGB channels**. Each
value is then written into one byte of a 9-byte digest as `mean`, `min(255, σ)` and
`min(255, |skew|)`.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| **The sign of the skewness is discarded (`fabs`)** | **defect** | Skewness measures the *direction* of asymmetry; its sign is half the information. Two images whose channel distributions are mirror images of each other get identical bytes. The natural fix is to store `skew + 128` clamped, or to widen the digest — either way it changes stored hashes. |
| Moments computed on RGB, not HSV | deliberate, needs a decision | The restatement says HSV, while noting "alternative encoding could just as easily be used". RGB is defensible; it should be recorded as a choice rather than left implicit. Confirm against the paper first, given the rank of the source. |
| Values clamped into `uint8_t` | deliberate | σ can reach 127.5 and the skew term further, so clamping at 255 rarely bites; the quantisation to integers does cost precision. |
| Distance is L2 over the 9 bytes, not the weighted L1 of the source | deliberate | The source leaves the weights to the application, so there is no defined default to conform to. |

---

## Status of the hypotheses in the task

| Algorithm | Hypothesis | Outcome |
|---|---|---|
| aHash | Krawetz, "Looks Like It", possibly a blog post rather than a paper | **Confirmed**, and it is indeed a blog post, 26 May 2011. Recorded as such. |
| dHash | Krawetz, "Kind of Like That", same caveat | **Confirmed**, blog post, 21 January 2013. Refined: the algorithm was proposed by David Oftedal; Krawetz described and evaluated it. |
| pHash | Zauner's thesis is the most likely good primary source | **Confirmed** as the best available, and it identifies the deeper origin: Coskun & Sankur 2004. Both it and Krawetz exclude the DC term, which this implementation does not. |
| wHash | Check Venkatesan et al., ICIP 2000; separately, ImageHash by Buchner | **Rejected** as our source — a real paper, but a different algorithm (keyed random tiling + ECC). Actual origin: Buchner's ImageHash, itself citing only a blog post. wHash has no primary source. |
| mHash | First find out what this even is; if it matches pHash's `ph_mh_imagehash`, cite Marr & Hildreth | **Rejected.** It matches neither Marr–Hildreth nor pHash's `ph_mh_imagehash`. It is a plain 4-neighbour Laplacian sign scan original to this library, and the name is wrong. |
| BMH | Yang, Gu, Niu, IIH-MSP 2006 | **Confirmed** as the primary source. It specifies a median threshold; this implementation uses the mean. |
| Radial | Zauner on RASH, plus Lefèbvre/Macq and De Roover et al. 2005; pHash's `_ph_image_digest` as the reference implementation | **Confirmed and sharpened.** The implemented algorithm is De Roover et al. 2005; RASH (2002) is its superseded predecessor, which its own authors reported as troubled. Four defects follow. |
| ColorHash | Probably Buchner's implementation with no paper | **Confirmed.** ImageHash gives no reference at all for `colorhash`. |
| ColorMoments | Stricker & Orengo, SPIE 1995 | **Confirmed** as the source, though only a rank-4 restatement of it could be read. Formulas match; the colour space and the discarded skew sign do not. |

## Defects found, to be filed as separate tasks

Ordered by how strongly the primary source contradicts the code. The last is not a
contradiction of any source but of the code's own naming, and is listed because it
misled this analysis on its first pass.

1. **Radial: the DCT of the radial variance vector is missing, and `radial_projections`
   defaults to 40 angles instead of 180 angles reduced to 40 coefficients.** Contradicts
   De Roover et al. via Zauner §3.1.3. Changes every radial hash.
2. **Radial: digests are compared element-wise, so there is no rotation invariance**,
   while the source compares by peak of cross-correlation and `docs/algorithms.md`
   advertises rotation robustness up to 360°. Either implement PCC or withdraw the claim.
3. **BMH: the threshold is the mean of the block values, not their median.** Contradicts
   equation 3.9 of Yang, Gu and Niu. Changes every BMH hash.
4. **pHash: the DC coefficient is included in the median and in the hash bits.**
   Contradicts Zauner §3.2.1 and Krawetz/Starkweather independently. Changes every pHash
   value; note that it makes us agree with ImageHash, so the cross-check in
   `python-libphash` will start disagreeing when this is fixed — that is the expected
   outcome, not a regression.
5. **Radial: `PH_DEFAULT_GAMMA` is 2.2 where the pHash authors suggest 1.** Already
   filed separately; this document is the evidence for it.
6. **ColorMoments: the sign of the skewness is discarded.** Half of the third moment's
   information is thrown away.
7. **mHash is documented as a Marr–Hildreth hash and is not one**, and
   `docs/algorithms.md` additionally describes it as configurable through
   `ph_context_set_block_params`, which it ignores. Documentation and naming only; the
   maths is unaffected.
8. **BMH: no preset normalisation size**, so the block means are only true block means
   when the image dimensions are a multiple of the grid. Lower severity than the others.
9. **`ph_resize_lanczos()` does not use Lanczos.** It calls `stbir_resize_uint8_linear()`,
   which resolves an unspecified filter to stb's default — Mitchell when downscaling,
   Catmull-Rom when upscaling, point sampling at 1:1. Every use in this library is a
   downscale, so it is Mitchell throughout. No source specifies a filter, so no formula
   is contradicted, but the name asserts something false about aHash and dHash to anyone
   reading the code, and it is the reason the resampling was assumed to match ImageHash
   (which uses PIL's `LANCZOS`) when it does not. Rename it, or make it actually pass
   `STBIR_FILTER_MITCHELL`; do not silently change the filter, which would move every
   aHash and dHash value.

Items 1–5 all change stored hash values and therefore need a decision about whether they
belong in 2.0.0 at all. Item 9 changes none, as long as the fix is to the name.

---

# Verification methodology

Agreed 2 September 2026. This half of the document is a *decision*, not a finding: it
says what this project will treat as correct, and how it will check it. It exists so
that the next disagreement about a hash value has somewhere to be resolved.

## The criterion for a defect

A difference is a defect when it **contradicts a formula or step stated by the primary
source**, or when it **measurably worsens one of the properties below**.

A difference from a third-party implementation is not, on its own, a defect. ImageHash,
OpenCV and pHash are implementations; none of them is a specification, and none has been
verified against the papers it implements. The DC coefficient in pHash is the worked
example of why this matters: the code here agrees with ImageHash exactly and contradicts
both Zauner and Krawetz, so an ImageHash comparison could only ever have confirmed the
defect.

The corollary is uncomfortable and is accepted: fixing a defect will make this library
*disagree* more with ImageHash. That is the expected direction of travel.

For the three algorithms with no primary source — wHash, mHash, ColorHash — only the
second half of the criterion can ever apply. They are judged by measurable properties
alone, and the attribution headers say so rather than implying a specification exists.

## Checking formulas, not outputs

Where a source states a formula, the check is on the **intermediate quantity**, on
synthetic input whose correct answer is computed by hand or by an independent
definition. A mismatch there is unambiguous. A mismatch in the final bits of a hash of a
photograph is not: it could be the resampler, the grayscale weights, rounding, or the
algorithm.

Implemented as `tests/src/test_formula_conformance.c`:

- **DCT (pHash).** The type-II DCT matrix from Zauner's definition 3.3 is checked for
  orthonormality, and `ph_dct2_partial()` is checked against a direct O(N⁴) evaluation
  of the 2-D definition on a fixed pseudo-random 32×32 input, plus inputs whose
  transform is known in closed form (a constant image, where every AC coefficient is
  zero and DCT(0,0) = N·mean; and a single cosine at a known frequency, which must put
  all its energy in one coefficient).
- **Haar (wHash).** `ph_haar_1d_float()` is checked for orthonormality and against a
  step signal whose coefficients are known by hand, and the 2-D level is checked against
  a separable reference.
- **Block means (BMH).** On an input whose dimensions are an exact multiple of the grid,
  each output value must equal the arithmetic mean of its block, computed independently.
  This is the check that pins the "no preset normalisation size" gap: it can only pass
  on exact multiples, which is the point.
- **Colour moments.** Mean, standard deviation and skewness are checked against a
  distribution with hand-computed moments, including a deliberately skewed one where the
  third moment is negative — the case that the discarded sign destroys.

These tests are written against the *sources'* formulas. Where the code currently
contradicts a source, the test documents the contradiction with an explicit
`KNOWN DIVERGENCE` marker naming the task that will fix it, and asserts today's
behaviour so the fix is a visible, deliberate change rather than a silent one.

## Measurable properties

For everything a source does not specify, and for the three algorithms that have no
source, correctness is replaced by three measurable properties:

- **Robustness** — the same image after a benign transformation must hash close by.
- **Discrimination** — different images must hash far apart.
- **Separability** — the two distributions above must not overlap. This is the property
  that actually matters; either of the first two is trivially satisfiable alone (a
  constant hash is perfectly robust).

Separability is reported as the gap between the two distributions in units of their
spread, and a threshold is only ever set from a measurement, never chosen by eye — the
same rule as the benchmark methodology task.

## The corpus

A **generated synthetic corpus**, produced deterministically by a script in the
repository from a fixed seed. No external dataset, no manifest of URLs: it works with no
network, adds nothing to the repository's size, reproduces identically in CI and on a
developer's machine, and cannot rot.

The cost is accepted and stated here: synthetic images do not represent photographs. A
number measured on this corpus describes the algorithm's behaviour on the corpus. It is
suitable for detecting a regression and for comparing two implementations of the same
algorithm against each other — which is what these tests are for. It is **not** evidence
about real-world recall, and no such claim should be made from it.

## The boundary with `python-libphash`

Fixed by the earlier decision that the ImageHash comparison lives in the bindings
repository. Restating it in the terms above:

- **Here:** conformance to the primary sources, formula checks, and property
  measurements. These may fail the build.
- **There:** cross-checking against ImageHash, as a *signal* — useful for noticing that
  something moved, never a criterion for whether it should have.

When a defect from this document is fixed, the cross-check in `python-libphash` is
expected to start disagreeing. That disagreement is the fix working. The correct
response is to update the expectation there and record why, not to revert the fix.
