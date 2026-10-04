# Algorithm provenance

Where each of the nine hashes in `libphash` comes from, what its source actually
specifies, what this implementation does, and where the two differ.

Agreement with ImageHash is not a specification: a difference from it is not evidence of
a bug, and agreement with it is not evidence of correctness. This document separates the
two.

## How to read this

Every algorithm gets the same five headings: **Author**, **Primary source**, **What
the source specifies**, **What this implementation does**, and **Delta**. Each row of a
Delta table is classified as one of:

- **conforms** — does what the source states.
- **matches pHash's code** (or **matches the reference implementation**) — follows the
  code that defines the algorithm where the prose descriptions of it disagree.
- **pinned** — the source leaves a choice open, and this library fixes it one way for a
  reason recorded here.
- **deliberate** — differs from the source, for a reason recorded here.
- **undefined** — the source does not say, so there is nothing to conform to.

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

Zauner's thesis is rank 4 for the DCT hash: it is one careful reader's account of Klinger
and Starkweather's program, and two of its statements about that program do not match it
(§3).

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
| Yang, Gu, Niu (IIH-MSP 2006) | Zauner's thesis §3.1.4, which reproduces all four methods step by step | thesis citing the paper (rank 4: a third-party account) |
| De Roover et al. (ICIP 2005); Lefèbvre et al. (EUSIPCO 2002); Standaert et al. (ITCC 2005) | Zauner's thesis §3.1.3 and §3.2.3 | thesis citing the papers (rank 4: a third-party account) |
| Stricker & Orengo (SPIE 1995) | N. Keen, *Color Moments*, University of Edinburgh CVonline course notes, 2005 | student coursework (rank 4) |

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
an area average (`ph_area_downscale()`), bit set when the pixel is at or above the exact mean of the 64
bytes (`pixel * 64 >= sum`, no rounding), bit index `63 - i` in row-major order — that is,
MSB first, left to right, top to bottom, big-endian.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| Resampling filter | undefined | Source says only "shrink". The reduction is an exact area average — each of the 64 values is the mean of the part of the image it covers — which is what "shrink" computes when nothing more is said. Nothing in the source is violated, and it is not the filter ImageHash uses (PIL `LANCZOS`). Against stb's Mitchell filter it measures better: separability 3.63 → 4.55 and false matches at 95 % recall 8.0 % → 2.9 % on the synthetic corpus; on 800 photographs 1208 → 1051 pairs of different images with identical hashes, the rest level. It is also shared: aHash, pHash, wHash and BMH read one area-average pass over the image. |
| Grayscale coefficients | pinned | Source says only "convert to a grayscale". `PH_GRAY_R/G/B` = 38/75/15 over 128 (`src/image/image.h`) — an integer approximation of the **ITU-R BT.601** luma coefficients (0.299/0.587/0.114), cited as an external standard because none of this library's nine primary sources define a grayscale formula at all. The 77/150/29-over-256 triple, closer to BT.601 in decimal, measures worse on the separability corpus — BMH 5.24 → 4.97, wHash 4.34 → 4.27, no gain elsewhere — so 38/75/15 is used. Affects every algorithm that reduces to grayscale — aHash, dHash, pHash, wHash, mHash, BMH, Radial (all seven that call `ph_get_gray()`); noted once here, cross-referenced from the others. |
| Ties (`pixel == mean` → 1) | pinned | "Above or below" leaves the tie unstated, and no reference implementation is cited here to defer to (contrast pHash/wHash below, which are). `>=` is the library-wide rule for an unpinned tie: it agrees with the one source that states a direction (Zauner eq. 3.9, for BMH). |
| Mean compared exactly, not rounded | conforms | "Above or below the mean" is a comparison with the mean itself. A mean truncated to an integer would move every pixel equal to `floor(mean)` — below a fractional mean — onto the tie and set its bit, which on a low-contrast image sets nearly all 64. |

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
`ph_resize_mitchell()`, stb_image_resize2's Mitchell filter, bit set when `row[col] < row[col+1]`, bit index `63 - (row*8 + col)`.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| Resampling filter | undefined | Source says only "shrink". Mitchell, not the area average aHash uses: dHash compares neighbouring pixels, and the sharp cell edges of an area average make those differences noisier. Measured with an area average, straight to 9×8 or through any intermediate grid from 72×64 to 288×256: separability down (3.49 → 3.23 synthetic) and more pairs of different photographs sharing a hash (721 → 900–1100 of 800 photographs). |
| Grayscale coefficients | pinned | As for aHash. |

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
  **Rank 4** for the construction (a third-party account of pHash's code), and the best
  description of it.
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
no say in the threshold; and thresholds all 64 against that median with a strict `>`.

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

**What this implementation does** (`src/hashes/phash.c`): grayscale, area-average resize to
32×32, type-II DCT by matrix multiplication with the same matrix definition as Zauner's
equation 3.3, coefficients (0,0) through (7,7), **median over the 63 AC coefficients**,
bit set when `value > median + 0.001 × (AC range)` — pHash's construction with a margin
(see the delta table and "Where the weakness is" below). The bit order is this library's
own: LSB first (see the bit-order table in [`algorithms.md`](algorithms.md)).

**Delta:**

| Difference | Class | Note |
|---|---|---|
| DC excluded from the median | matches pHash's code | As `ph_dct_imagehash()` does, and as both descriptions ask. It changes no bit except on an exact tie (measured below). |
| DC keeps its bit, so one bit of the 64 is constant | deliberate — pHash's own behaviour | DCT(0,0) is non-negative and larger than every AC term, so it is above the median every time and its bit is 1 every time. The hash is effectively 63 bits. Removing the dead bit means moving the block to (1,1), which the prose describes and the code does not; measured below and rejected. |
| No 7×7 mean prefilter before the resize | deliberate | `ph_resize_box()` already averages over each source region, which is a low-pass step of a similar kind. Not identical to a 7×7 mean at full resolution; worth measuring rather than assuming. |
| Threshold is the median | deliberate | The two sources disagree; the rank-1 source (Zauner/pHash) says median. |
| Threshold is the median plus 0.1 % of the AC range, not the bare median | deliberate | Coefficients crowding the median decide their bits by noise; the margin sends them to 0 together. Measured below: false matches at 95 % recall on photographs halve. `ph_dct_imagehash()` thresholds at the bare median, so pHash values differ from it. |
| `>` rather than `≥` | matches pHash's code | Zauner's 3.10 says `≥`; `ph_dct_imagehash()` writes `>`, and so does this. With floating-point coefficients the two differ only on an exact tie against the median, i.e. on degenerate input such as a solid colour. `>` because it is pinned to the reference implementation's code, which outranks a library-wide convention. |
| Box resampling | undefined | No source specifies a filter. Zauner's account of pHash has a 7×7 mean filter and then a resize, so a box filter is at least the same kind of operation. |
| Grayscale coefficients | pinned | As for aHash — see §1. `ph_median_bitpack_from()` (shared with wHash) operates on the DCT of the grayscale buffer, so the same BT.601 triple applies here too. |

**What the DC term actually costs, measured.** The received explanation — that including
DC "drags the median that decides the other 63 bits" — is false, and worth writing down
because it is repeated everywhere. A median is not dragged by an outlier. With the 64
values sorted ascending as v0..v63 and DC the largest, the median of all 64 is
(v31 + v32)/2 and the median of the 63 without DC is v31; nothing lies strictly between
them, so the same coefficients clear the threshold either way. The two can only disagree
when v31 and v32 are so close that the float average rounds onto v32, and then by one bit.

Measured: excluding DC from the median or not gives an identical pHash on every fixture in
`tests/data`, and identical separability (2.48) across the synthetic corpus.

Taking the 8×8 block at DCT(1,1) instead, so all 64 bits carry information, measures
worse:

| | mean intra | mean inter | separability |
|---|---|---|---|
| block at (0,0), median over AC (pHash) | 0.177 | 0.490 | **2.48** |
| block at (1,1), median over all 64 | 0.190 | 0.499 | 2.27 |

Trading the dead DC bit for one more row and column of higher-frequency coefficients buys
a bit of width and loses more robustness than it gains, so the block is at (0,0), as in
pHash.

Both measurements take the threshold at the bare median, without the margin described
below. The DC term does not explain pHash's weaker robustness there (mean intra-distance
0.177 against 0.03–0.07 for the other structural hashes): neither treatment of DC moves
that number.

**Where the weakness is: coefficients crowding the median.** Every bit is a comparison
with the median of the 63 AC coefficients, so a coefficient close to the median is decided
by whatever nudges it, and an image with many of them has many such bits. On an image with
little low-frequency structure most AC coefficients are near zero, and so is their median.
Measured over 800 photographs and textures (JPEG and PNG images shipped with macOS,
reduced to 512 px on the long side):

| AC coefficients within 0.1 % of the AC range of the median | photographs |
|---|---|
| under 5 % | 520 (65 %) |
| 5–25 % | 153 (19 %) |
| 25–50 % | 77 (10 %) |
| 50 % or more | 50 (6 %) |

The same effect seen through the hash: ±1 grey level of uniform noise, invisible, moves
pHash by 11 bits or more on 140 of the 800 (aHash 53, dHash 45, wHash 68). `photo_complex.png`
in `tests/data` is one of the 50: 55 of its 63 AC coefficients lie within 0.1 % of the
median. It and the uniform `photo.png` therefore pin pHash's behaviour in that regime, not in
the typical one.

So the threshold is not the bare median. It is raised by a margin of 0.1 % of the AC
range (`PH_PHASH_MEDIAN_MARGIN`), which sends the crowd around the median to 0 as a block:

| margin | photographs: separability | false matches at 95 % recall | moved 11+ bits by ±1 level of noise |
|---|---|---|---|
| none (pHash's `ph_dct_imagehash()`) | 3.67 | 14.2 % | 140 of 800 |
| **0.1 %** | **4.00** | **6.8 %** | **98** |
| 0.2 % | 4.01 | 6.5 % | 92 |
| 0.5 % | 3.77 | 8.2 % | 75 |

The synthetic corpus agrees (separability 2.69 → 3.17). A uniform image, whose AC
coefficients are float residue, hashes within 11 bits of zero instead of to rounding noise,
and any two flat greys within 13 bits of each other. The price is conformance: pHash values
are not `ph_dct_imagehash()`'s, and three near-flat synthetic images of 24 share a hash.

---

## 4. wHash — Wavelet hash

**Author:** Johannes Buchner, as `whash` in the ImageHash library.

**Primary source: none.** ImageHash's README cites a
[blog post](https://fullstackml.com/wavelet-image-hash-in-python-3504fdd282b5) by
Alexander Petrov, not a paper. No academic publication describes this construction.
Rank 4 is the best available, and the honest statement is that wHash has no primary
source.

**Not the source: Venkatesan et al. 2000.** Venkatesan, Koon, Jakubowski and
Moulin, "Robust Image Hashing", *ICIP 2000*, vol. 3, pp. 664–666, DOI
[10.1109/ICIP.2000.899541](https://doi.org/10.1109/ICIP.2000.899541), is a real paper and
is often named as the origin of wavelet-based image hashing. It is **not** the source of
this algorithm: it builds a hash from statistics of randomly tiled wavelet subbands under
a secret key, followed by error-correction decoding. None of that — keying, random tiling,
ECC — appears in ImageHash's `whash` or here.

It cannot serve as a source for wHash either, by replacing it with the paper's algorithm,
for reasons recorded in [`references.md`](references.md) under [VKJM00]. In short: the
paper specifies the shape of the algorithm but not the constants — the tiling
distribution, the quantizer, the Reed–Muller parameters and the whole of its fourth step
are absent, and it says so itself ("a formal analysis of the steps involved in the hash
computation will appear elsewhere"). Implementing it would mean inventing half of it and
then citing a paper for the result, which is the exact failure this document exists to
prevent. Its randomization is also load-bearing rather than incidental, so pinning the key
to obtain a deterministic hash discards what the paper demonstrates.

It belongs in a related-work list, not in an attribution header.

**wHash is an algorithm of this library**: an unkeyed Haar descriptor, justified by
measurement (separability 4.10, third of the nine) rather than by a citation. A keyed
algorithm would break determinism, which is the premise this library is built on. The gap
is not in the implementation; the literature does not write papers about the problem this
library solves.

**What the reference implementation does** (ImageHash `whash`, the thing this was ported
from, rank 4): grayscale; resize to `image_scale`, the largest power of two not
exceeding the smaller image dimension; divide by 255; Haar `wavedec2` to level
`log2(image_scale) − log2(hash_size)`; **with `remove_max_haar_ll=True` by default, zero
the LL coefficients of a full decomposition before the main one**; median of the
remaining `hash_size × hash_size` low band; bit set when `value > median`.

Note that `remove_max_haar_ll=True` cannot do for ImageHash what its name promises either,
for the same reason given in the delta table below: its working band is thresholded at a
median too.

**What this implementation does** (`src/hashes/whash.c`): two modes.

- `PH_WHASH_FAST` (default): box resize to a fixed 16×16, divide by 255, one Haar level
  (orthonormal, both sums and differences divided by √2), take the top-left 8×8, median,
  `>`.
- `PH_WHASH_FULL`: `image_scale` = largest power of two ≤ the smaller dimension, floored
  at 8; cascade Haar levels down to 8×8; same median and comparison.

**Delta** — against the reference implementation, since there is no source to conform to:

| Difference | Class | Note |
|---|---|---|
| `remove_max_haar_ll` implemented, defaults to off | deliberate (a proved identity) | The operation is the identity for a hash thresholded at the median, here and in ImageHash. Zeroing the single coarsest LL coefficient and reconstructing subtracts the image mean from every sample and nothing else (verified: max deviation 1.9e-07 against `orig − mean`, on a cascade whose round-trip error is 4.2e-07). A constant subtracted from every sample shifts every working-LL coefficient and their median by that same constant, so `value > median` is unchanged. Measured accordingly: all six real fixtures hash bit for bit identically in both modes. On the synthetic corpus 49 of 192 images do move, 536 bit flips in total, separability 4.34 → 3.43 — entirely tie-breaking noise, since bits move only where coefficients land exactly on the median (a disc with 34 such ties flips 2 bits; stripes, quadrants and noise have no ties and flip none), and that corpus is rich in the flat regions that produce ties while photographs produce none. Omitting it does not leave brightness in the hash: the +25 brightness row is 0.028 without the removal and 0.050 with it. Exposed as `ph_context_set_whash_remove_max_haar_ll()` for callers mirroring ImageHash's configuration; default off, because the only thing it can do is let rounding error decide ties. Pinned by `test_remove_max_haar_ll_subtracts_the_mean`, `test_remove_max_haar_ll_leaves_the_hash_alone` and `test_remove_max_haar_ll_on_a_solid_fill`. |
| Default mode fixes the scale at 16×16 | deliberate | `PH_WHASH_FULL` implements the power-of-two rule. Speed/robustness trade-off. |
| Box resampling, where the reference implementation resamples with PIL's `LANCZOS` | undefined | Neither ImageHash's own choice of `LANCZOS` nor this library's `ph_resize_box()` is asked for by anything upstream of ImageHash — there being no primary source for wHash at all (see above), there is nothing to conform to or diverge from, only a reference implementation to differ from by choice. Box resampling was picked for the same reason `ph_resize_box()` exists at all: cheap, and a defensible low-pass step ahead of a wavelet decomposition that is itself a filter bank. |
| Grayscale coefficients | pinned | As for aHash — see §1. |
| Tie (`value == median` → `>`) | matches the reference implementation | Same convention ImageHash's `whash` uses ("what the reference implementation does" above). Not changed to `≥` for the same reason as pHash's: an actual reference implementation to match beats a general convention adopted for everything that has neither. |

---

## 5. mHash — Marr–Hildreth hash

**Two sources, covering different halves of it.**

The **operator** is the Laplacian of Gaussian of D. Marr and E. Hildreth, "Theory of edge
detection", *Proc. R. Soc. Lond. B* 207:187–217, 1980 — rank 1. The kernel is the sampled
Mexican hat, `(2 − A)·exp(−A/2)`, with `A` the squared distance from the centre in units
of the scale.

The **hash** built on it has no paper. It is pHash's `ph_mh_imagehash()`, by Evan Klinger
and David Starkweather, and Zauner §3.2.2 says outright that the construction "has not
been proposed previously". By the ranking above, that makes pHash's own code the primary
source for the construction — not a reference implementation of something else — and the
steps and constants below are taken from it as facts about the algorithm.

**What the source specifies:** luminance, blur at σ = 1, resize to 512×512, histogram
equalisation over 256 levels, correlation with the LoG kernel at α = 2 and level = 1, the
response normalised and summed over 16×16 blocks into a 31×31 grid, and nine bits per 3×3
window of that grid at stride 4, each thresholded against its window's mean — 64 windows,
**576 bits, 72 bytes**. pHash's own page states the 72 bytes; its header states the α and
level defaults.

**What this implementation does** (`src/hashes/mhash.c`): that construction, with two differences in kind and one in arithmetic.

| Difference | Class | Note |
|---|---|---|
| Separable truncated Gaussian at σ = 1, not Deriche's recursive approximation | deliberate | CImg's `blur()` is a recursive filter; this is a direct kernel truncated at 3σ. Both approximate the same Gaussian. |
| Resize through stb's Mitchell filter (`ph_resize_mitchell()`), not CImg's quintic | deliberate | Different interpolator, same step. |
| Block sums computed through an integral image rather than by filtering every pixel | deliberate, and strictly better | See below. This is the one change that moves the numbers, and it moves them the right way. |
| Values are not bit-identical to pHash's | consequence of the three above | Reproducing them would mean reimplementing CImg's blur, resize and equaliser, for a comparison nothing here can run: there is no pHash build to check against. The construction and every parameter of it are reproduced; the arithmetic is not. |
| Zero-crossings are not detected | **not a divergence** | Worth stating because the name invites it: Marr and Hildreth find edges as the zero-crossings of the filtered image, and *neither* pHash nor this code looks for one. The response is block-summed and thresholded against a local mean. The operator is theirs; the edge detector is not being implemented, by either. |
| Grayscale coefficients (the "luminance" step) | pinned | As for aHash — see §1. |

### Folding the block sum into the kernel: faster, and more accurate

The definition filters every pixel and then sums the response over each 16×16 block. The
kernel tap does not depend on the pixel, so it comes out of the inner sum:

> B(by,bx) = Σ<sub>ky,kx</sub> K[ky][kx] · Σ<sub>(y,x) ∈ block</sub> I(y+ky−half, x+kx−half)

and what is left inside is a 16×16 box over the edge-replicated image, which an integral
image answers in four lookups. That is 31·31·289 multiply-adds instead of 496·496·289 —
**1.75 ms instead of 50 ms**, measured on a 400×400 JPEG.

The accuracy matters more than the speed. The LoG kernel sums to nearly zero, so
evaluating it per pixel in single precision is a sum of large products that almost
entirely cancel; the error survives into the block sum. Folded, the inner sums are exact
integers and only 289 terms accumulate, in double. Measured on the property corpus:
**separability 1.81 evaluating the definition directly in float, 2.49 this way.** The test
`test_mh_block_sums_match_the_direct_definition()` checks the folded result against the
definition evaluated in double.

One step of the source is dropped, provably without effect: it normalises the response to
[0,1] before summing. The block sums are affine in the response, the window mean is affine
in the block sums, and `value > mean` is invariant under an affine map with positive
scale, so no bit can change.

### Parameters, and why the defaults are pHash's

`ph_context_set_mhash_params()` exposes α and level — pHash's own two parameters, which
set the kernel's scale — and the size the image is normalised to, which pHash fixes at
512. The ratio between the kernel's scale and the picture is the only thing in this
algorithm that decides what it sees, and both knobs move it.

The defaults are pHash's (α = 2, level = 1, 512). Across 24 settings (4 levels × 6 sizes)
on a 300×300 corpus every one lands between 2.46 and 3.03, with no trend in either
parameter, so none beats the source's values. The sweep has to use a corpus larger than
the normalisation sizes: on a corpus near a normalisation size the base image is not
resampled at all while its transformed copies are, and the measurement rewards the
setting that happens to match the corpus.

Two properties follow:

- **This library's property corpus understates any algorithm that normalises to a size
  larger than the corpus** (less so since its feature sizes scale with the corpus; see
  "The corpus"). On a 300×300 corpus mHash separates at 2.70 (BMH 3.25, wHash 2.88,
  Radial 2.72, aHash 2.31, dHash 2.07, pHash 1.89, ColorHash 1.82); on the 160×160
  property corpus it separates at 2.62.
- **A small local edit moves this hash less than a rescale does.** A patch covering 4% of
  the frame measures 0.06–0.09 away at the default scale, where the benign transformations
  measure 0.12–0.17. For finding an edited copy of a picture, that ordering is the wrong
  way round; coarser scales partly repair it, inconsistently. The algorithm is a
  coarse-structure descriptor and should not be relied on to notice small edits.

---

## 6. BMH — Block Mean Value hash

**Author:** Bian Yang, Fan Gu, Xiamu Niu.

**Primary source:** "Block Mean Value Based Image Perceptual Hashing", *Proc.
International Conference on Intelligent Information Hiding and Multimedia Signal
Processing (IIH-MSP)*, IEEE, 2006, pp. 167–172, ISBN 0-7695-2745-0. Paywalled and not
read directly; the steps below are Zauner's §3.1.4 reproduction of the paper's method 1,
which he implemented into pHash as part of the thesis: a rank 4 restatement of a rank 1
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

**What this implementation does** (`src/hashes/bmh.c`): grayscale, an exact area average
(`ph_area_downscale()`) straight to `block_size × block_size` (default 16×16 → 256 bits), **median** of the
resulting values, bit set when `value >= median`, packed LSB-first within each byte.

There is no reference implementation to check the prose against, which is the situation
this document keeps running into from the other side. pHash carries no block-mean hash
today, although Zauner says he contributed one. The other implementation in wide use,
OpenCV's `cv::img_hash::BlockMeanHash`, resizes to 256×256 and then thresholds against the
arithmetic mean of the image — which it stores in a variable it names `median`. The name
says the intent and the value says the slip. This library follows the paper, so BMH values
differ from OpenCV's.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| Threshold is the median of the block means | conforms | Per step 4 and equation 3.9. It makes the bit distribution balanced by construction, which the paper depends on. On ordinary images the mean and the median of a block-value distribution sit close together, so a mean threshold would measure almost the same; the median is used for conformance and balance, not for a number. |
| Median of an even count taken as the upper of the two central values | undefined | The paper does not say. This is the choice that preserves its property: with `≥`, exactly half the blocks clear the upper central value. Ties among block values can still unbalance it — they are bytes, and a flat image has many — and nothing in the method addresses that. |
| No preset normalisation size; the image is resampled straight to the block grid | deliberate, and measured better | See below. |
| Key-permuted block order omitted | deliberate | Also omitted by pHash. It is a security feature (unpredictability under a key), not a perceptual one, and the paper leaves the cipher unspecified. |
| `≥` at the threshold | conforms | Matches equation 3.9. This is also the library-wide rule for aHash's own unpinned tie (§1). |
| Bit packing LSB-first within a byte | documented | The paper defines a bit sequence, not a byte layout, so there is nothing to conform to or diverge from — only a choice to record. See `docs/algorithms.md`'s bit-order table for all nine algorithms; `bmh.c`'s own file header states it too. |
| Grayscale coefficients | pinned | As for aHash — see §1. |

**The missing normalisation step, and why it is not needed.** Step (a) normalises the
image to a preset size before blocking, and both implementations of the paper do it at
256×256. This library resamples straight to the block grid. Resampling straight to the
grid is the block means for any source size, not only multiples of the grid:
`ph_area_downscale()` computes each block as the coverage-weighted sum of exactly the
source region behind it, fractional edges included, in integers, and rounds once. It
is the paper's block mean to the nearest byte, which
`test_area_downscale_matches_brute_force()` checks against a brute-force sum.

Normalising first, the paper's way — to the largest multiple of the grid at or below 256,
then averaging integer blocks — measures worse: separability on the synthetic corpus falls
from 5.24 to **5.11**, which is what an extra resampling stage
costs — the intermediate is rounded to integer pixels, so it adds error the direct area
average does not have. It would also tie the grid to divisors of the preset, and
most grid sizes this library accepts (2..32, e.g. 3×3 or 22×22) do not divide 256.

So the step is skipped deliberately, and the invariant that licenses skipping it is pinned
by `test_block_means_on_a_non_multiple()`.

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

**What this implementation does** (`src/hashes/radial.c`): a σ-parameterised
Gaussian blur (**default σ = 3.5**, pHash's own header default) and gamma correction with
a default of **1.0** — an exact identity, `pow(v/max, 1.0) * max == v` for any `max > 0` —
then for each of `radial_projections` angles (**default 180**) spread over [0, π), sample
`radial_samples` points (**default 128**) bilinearly along the line through the image
centre out to `min(w,h)/2` and compute
the variance with exactly the source's formula. That vector is standardised to zero mean
and unit variance, as pHash's `ph_feature_vector()` does; a vector with no spread worth
the name — mean variance at most 1e-6 grey levels², or a spread across angles below 1 % of
the mean (squared coefficient of variation 1e-4) — yields an all-zero digest, which
`ph_radial_similarity()` refuses to score (`PH_ERR_NO_STRUCTURE`). A 1-D DCT-II follows, and its **first 40 coefficients** are the hash,
mapped affinely onto 0–255 by their own minimum and maximum, the quantisation pHash's
`ph_dct()` uses. Digests are compared by `ph_radial_similarity()`, the peak of the
cross-correlation over cyclic shifts, against a threshold of 0.9 — pHash's
`ph_crosscorr()`.

Measured on the synthetic corpus of `tests/src/test_hash_properties.c` (distances
normalised to [0,1]), compared by peak cross-correlation: mean intra-distance 0.021, mean
inter-distance 0.261, separability **2.72**.

The vector is standardised before the transform. Without that, DCT coefficient 0 is the
sum of the variances: always the largest of the 40, always quantised to 255, so one byte
of every digest carries no information *and* pins the top of the quantisation range,
squeezing the rest into what is left — which pulls every pair of digests towards each
other.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| DCT of the radial variance vector, first 40 coefficients | conforms | The DCT of the 180-angle variance vector; the first 40 coefficients are the hash — the step the paper credits for the improvement. |
| Comparison by the peak of cross-correlation, threshold 0.9 | conforms, one deliberate difference | `ph_radial_similarity()`, pHash's `ph_crosscorr()`. One deliberate difference: pHash divides by the first digest's variance alone, which makes its score asymmetric — `crosscorr(x,y)` and `crosscorr(y,x)` disagree. This uses the symmetric Pearson correlation. |
| Rotation robustness is a few degrees, not arbitrary | a property of the algorithm | Measured below. The source is not contradicted; the tolerance is a few degrees plus a half turn. |
| Default gamma 1.0, pixels raised to `gamma` directly, normalised by the buffer's own maximum before the power step and rescaled by it after | matches pHash's code | Default γ = 1.0 (an exact identity), as `ph_compare_images()` defaults it. Pixels are raised to γ after normalising by the buffer's own maximum and rescaled by it, as pHash does. [Z10] reports 1 as the authors' suggestion; that is a suggestion the thesis records, not a formula from the paper. |
| Blur is σ-parameterised (`ph_gaussian_blur_sigma()`), default σ = 3.5 | matches pHash's code | pHash's own header defaults `sigma` to 3.5. [Z10] reports the authors as suggesting σ = 1, which pHash's header does not follow — so 3.5 is the reference *implementation's* choice, not a value derived from the paper. |
| 128 samples per projection, bilinearly interpolated | deliberate | The source integrates over the pixels of a one-pixel-wide strip, whose count varies with the angle and the image size; a fixed sample count is a different estimator of the same quantity. Cheaper and resolution-independent, but it is an approximation, not the definition. |
| Radius capped at `min(w,h)/2` | deliberate | Keeps every projection inside the image. The source does not normalise resolution and does not discuss the cap. |
| Grayscale coefficients | pinned | As for aHash — see §1. |
| Coefficients quantised by their own min and max | deliberate, and pHash's | Not in the paper, which says nothing about quantisation. It is what pHash's `ph_dct()` does, it keeps the sign, and it makes the digest invariant to a rescaling of the whole variance vector. |
| All-zero digest below a variance of 0.001 on every projection | pinned | Not in the source, and the source could not supply one — it does not discuss degenerate input. `0.001` is chosen, not inherited: small enough that no real image's projection variance falls under it (measured against the full test corpus), large enough to catch the residual floating-point noise a genuinely flat image leaves in `ph_projection_variance()`. Without it the min-max quantiser would stretch that noise across the whole byte range and manufacture detail that is not there. |

### What "robust to rotation" amounts to here, measured

The literature credits the radial variance hash with robustness to rotation, and it is
worth being precise about how much: not arbitrary rotation.

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
high-frequency synthetic corpus, where a one-degree resample already moves its narrow
stripes and small checkerboards, it is much weaker (mean 0.77 at 1°) — content matters,
and the corpus is the pessimistic end of it.

Quarter turns are not absorbed, and no comparison of these 40 coefficients can absorb
them: pHash's maximisation over cyclic shifts of the coefficients is not the group a
rotation acts through. pHash behaves identically. Recovering large rotations would mean
storing something a shift does not destroy — the magnitudes of the first Fourier
coefficients of the variance vector are exactly shift-invariant, for instance — which
would be a departure from the source with no defect to justify it. Not done, and not
planned.

Radial follows its source end to end: projections, standardisation, transform,
quantisation, comparison, and pHash's blur and gamma defaults. At the default γ = 1.0 the
gamma step is an exact identity (`(v/max)^1.0 * max == v`); the exponent convention and
the normalisation by the buffer's maximum matter only to a caller who sets γ explicitly.

---

## 8. ColorHash — colour histogram

**Authors:** Michael Swain and Dana Ballard, for the method. The quantisation is this
library's.

**Primary source:** M. Swain, D. Ballard, "Color Indexing", *International Journal of
Computer Vision* 7(1):11–32, 1991. **Not read.** IJCV is closed, OpenAlex reports
`oa_status: closed` and no repository holds the full text; Swain's Rochester technical
report (TR 360, 1990) is not freely available either. The intersection formula is
confirmed by several independent restatements, which by this document's own ranking leaves
the basis at **rank 4**. No conformance to the paper is claimed. The honest description,
used in the code and in `references.md` alike: *a colour histogram with histogram
intersection, after Swain & Ballard (1991), implemented from secondary descriptions.*

**What the secondary descriptions agree on:** quantise the colour space, count pixels per
bin, compare two histograms by their intersection

> H(t, r) = Σ min(t_i, r_i) / Σ r_i

which is 1.0 for identical distributions and falls towards 0 as they diverge. The property
it is taken for is that it counts only what the two images have in common, so a change of
background or a partial overlap costs only the part that differs. The axes the paper uses
are the opponent ones: `rg = R − G`, `by = 2B − R − G`, `wb = R + G + B`.

**What this implementation does** (`src/hashes/color_histogram.c`): those three axes at
6 × 6 × 3 = **108 bins**, one byte per bin scaled against the largest bin, compared by
`ph_histogram_intersection()` with each side normalised by its own total.

Measured separability: **3.95**, and 3.87 on a second corpus at a different resolution.

### Choosing the quantisation, since the paper cannot supply it

This is the measured-property regime, the same one wHash is in, and the measurement is the
justification. Sixteen candidates, on both corpora; the two agree throughout, which is
expected — a histogram does not resample the image, so the resolution confound that
distorts §5's numbers does not arise here.

| quantisation | bins | separability |
|---|---|---|
| RGB 3×3×3 | 27 | 2.74 |
| RGB 4×4×4 | 64 | 2.38 |
| RGB 5×5×5 | 125 | 2.02 |
| HSV 8×4×4 | 128 | 3.07 |
| HSV 12×3×3 | 108 | 2.90 |
| opponent 4×4×4 | 64 | 2.13 |
| opponent 5×5×4 | 100 | 2.48 |
| opponent 5×5×5 | 125 | 2.77 |
| opponent 6×4×4 | 96 | 2.45 |
| opponent 6×5×4 | 120 | 2.58 |
| opponent 6×6×2 | 72 | 2.69 |
| **opponent 6×6×3** | **108** | **3.95** |
| opponent 8×4×4 | 128 | 2.51 |
| opponent 9×9×1 | 81 | 3.94 |
| opponent 6×6×1 | 36 | **4.28** |

**The two highest scores were rejected.** `6×6×1` and `9×9×1` drop the light–dark axis,
which buys invariance to exposure — and makes a black image and a white image produce the
same hash, along with every other pair of flat greys. A corpus of colourful pictures never
notices; `test_color_hash_separates_flat_colours()` does, and exists so that a future
retuning cannot make that trade quietly. The corpus is never the only instrument: here its
best score is the wrong answer.

A perceptually spaced intensity axis measures worse than the uniform one (3.67, and *more*
grey collisions).

**What 6 × 6 × 3 still cannot separate**, from the same check: flat colours whose chroma
matches and whose total intensity falls in the same third — black against dark grey, light
grey against white. Three intensity bins is what fits beside 6 × 6 chroma inside
`PH_DIGEST_MAX_BYTES`, and chroma resolution is worth more here than intensity resolution
(5×5×5 has no such collisions and separates at 2.77). The limit is asserted as a limit in
the tests rather than left to be discovered.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| Quantisation is 6×6×3 of the opponent axes | this library's, justified by measurement | The paper's own resolution is 16×16×8 = 2048 bins, far past a 128-byte digest. |
| Bins scaled against the largest bin, not the pixel count | deliberate | With 108 bins the average bin holds under 1% of the image, which as a fraction of the total quantises to two or three of 255 levels. The comparison renormalises each side by its own sum, so nothing depends on the choice. |
| Intersection normalised by each side's own total | deliberate | The formula as stated normalises by the reference histogram, which makes the score asymmetric when the two images hold different pixel counts. A comparison that depends on the order of its arguments is a defect. The two agree whenever the counts match. |
| Spatial layout discarded entirely | inherent to the method | An image and a shuffling of its pixels hash identically, and a 90° rotation does not move the hash at all — asserted in the tests, since it is the property the algorithm exists for. Use it alongside a structural hash. |
| A small local edit barely moves it | inherent, measured | A patch covering 4% of the frame moves the distance by 0.03, where the benign transformations move it by 0.09. A global statistic notices a local change in proportion to its area. |

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
value is then written into an 18-byte digest as a **signed 16-bit big-endian fixed-point
number in units of 1/128**, tagged `PH_DIGEST_KIND_VECTOR16`.

**Delta:**

| Difference | Class | Note |
|---|---|---|
| Signed skewness kept | conforms | Skewness measures the *direction* of asymmetry, and its sign is half the information: stored as signed 16-bit fixed point, so mirror-image distributions differ. A 16-bit field rather than `skew + 128` in one byte also gives the finer resolution below. Pinned by `test_colour_moments_digest_keeps_the_skew_sign`. |
| Moments computed on RGB, not HSV | deliberate | The restatement specifies HSV and notes that "alternative encoding could just as easily be used"; this library uses RGB. Unverified against the paper itself (rank 4 source). |
| 16-bit fixed point, scale 128 | deliberate | The scale of 128 is the largest power of two for which the whole attainable range still encodes: over every distribution an 8-bit channel admits, the extremes are a mean of 255, a σ of 127.5 and a skewness of ±116.85 (two-point distributions are extremal for all three), so 255 is the largest magnitude any moment can take and 255 × 128 = 32640 ≤ 32767. Nothing clamps, and the resolution is 1/128 of a channel level instead of a whole one. A `_Static_assert` in `src/hashes/hashes.h` holds the scale to that bound. |
| Distance is L2 over the nine features, not the weighted L1 of the source | deliberate | The source leaves the weights to the application, so there is no defined default to conform to. `ph_l2_distance()` decodes the 16-bit pairs and computes the distance in the moments' own units; reading them as bytes would treat each feature's two halves as independent features, which is why `PH_DIGEST_KIND_VECTOR16` is a separate tag rather than a wider `PH_DIGEST_KIND_VECTOR`. |

---

# Verification methodology

This half of the document says what this project treats as correct and how it checks it,
so that a disagreement about a hash value has somewhere to be resolved.

## The premise: what problem this library solves

Everything below follows from one premise:
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

**What follows for the algorithms with no primary source.** wHash and
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
verified against the papers it implements. BMH is the worked example of why this
matters: OpenCV's widely used implementation thresholds on the mean
while the paper specifies the median, and a comparison against OpenCV could only have
confirmed the departure.

Conforming to a source can put this library at odds with ImageHash; that is accepted.
Cross-checking against ImageHash is a signal that something moved, never a criterion for
whether it should have; this repository's build gates only on source conformance, formula
checks and property measurements.

For the algorithm with no primary source — wHash — only the second half of the criterion
can ever apply. They are judged by measurable properties
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
- **Block means (BMH).** Each output value must equal the arithmetic mean of its block,
  computed independently: checked on exact multiples of the grid and, against an exact
  area-weighted mean, on non-multiples (`test_block_means_on_a_non_multiple`).
- **Colour moments.** Mean, standard deviation and skewness are checked against a
  distribution with hand-computed moments, including a deliberately skewed one where the
  third moment is negative — the case that needs the sign.

These tests are written against the *sources'* formulas. Where the code deliberately
departs from a source, the test says so in a comment and asserts the implemented
behaviour.

## Measurable properties

For everything a source does not specify, and for wHash, which has no source, correctness is replaced by three measurable properties:

- **Robustness** — the same image after a benign transformation must hash close by.
- **Discrimination** — different images must hash far apart.
- **Separability** — the two distributions above must not overlap. This is the property
  that actually matters; either of the first two is trivially satisfiable alone (a
  constant hash is perfectly robust).

Separability is reported as the gap between the two distributions in units of their
spread, and a threshold is only ever set from a measurement, never chosen by eye.

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

**Resolution.** Every feature size in `make_base()` — checkerboard cells, stripe widths,
ring periods, disc radii — is a fraction of the corpus resolution (`IMG_W`/`IMG_H`, via
`BASE_RES`), so resolution and structural fineness are independent knobs. The corpus is
160×160, deliberately not equal to any algorithm's normalisation preset (8 for
aHash/dHash, 16 for BMH's default `block_size`, 32 for pHash's default `dct_size`, 512 for
mHash). mHash still upsamples it 3.2× to reach 512, which understates it somewhat — a
corpus at or above 512 would not,
but `tests/src/test_hash_properties.c`'s radial-rotation assertions set a practical
ceiling on how large this corpus can go before an unrelated property (`ph_compute_radial_hash()`'s
fixed `PH_RADIAL_SAMPLES` sampling a fixed-size disc more coarsely) starts failing; see the
comment on `IMG_W` there. Numbers measured on this corpus are comparable within one run of
that file at one resolution, and nowhere else.
