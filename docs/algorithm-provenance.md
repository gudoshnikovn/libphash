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

The question a rank answers is **who defined this algorithm**, not what genre the
document belongs to. A thesis is not automatically above a source file; a thesis by the
algorithm's author is, and a thesis describing somebody else's program is a third-party
account of it. Applied when sources disagree, strongest first:

1. Peer-reviewed paper, thesis or technical report **by the algorithm's author**.
2. Preprint or unrefereed write-up by the author.
3. **Code published by the author** — which is the top rank whenever there is no paper,
   as for pHash's DCT hash, aHash and dHash, where the implementation *is* the algorithm.
4. Third-party description or implementation: ImageHash, OpenCV, a blog restatement, or a
   thesis analysing code the author of the thesis did not write.

Getting this wrong is not hypothetical. Zauner's thesis sat at rank 1 in an earlier draft
of this document because it is a thesis, and it is rank 4 for the DCT hash because it is
one careful reader's account of Klinger and Starkweather's program. Two of its statements
about that program turn out not to match it, and a task was written against one of them
before anyone checked (§3).

A third-party implementation is a hint about where to look, never itself the basis for a
claim of correctness — §6 follows the BMH paper against OpenCV, which is far more widely
used than this library. Where a paper could not be read directly (IEEE and SPIE
paywalls), the restatement used is named inline and its rank is stated, so the weight of
each claim stays visible.

### On reading other people's implementations

Several of the algorithms here are pinned down by reading code: pHash is GPL-3.0, OpenCV's
`img_hash` is Apache-2.0, ImageHash is BSD. This library is MIT and contains **no third-party
hashing code at all**. Those projects are read the way a paper is read — to establish what
the method is, what constants it uses and what its defaults are. Methods, parameter values
and defaults are facts; they carry no copyright, and they are quoted here with a precise
citation so a reader can check them.

What is *not* done: copying source, transcribing a function's structure, or reproducing
documentation prose. Where this document needs to say what another implementation does, it
says so in words. Every hash in `src/hashes/` is written from the specification, and where
the specification is somebody's program, from a description of that program's behaviour —
not from its text.

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
**median**. They agree, independently, that the **DC term is excluded** — but they do not
agree with pHash's code, which is the third thing in the room and the one that actually
defines the algorithm. `ph_dct_imagehash()` crops the 8×8 block at **(0,0)**, so the DC
term is among the 64 values; takes the median of **elements 1 through 63 only**, so DC has
no say in the threshold; and thresholds all 64 against that median with a strict `>`,
packing the bits most-significant first.

So DC keeps its bit and loses its vote. Zauner's "the coefficient DCT(1,1) being the
upper left corner" is his reading of that code, not what it does; Starkweather's "leaving
out the first DC term" describes the median and not the block.

The code decides here, and it is worth being exact about why, because the reason is not
"code beats prose". **This algorithm has no paper.** Its authors are Evan Klinger and
David Starkweather, and what they authored is the pHash implementation; there is nothing
behind it to appeal to. Zauner's thesis and Krawetz's post are third parties describing
someone else's program, however carefully. Where an algorithm *does* have a paper by its
author — Yang, Gu and Niu for BMH, Stricker and Orengo for the colour moments — the paper
outranks every implementation, including implementations more popular than this library
will ever be. §6 follows the BMH paper against OpenCV for exactly that reason.

**What this implementation does** (`src/hashes/phash.c`): grayscale, box resize to 32×32,
type-II DCT by matrix multiplication with the same matrix definition as Zauner's
equation 3.3, coefficients (0,0) through (7,7), **median over the 63 AC coefficients**,
bit set when `value > median` — pHash's construction exactly.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| DC excluded from the median | **fixed in 2.0.0** | Was included in it, which is neither what pHash's code does nor what either description asks for. See the measurement below: the effect turned out to be almost nothing, which is itself the result. |
| DC keeps its bit, so one bit of the 64 is constant | deliberate — pHash's own behaviour | DCT(0,0) is non-negative and larger than every AC term, so it is above the median every time and its bit is 1 every time. The hash is effectively 63 bits. Removing the dead bit means moving the block to (1,1), which the prose describes and the code does not; measured below and rejected. |
| No 7×7 mean prefilter before the resize | deliberate | `ph_resize_box()` already averages over each source region, which is a low-pass step of a similar kind. Not identical to a 7×7 mean at full resolution; worth measuring rather than assuming. |
| Threshold is the median | deliberate | The two sources disagree; the rank-1 source (Zauner/pHash) says median. |
| `>` rather than `≥` | matches pHash's code | Zauner's 3.10 says `≥`; `ph_dct_imagehash()` writes `>`, and so does this. With floating-point coefficients the two differ only on an exact tie against the median, i.e. on degenerate input such as a solid colour. |
| Box resampling | undefined | No source specifies a filter. Zauner's account of pHash has a 7×7 mean filter and then a resize, so a box filter is at least the same kind of operation. |

**What the DC term actually costs, measured.** The received explanation — that including
DC "drags the median that decides the other 63 bits" — is false, and worth writing down
because it is repeated everywhere. A median is not dragged by an outlier. With the 64
values sorted ascending as v0..v63 and DC the largest, the median of all 64 is
(v31 + v32)/2 and the median of the 63 without DC is v31; nothing lies strictly between
them, so the same coefficients clear the threshold either way. The two can only disagree
when v31 and v32 are so close that the float average rounds onto v32, and then by one bit.

Measured, not argued: after excluding DC from the median, the pHash value is **identical
on every fixture in `tests/data`**, and separability across the synthetic corpus is
unchanged at 2.48. The change is conformance, and its behavioural effect is a single bit
on tied input.

The other reading — take the 8×8 block at DCT(1,1), so all 64 bits carry information — was
implemented and measured too, because it is the only version of this fix that does
anything:

| | mean intra | mean inter | separability |
|---|---|---|---|
| block at (0,0), median over AC (pHash) | 0.177 | 0.490 | **2.48** |
| block at (1,1), median over all 64 | 0.190 | 0.499 | 2.27 |

It is worse. Trading the dead DC bit for one more row and column of higher-frequency
coefficients buys a bit of width and loses more robustness than it gains. So the block
stays at (0,0), matching pHash.

Which leaves the hypothesis this task was built on — that the DC coefficient is why pHash
has the worst robustness of the structural hashes here, mean intra-distance 0.177 against
0.03–0.07 — **refuted**. Neither treatment of DC moves that number. The cause is
elsewhere, and `tests/src/test_hash_properties.c` no longer suggests otherwise.

---

## 4. wHash — Wavelet hash

**Author:** Johannes Buchner, as `whash` in the ImageHash library.

**Primary source: none.** ImageHash's README cites a
[blog post](https://fullstackml.com/wavelet-image-hash-in-python-3504fdd282b5) by
Alexander Petrov, not a paper. No academic publication describes this construction.
Rank 4 is the best available, and the honest statement is that wHash has no primary
source.

**Hypothesis rejected, and the paper was then read.** Venkatesan, Koon, Jakubowski and
Moulin, "Robust Image Hashing", *ICIP 2000*, vol. 3, pp. 664–666, DOI
[10.1109/ICIP.2000.899541](https://doi.org/10.1109/ICIP.2000.899541), is a real paper and
is often named as the origin of wavelet-based image hashing. It is **not** the source of
this algorithm: it builds a hash from statistics of randomly tiled wavelet subbands under
a secret key, followed by error-correction decoding. None of that — keying, random tiling,
ECC — appears in ImageHash's `whash` or here.

It was later read in full to answer a different question: could it *replace* our
source-less wHash, so that the library would rest on a published algorithm? The answer is
no, for reasons recorded in [`references.md`](references.md) under [VKJM00]. In short: the
paper specifies the shape of the algorithm but not the constants — the tiling
distribution, the quantizer, the Reed–Muller parameters and the whole of its fourth step
are absent, and it says so itself ("a formal analysis of the steps involved in the hash
computation will appear elsewhere"). Implementing it would mean inventing half of it and
then citing a paper for the result, which is the exact failure this document exists to
prevent. Its randomization is also load-bearing rather than incidental, so pinning the key
to obtain a deterministic hash discards what the paper demonstrates.

It belongs in a related-work list, not in an attribution header.

**Decision (2 September 2026): keep the algorithm, name it honestly — and then improve it
on its own terms.** wHash stays an unkeyed Haar descriptor, described as an algorithm of
this library justified by measurement rather than as an implementation of anything. That
is a decision about *provenance*, not a freeze: the one substantive difference from the
reference implementation, the missing LL removal, is filed to be settled by measurement. Removing a working
descriptor for want of a citation would trade a measured 4.34 separability, the second
best of the nine, for nothing but tidier provenance. Adopting a keyed algorithm instead
would break determinism, which is the premise this library is built on. The gap here is
not in the implementation; it is that the literature does not write papers about the
problem this library actually solves.

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
| `remove_max_haar_ll` not implemented | **to be decided by measurement** | ImageHash removes the coarsest LL band so the hash describes local structure rather than overall brightness. Omitting it makes wHash more like aHash than intended — and the per-transform figures are consistent with that: wHash's mean distance under a +25 brightness shift is 0.008, so today's hash barely notices brightness leaving the LL band in. No source says which is correct, so this is settled the only way it can be: implement both, measure, keep the better, and record both numbers. Filed separately; the change was previously deferred only because it moves hash values, which 2.0.0 does anyway. |
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
straight to `block_size × block_size` (default 16×16 → 256 bits), **median** of the
resulting values, bit set when `value >= median`, packed LSB-first within each byte.

There is no reference implementation to check the prose against, which is the situation
this document keeps running into from the other side. pHash carries no block-mean hash
today, although Zauner says he contributed one. The other implementation in wide use,
OpenCV's `cv::img_hash::BlockMeanHash`, resizes to 256×256 and then thresholds against the
arithmetic mean of the image — which it stores in a variable it names `median`. The name
says the intent and the value says the slip, and it is very likely where this library's own mean came from. The paper is
followed here; expect BMH values to differ from OpenCV's.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| Threshold is the median of the block means | **fixed in 2.0.0** | Was the arithmetic mean, contradicting step 4 and equation 3.9. Measured on the synthetic corpus: separability 5.21 → 5.24, mean intra 0.031 → 0.035, mean inter 0.478 → 0.482. Barely moves, which is expected — on ordinary images the mean and the median of a block-value distribution sit close together. The change is for conformance and for the balance property the paper depends on, not for a number. |
| Median of an even count taken as the upper of the two central values | undefined | The paper does not say. This is the choice that preserves its property: with `≥`, exactly half the blocks clear the upper central value. Ties among block values can still unbalance it — they are bytes, and a flat image has many — and nothing in the method addresses that. |
| No preset normalisation size; the image is resampled straight to the block grid | deliberate, and measured better | See below. |
| Key-permuted block order omitted | deliberate | Also omitted by pHash. It is a security feature (unpredictability under a key), not a perceptual one, and the paper leaves the cipher unspecified. |
| `≥` at the threshold | conforms | Matches equation 3.9. |
| Bit packing LSB-first within a byte | undefined | The paper defines a bit sequence, not a byte layout. |

**The missing normalisation step, and why it stays missing.** Step (a) normalises the
image to a preset size before blocking, and both implementations of the paper do it at
256×256. This library resamples straight to the block grid. That was filed as a defect on
the reasoning that a resample to 16×16 equals the block means only when the source
dimensions are a multiple of 16 — which is true of a naive resampler and false of this
one. `ph_resize_box()` is stb_image_resize2 with `STBIR_FILTER_BOX`, whose support scales
with the ratio, so every output pixel is the coverage-weighted average of exactly the
source region behind it, fractional edges included.

Measured against the exact area-weighted block mean computed in double precision, the
largest deviation over 64 blocks is:

| source | 64×64 | 100×100 | 401×239 | 37×53 |
|---|---|---|---|---|
| max deviation from the exact block mean | 0.500 | 0.499 | 0.485 | 0.497 |

That is byte rounding, and it is no worse for the awkward sizes than for the exact
multiple. The one-step form already computes what the paper defines.

Doing it the paper's way was implemented and measured too: normalise to the largest
multiple of the grid at or below 256, then average integer blocks. Separability on the
synthetic corpus falls from 5.24 to **5.11**, which is what an extra resampling stage
costs — the intermediate is not an exact area average, so it adds error the direct box
resample does not have. It would also tie the grid to divisors of the preset, and the
maximum grid this library accepts, 22×22, does not divide 256.

So the step is skipped deliberately, the invariant that licenses skipping it is pinned by
`test_block_means_on_a_non_multiple()`, and the row above is not a defect.

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
for which [Z10] reports that "the authors suggest 1 for both variables" — though pHash's
own header defaults σ to **3.5** and γ to 1.0, so the thesis is right about γ and wrong
about σ; N = 180 angles by default;
**comparison by peak of cross-correlation (PCC)** with a default threshold of 0.9; and,
uniquely among the four hashes in the thesis, **no normalisation of image resolution**.

**What this implementation does** (`src/hashes/radial.c`), since 2.0.0: 3×3 Gaussian
blur, gamma correction with a default of **2.2**, then for each of `radial_projections`
angles (**default 180**) spread over [0, π), sample `radial_samples` points (**default
128**) bilinearly along the line through the image centre out to `min(w,h)/2` and compute
the variance with exactly the source's formula. That vector is standardised to zero mean
and unit variance, as pHash's `ph_feature_vector()` does; a vector with no spread — a flat
image, or one radially symmetric enough that every angle sees the same variance — yields
an all-zero digest. A 1-D DCT-II follows, and its **first 40 coefficients** are the hash,
mapped affinely onto 0–255 by their own minimum and maximum, the quantisation pHash's
`ph_dct()` uses. Digests are compared by `ph_radial_similarity()`, the peak of the
cross-correlation over cyclic shifts, against a threshold of 0.9 — pHash's
`ph_crosscorr()`.

Before 2.0.0 the transform was absent and the 40 was the number of angles. Fixing it
changed every radial hash. Measured on the synthetic corpus of
`tests/src/test_hash_properties.c`, mean intra-distance / mean inter-distance /
separability, distances normalised to [0,1]:

| | comparison | intra | inter | separability |
|---|---|---|---|---|
| 40 angles, no DCT | L2 | 0.021 | 0.344 | 2.80 |
| 180 angles, DCT-40 | L2 | 0.063 | 0.392 | 2.39 |
| 180 angles, DCT-40 | peak cross-correlation | 0.032 | 0.263 | **2.46** |

The last row is the algorithm as its source defines it, end to end, and it is the number
to quote. Robustness and discrimination both improve on the middle row; against the old
40-angle profile the standardised gap is slightly narrower, which is the price of
compressing 180 numbers into 40 and is what the source trades for decorrelation.

The standardisation before the transform is worth calling out, because leaving it out cost
more than it looks. Without it, DCT coefficient 0 is the sum of the variances: always the
largest of the 40, always quantised to 255, so one byte of every digest carries no
information *and* pins the top of the quantisation range, squeezing the rest into what is
left. The damage shows up in the comparison, where a byte every digest shares pulls every
pair towards each other — unrelated images averaged a correlation of 0.85, and the
separability under cross-correlation was 1.75 instead of 2.46.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| DCT of the radial variance vector, first 40 coefficients | **fixed in 2.0.0** | Was the algorithm's missing final step — the one the paper credits for the improvement. Now applied, over 180 angles, with the 40 back where the source puts it: the coefficient count. |
| Comparison by the peak of cross-correlation, threshold 0.9 | **fixed in 2.0.0** | `ph_radial_similarity()`, pHash's `ph_crosscorr()`. One deliberate difference: pHash divides by the first digest's variance alone, which makes its score asymmetric — `crosscorr(x,y)` and `crosscorr(y,x)` disagree. This uses the symmetric Pearson correlation. |
| Rotation robustness is a few degrees, not arbitrary | not a defect — a property of the algorithm | Measured below. The source is not contradicted; `docs/algorithms.md`'s former "unmatched robustness against rotation (up to 360°)" was this project's own wording and is withdrawn. |
| **Default gamma 2.2 where pHash defaults to 1.0** | **defect** | Already filed as its own task. `ph_compare_images()` defaults `gamma` to 1.0, and [Z10] agrees. Applying a 2.2 correction by default means the reference and this library see different pixel values before the variance is even computed. |
| Blur is a fixed 3×3 kernel, σ ≈ 0.707, where pHash defaults σ to 3.5 | **defect, larger than previously recorded** | [Z10] reports the authors as suggesting σ = 1, and that was what this document said; pHash's own header defaults `sigma` to **3.5**. Either way the kernel here is not σ-parameterised at all, and the gap to the reference is wider than a factor of one and a half. Belongs with the gamma task. |
| 128 samples per projection, bilinearly interpolated | deliberate | The source integrates over the pixels of a one-pixel-wide strip, whose count varies with the angle and the image size; a fixed sample count is a different estimator of the same quantity. Cheaper and resolution-independent, but it is an approximation, not the definition. |
| Radius capped at `min(w,h)/2` | deliberate | Keeps every projection inside the image. The source does not normalise resolution and does not discuss the cap. |
| Coefficients quantised by their own min and max | deliberate, and pHash's | Not in the paper, which says nothing about quantisation. It is what pHash's `ph_dct()` does, it keeps the sign, and it makes the digest invariant to a rescaling of the whole variance vector. The pre-DCT "normalise by the maximum, then take the square root" of earlier versions is gone: a square root before a transform is a different signal, not a scaling. |
| All-zero digest below a variance of 0.001 on every projection | undefined | Not in the source. Without it the min-max quantiser stretches the residue of a blank image across the whole byte range. The threshold predates 2.0.0 and is one of the constants pinned down separately. |
| 3×3 box-weight Gaussian, not a σ-parameterised one | undefined | The source has σ as a parameter; here the kernel is fixed. |

### What "robust to rotation" amounts to here, measured

The literature credits the radial variance hash with robustness to rotation, and it is
worth being precise about how much, because the wording this project used to carry —
"unmatched robustness against rotation (up to 360°)" — was never the source's and is not
true.

The mechanism is real and this implementation has it: a rotation cyclically shifts the
vector of per-angle variances. Measured directly on a synthetic image and its exact
quarter turn, the two 180-element variance vectors correlate at **0.9997 at a shift of
exactly 90 places** — the rotation is in there, cleanly and completely.

The hash is not that vector. It is 40 DCT coefficients of it, and the DCT is not
shift-equivariant: a cyclic shift of a signal is not a cyclic shift of its transform. So
what survives is a transform's tolerance to a small perturbation, not invariance to an
arbitrary rotation. Measured on `tests/data/photo.jpeg` with the source's comparison and
its 0.9 threshold, against 0.69 for an unrelated image:

| rotation | 1° | 2° | 3° | 5° | 10° | 15° | 90° | 180° |
|---|---|---|---|---|---|---|---|---|
| peak cross-correlation | 0.993 | 0.975 | 0.944 | 0.870 | 0.689 | 0.437 | 0.243 | 0.993 |

A few degrees — the kind a rescan, a crop-and-straighten or a re-encode introduces, and
the kind the perceptual-hashing literature evaluates — and an exact half turn. The half
turn is not the transform's doing: a projection line at α and at α+180 is the same line,
so it is the identity on the variance vector before the DCT ever runs. On the smoother
`photo_complex.png` the sweep holds out to 10° (0.939); on the deliberately
high-frequency synthetic corpus, where a one-degree resample already moves 3-pixel
stripes, it is much weaker (mean 0.76 at 1°) — content matters, and the corpus is the
pessimistic end of it.

Quarter turns are not absorbed, and no comparison of these 40 coefficients can absorb
them: pHash's maximisation over cyclic shifts of the coefficients is not the group a
rotation acts through. pHash behaves identically. Recovering large rotations would mean
storing something a shift does not destroy — the magnitudes of the first Fourier
coefficients of the variance vector are exactly shift-invariant, for instance — which
would be a departure from the source with no defect to justify it. Not done, and not
planned.

Radial now follows its source end to end: the projections, the standardisation, the
transform, the quantisation and the comparison. The one open divergence is the gamma
default.

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
   De Roover et al. via Zauner §3.1.3. Changes every radial hash. — **fixed in 2.0.0**;
   the before/after measurement is in §7.
2. **Radial: digests are compared element-wise, so there is no rotation invariance**,
   while the source compares by peak of cross-correlation and `docs/algorithms.md`
   advertises rotation robustness up to 360°. Either implement PCC or withdraw the claim.
   — **PCC implemented in 2.0.0** as `ph_radial_similarity()`. The "up to 360°" claim
   stays withdrawn, because it was never the source's: measurement puts the real figure at
   a few degrees plus an exact half turn, the DCT not being shift-equivariant. §7 has the
   numbers.
3. **BMH: the threshold is the mean of the block values, not their median.** Contradicts
   equation 3.9 of Yang, Gu and Niu. Changes every BMH hash.
4. **pHash: the DC coefficient is included in the median and in the hash bits.**
   Contradicts Zauner §3.2.1 and Krawetz/Starkweather independently. Changes every pHash
   value; note that it makes us agree with ImageHash, so the cross-check in
   `python-libphash` will start disagreeing when this is fixed — that is the expected
   outcome, not a regression. — **partly fixed in 2.0.0, and the rest deliberately not.**
   DC is out of the median, as pHash's code does it. It keeps its bit, as pHash's code
   also does. And the prediction here was wrong on both counts: no pHash value changes on
   any fixture, so ImageHash parity is *not* broken, and the DC term is not what hurts
   pHash's robustness. §3 has the numbers.
5. **Radial: `PH_DEFAULT_GAMMA` is 2.2 where pHash defaults to 1.0, and the blur kernel is
   fixed at σ ≈ 0.707 where pHash defaults to σ = 3.5.** Already
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

Items 1–5 all change stored hash values. The decision taken on 2 September 2026 is that
**all of them are fixed in 2.0.0**: a major release is the one cheap moment to move a hash
value, the breaking-changes list already carries one such move, and the golden hashes are
regenerated once for the lot. Item 9 changes no value at all, as long as the fix is to the
name.

All nine are filed as individual tasks, each with its own before/after measurement.

---

# Verification methodology

Agreed 2 September 2026. This half of the document is a *decision*, not a finding: it
says what this project will treat as correct, and how it will check it. It exists so
that the next disagreement about a hash value has somewhere to be resolved.

## The premise: what problem this library solves

Everything below follows from one decision, taken deliberately on 2 September 2026:
**`libphash` is a deduplication library for a collection its operator controls.** Finding
copies and near-copies, clustering, cache keys. There is no adversary in its threat model.

This is not a disclaimer bolted on at the end. It determines what "correct" can mean here,
and it resolves a question that otherwise looks like a gap in the library:

**Why the well-founded algorithms in the literature do not fit.** The rigorous,
peer-reviewed work on image hashing is about *authentication* — a perceptual analogue of a
message authentication code. Venkatesan et al. (2000) states the goal outright: "to make
hash values on a set of distinct inputs pairwise independent […] even when inputs are
generated by an adversary", and its hash is `H(I, K)` for a secret key `K`. Monga and
Evans (2006) likewise quantizes probabilistically under a key. In both, the key and the
randomization are load-bearing: Venkatesan calls randomized rounding "the crucial source
of randomness (or entropy) in the hash function's output".

A keyed hash is the wrong tool for deduplication, and not by a little. Deduplication needs
the same image to produce the same value on every machine that ever sees it, years apart,
with no shared secret — which is exactly the property a perceptual MAC is designed to
destroy. Taking such an algorithm and pinning its key to a public constant does not adapt
it; it removes the thing the paper proves and leaves an unvalidated feature extractor
wearing a citation. This repository does not do that.

**What follows for the three algorithms with no primary source.** wHash, mHash and
ColorHash are judged by measured properties rather than by conformance, and under this
premise that is not a compromise. There is no paper describing an unkeyed deterministic
wavelet hash because, in the security literature's terms, there is nothing to prove about
one: no unforgeability claim to make, no adversary to bound. What remains to establish is
that it is a good descriptor, and that is a measurement — which is what
`tests/src/test_hash_properties.c` does. Judging by measurement is the right instrument
here, not a fallback from a missing one.

**What this premise forbids.** No claim, anywhere in this repository, that any hash here
resists deliberate manipulation. `docs/algorithms.md` states the exclusion for users;
Dolhansky and Canton Ferrer (2020) is cited there for the attack, and it covers learned
hashes too, so the exclusion is not an argument for replacing these algorithms with neural
embeddings. If the threat model ever changes, this section is what has to be reopened
first — before any algorithm is chosen — because every choice below depends on it.

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
