# References

The sources this library's algorithms are built on. Every hash in `libphash` is someone
else's algorithm; this file says whose, and where it is written down.

This is the bibliography. The analysis — what each source specifies, what this code
does, and where they differ — is in
[`algorithm-provenance.md`](algorithm-provenance.md). The two are meant to be read
together: this file is what we cite, that file is what we concluded from it.

## Reading other people's code, and the licences on it

Several algorithms here have no paper, so the only way to know what they are is to read an
implementation. Three appear throughout this document: **pHash** (GPL-3.0, with a separate
commercial licence), **OpenCV**'s `img_hash` module (Apache-2.0) and **ImageHash** (BSD).

This library is MIT and contains **no third-party hashing code**. Every hash in
`src/hashes/` is written from the specification; where the specification is somebody's
program, it is written from a description of that program's behaviour, not from its text.
Those projects are read the way a paper is read.

What is taken from them: what the method is, which constants it uses, what its defaults
are, what it does on an edge case. Those are facts, they carry no copyright, and each is
cited here precisely enough to be checked — file, function, and the parameter or the
behaviour in question.

What is not taken: source text, the structure of a function, or documentation prose. Where
this repository needs to state what another implementation does, it states it in words.
Short attributed quotations from *papers and theses* are ordinary scholarly citation and
appear throughout; quotations of *code* do not.

The vendored decoders under `vendor/` are a separate matter with their own licences and are
not covered here.


## How to read the rank column

| Rank | Meaning |
|---|---|
| 1 | Peer-reviewed paper or thesis by the algorithm's author |
| 2 | Technical report or preprint |
| 3 | Code or prose published by the algorithm's author (including a blog post) |
| 4 | Third-party implementation or restatement |

When two sources disagree, the lower rank wins. A rank-4 source is a hint about where to
look; it is never the basis for a claim that this code is correct.

**Read** says whether the source was retrieved and read in full. Several are behind IEEE
and SPIE paywalls; for those, the restatement actually used is named. Nothing in this
repository claims to follow a paper that nobody here has read without saying so.

The papers themselves are not committed. They are copyrighted by IEEE, SPIE and others,
and putting a PDF in a public repository redistributes it. Keep local copies in `papers/`,
which is git-ignored; this file holds the citation and the DOI so anyone can fetch their
own.

---

## Primary sources

### [Z10] Zauner 2010 — the most useful single source

Christoph Zauner, **"Implementation and Benchmarking of Perceptual Image Hash
Functions"**, Diplomarbeit, Upper Austria University of Applied Sciences, Hagenberg,
July 2010.

- PDF: <https://www.phash.org/docs/pubs/thesis_zauner.pdf>
- Also at the Internet Archive:
  <https://archive.org/details/thesis_zauner_Implementation_and_benchmarking_of_perceptual_image_hash_functions>
- Rank 1 · **Read in full**

Covers four of our algorithms and is the only source that documents both the theory and
what the pHash library actually computes, side by side. Sections used here: §3.1.1 (DCT
definitions), §3.1.2 (Laplacian and Marr–Hildreth), §3.1.3 (radial variance), §3.1.4
(block mean value, all four methods), §3.2.1–3.2.4 (pHash's implementations).

Used for: **pHash**, **BMH**, **Radial**, and for establishing what **mHash** is *not*.

### [K11] Krawetz 2011 — aHash, and a second description of pHash

Neal Krawetz, **"Looks Like It"**, The Hacker Factor Blog, 26 May 2011.

- <https://www.hackerfactor.com/blog/index.php?/archives/432-Looks-Like-It.html>
- Rank 3 · **Read in full** (via the Internet Archive; the live site returns HTTP 403 to
  automated clients)

A blog post, not a paper. There is no academic publication of aHash, and this is the
author's own description, which makes it the best source that exists. It also contains a
quotation from David Starkweather of pHash about the DCT hash excluding the DC term.

Used for: **aHash**, **pHash**.

### [K13] Krawetz 2013 — dHash

Neal Krawetz, **"Kind of Like That"**, The Hacker Factor Blog, 21 January 2013.

- <https://www.hackerfactor.com/blog/index.php?/archives/529-Kind-of-Like-That.html>
- Rank 3 · **Read in full** (same caveat as above)

Also a blog post. Note the attribution: the algorithm was proposed by **David Oftedal**
as a comment on [K11]; Krawetz named, described and evaluated it. Both names belong in
any attribution of dHash.

Used for: **dHash**.

### [CS04] Coskun & Sankur 2004 — origin of the DCT coefficient selection

B. Coskun and B. Sankur, **"Robust video hash extraction"**, *Proc. Signal Processing and
Communications Applications Conference*, IEEE, April 2004, pp. 292–295.

- Rank 1 · **Not read** — cited here as [Z10]'s reference [9], which is where the rule
  "select 64 low-frequency DCT coefficients, omitting the lowest frequency coefficients"
  comes from.

Used for: **pHash** (the provenance of the coefficient selection).

### [YGN06] Yang, Gu & Niu 2006 — BMH

Bian Yang, Fan Gu, Xiamu Niu, **"Block Mean Value Based Image Perceptual Hashing"**,
*Proc. International Conference on Intelligent Information Hiding and Multimedia Signal
Processing (IIH-MSP)*, IEEE, 2006, pp. 167–172. ISBN 0-7695-2745-0.

- <https://doi.org/10.1109/IIH-MSP.2006.265125>
- Rank 1 · **Not read** (IEEE paywall) — steps taken from [Z10] §3.1.4, which reproduces
  all four of the paper's methods and whose author implemented method 1 into pHash.

Used for: **BMH**. Specifies the median threshold that this code currently does not use.

### [DR05] De Roover et al. 2005 — the radial variance hash

C. De Roover, C. De Vleeschouwer, F. Lefèbvre, B. Macq, **"Robust image hashing based on
radial variance of pixels"**, *Proc. International Conference on Image Processing
(ICIP)*, vol. 3, IEEE, September 2005, pp. 77–80.

- <https://doi.org/10.1109/ICIP.2005.1530575>
- Rank 1 · **Not read** (IEEE paywall) — content from [Z10] §3.1.3 and §3.2.3, which
  states plainly that "pHash implements the algorithm as proposed in [35]", [35] being
  this paper.

Used for: **Radial**. This — not RASH below — is the algorithm pHash implements and the
one this library is trying to be.

### [LML02] Lefèbvre, Macq & Legat 2002 — RASH, the superseded predecessor

F. Lefèbvre, B. Macq, J.-D. Legat, **"RASh: RAdon Soft Hash algorithm"**, *Proc. European
Signal Processing Conference (EUSIPCO)*, vol. I, September 2002, pp. 299–302.

- Rank 1 · **Not read** — described in [Z10] §3.1.3.

Historical context only. Its own authors reported in 2005 that it "suffers from some
troubles" and replaced it with [DR05]. Cite it as background, never as the specification.

### [St05] Standaert et al. 2005 — evaluation of RASH

F.-X. Standaert, F. Lefèbvre, G. Rouvroy, B. Macq, J.-J. Quisquater, J.-D. Legat,
**"Practical evaluation of a radial soft hash algorithm"**, *Proc. International
Symposium on Information Technology: Coding and Computing (ITCC)*, vol. 2, IEEE, April
2005, pp. 89–94.

- <https://doi.org/10.1109/ITCC.2005.111>
- Rank 1 · **Not read** — described in [Z10] §3.1.3. Source of the discrete
  line-integral definition (the one-pixel-wide strip) quoted in the analysis.

### [SO95] Stricker & Orengo 1995 — colour moments

Markus Stricker, Markus Orengo, **"Similarity of color images"**, *Proc. SPIE 2420,
Storage and Retrieval for Image and Video Databases III*, 1995, pp. 381–392.

- <https://doi.org/10.1117/12.205308>
- Rank 1 · **Not read** (SPIE paywall) — and, unlike the papers above, **no rank-1
  restatement was found either**. The formulas used come from [Ke05] below, which is
  student coursework.

Used for: **ColorMoments**. This is the weakest evidence in our documentation and is
flagged as such wherever it is relied on.

### [Ke05] Keen 2005 — restatement of [SO95]

N. Keen, **"Color Moments"**, University of Edinburgh, CVonline course notes, 10 February
2005.

- <https://homepages.inf.ed.ac.uk/rbf/CVonline/LOCAL_COPIES/AV0405/KEEN/av_as2_nkeen.pdf>
- Rank 4 · **Read in full**

Gives the three moment formulas and the weighted-L1 distance attributed to [SO95]. Used
only because [SO95] itself could not be obtained. Any change made on the strength of
these formulas should be re-checked against the paper first.

### [DC20] Dolhansky & Canton Ferrer 2020 — why none of these resist an adversary

B. Dolhansky, C. Canton Ferrer, **"Adversarial collision attacks on image hashing
functions"**, arXiv:2011.09473, 2020.

- <https://arxiv.org/abs/2011.09473>
- Rank 2 · Abstract read

Cited for the threat-model note in [`algorithms.md`](algorithms.md). It produces exact
hash collisions between unrelated images through minimal, gradient-guided perturbations,
and does so "across numerous image pairs and hash types, encompassing both deep learning
and traditional hashing methods" — the authors point out that an attacker can thereby
"poison the image lookup table of a duplicate image detection service".

Worth citing precisely because it covers learned hashes too. Swapping a perceptual hash
for a neural embedding does not buy adversarial robustness; a self-supervised vision
backbone is not trained for it, and adversarial examples are that family's oldest known
failure mode.

### [MH80] Marr & Hildreth 1980 — cited only as a negative

D. Marr, E. Hildreth, **"Theory of edge detection"**, *Proceedings of the Royal Society of
London B*, 207(1167):187–217, February 1980.

- <https://doi.org/10.1098/rspb.1980.0020>
- Rank 1 · **Not read**

Listed here because this library's `mHash` was named after it and **is not an
implementation of it**. See [`algorithm-provenance.md`](algorithm-provenance.md) §5.
Cite this paper only if `mHash` is ever changed to actually compute a Laplacian of
Gaussian with zero-crossing detection.

### [VKJM00] Venkatesan et al. 2000 — related work, not our source

R. Venkatesan, S.-M. Koon, M. H. Jakubowski, P. Moulin, **"Robust Image Hashing"**, *Proc.
ICIP*, vol. 3, IEEE, 2000, pp. 664–666.

- <https://doi.org/10.1109/ICIP.2000.899541>
- Rank 1 · **Read in full**

Listed first to record a rejected hypothesis: this is the paper usually named as the
origin of "wHash", and it is not. It describes a *keyed* algorithm — `h = H(I, K)`, where
"the key is kept secret, and the hash value of a given image cannot be computed or
verified by an unauthorized party". Neither our wHash nor ImageHash's `whash` has a key.

Read in full because of a later question: could it replace our source-less wHash? What it
specifies is four steps —

1. a three-level Haar wavelet decomposition, each subband **randomly tiled into
   rectangles** under the key; averages of coefficients in the coarse subband, variances
   in the others, giving a length-*l* statistics vector;
2. **randomized rounding** of that vector to 3-bit values, `x = Q(m, K) ∈ {0..7}^l`;
3. decoding `x` with a **first-order Reed–Muller decoder** under an "exponential
   pseudo-norm" metric, giving the binary hash;
4. **"another decoding stage of a linear code with random parameters"**, shortening it
   further.

and what it does not specify is most of what an implementer needs: the tiling
distribution and rectangle count, the quantizer's step and distribution, the Reed–Muller
parameters, the pseudo-norm's parameter, and step 4 in its entirety. The paper says
outright that "a formal analysis of the steps involved in the hash computation will
appear elsewhere". It is three pages of ICIP proceedings, and it is a description, not a
specification.

Two further mismatches with what this library does. The randomization is not incidental —
"randomized rounding is the crucial source of randomness (or entropy) in the hash
function's output" — so fixing the key to a public constant to obtain determinism removes
the property the paper exists to demonstrate. And the intended comparison is **exact
equality** of the final hash ("we can compare two images by checking two bit strings for
exact equality"); the fuzzy, Hamming-distance comparison this library uses corresponds to
the paper's *intermediate* hash, which is the thing its experiments actually measure
(the "equality percentage").

Conclusion: citable as related work and as the origin of the idea. Not implementable as a
conformant algorithm — anything built from it would be "inspired by", which is precisely
the kind of claim [`algorithm-provenance.md`](algorithm-provenance.md) exists to keep out
of this repository.

---

## Reference implementations

Rank 4 throughout. These tell us what other people did. They do not tell us what is
correct, and where one of them disagrees with a primary source above, the source wins.

### [pHash] The pHash library

Evan Klinger, David Starkweather. <https://www.phash.org/>, source at
<https://github.com/aetilius/pHash>. GPL-3.0, with a separate commercial licence.

For the **DCT hash** this is not a reference implementation but the algorithm itself:
there is no paper behind it, so Klinger and Starkweather's code is the primary source and
everything written about it — including [Z10] §3.2.1 — is a third-party account. For
**Radial** it is the implementation of [DR05] that [Z10] §3.2.3 says it is, and the closest
thing available to that paywalled paper. It also carries the **Marr–Hildreth** hash.

Its own design page, <https://phash.org/docs/design.html> (© 2008–2010 Klinger &
Starkweather), states the radial hash as "the variances of 180 lines drawn through the
center of the image", compacted "with the discrete cosine transform", compared by
correlation with "a good threshold" of 0.91; the DCT hash as 64 bits matched at a Hamming
distance of 22; and the Marr–Hildreth hash as "a fixed length hash of 72 bytes". It cites
no papers.

The defaults are in the public header, `src/pHash.h.cmake`, and are worth quoting because
[Z10] gets one of them wrong:

- `ph_image_digest(..., int N = 180)` — 180 projections.
- `ph_crosscorr(..., double threshold = 0.90)` — the radial match threshold. The 0.91 on
  the design page is advice; 0.90 is the code.
- `ph_compare_images(..., double sigma = 3.5, double gamma = 1.0, ...)` — **σ = 3.5**, not
  the σ = 1 that [Z10] §3.2.3 reports as the authors' suggestion. γ = 1.0 does match.
- `ph_mh_imagehash(..., float alpha = 2.0f, float lvl = 1.0f)`.

There is **no block-mean hash** in pHash today, although [Z10] says its author contributed
one; see §6 of `algorithm-provenance.md`.

### [SB91] Swain & Ballard 1991 — colour indexing

M. J. Swain, D. H. Ballard, **"Color Indexing"**, *International Journal of Computer
Vision* 7(1):11–32, 1991. doi:10.1007/BF00130487.

The source for **ColorHash** since 2.0.0: a colour histogram over quantised opponent axes,
compared by histogram intersection.

**Not read.** IJCV is closed, OpenAlex reports `oa_status: closed` and no repository holds
the full text; Swain's Rochester technical report (TR 360, 1990) is not freely available
either. The method is taken from several independent secondary restatements, which by the
ranking in [`algorithm-provenance.md`](algorithm-provenance.md) is **rank 4**. Cite it as
*"a colour histogram with histogram intersection, after Swain & Ballard (1991), implemented
from secondary descriptions"* — never as a conformant implementation of the paper.

The paper's own quantisation (16×16×8 = 2048 bins) does not fit a `ph_digest_t`; the
resolution this library uses was chosen by measurement, and the table is in §8 of the
analysis.

### [CV] OpenCV `img_hash`

<https://github.com/opencv/opencv_contrib/tree/master/modules/img_hash>. Apache-2.0.

Carries the only widely used implementation of **BMH** ([YGN06]) — pHash has none. It is a
third-party implementation, rank 4, and §6 of
[`algorithm-provenance.md`](algorithm-provenance.md) follows the paper against it: it
normalises to 256×256 and then thresholds each block against the arithmetic **mean** of the
image, in a variable it names `median`. The paper specifies the median. Consequently this
library's BMH values differ from OpenCV's, deliberately.

### [IH] ImageHash

Johannes Buchner. <https://github.com/JohannesBuchner/imagehash>

The de facto reference for **aHash**, **dHash**, **pHash**, **wHash** and **ColorHash** in
the Python ecosystem, and the implementation this library has historically been compared
against. Its README cites [K11] for aHash and pHash, [K13] for dHash, a blog post for
wHash, and **nothing at all** for colorhash.

**It is the origin, not merely a comparison point, for two of our algorithms**: wHash and
ColorHash exist here because they exist there. For those two, ImageHash is the closest
thing to a specification, and the analysis document says so plainly rather than implying
an academic pedigree that does not exist.

### [Pe16] Petrov — the blog post behind wHash

Alexander Petrov, **"Wavelet image hash in Python"**.
<https://fullstackml.com/wavelet-image-hash-in-python-3504fdd282b5>

Rank 4. The only reference [IH] gives for `whash`. Recorded so that "wHash has no primary
source" is a checkable statement rather than an assertion.

---

## Which algorithm rests on what

| Algorithm | Primary source | Rank | Read directly | Reference implementation |
|---|---|---|---|---|
| aHash | [K11] | 3 | yes | [IH] |
| dHash | [K13] (algorithm by David Oftedal) | 3 | yes | [IH] |
| pHash | [Z10] §3.2.1, [K11]; origin [CS04] | 1 | [Z10] yes, [CS04] no | [pHash], [IH] |
| wHash | **none** — [VKJM00] read and rejected as a candidate | — | — | [IH], via [Pe16] |
| mHash | **none** — see [MH80] for the name it wrongly claims | — | — | none |
| BMH | [YGN06] | 1 | no — via [Z10] §3.1.4 | [pHash] |
| Radial | [DR05]; background [LML02], [St05] | 1 | no — via [Z10] §3.1.3, §3.2.3 | [pHash] |
| ColorHash | **none** | — | — | [IH] |
| ColorMoments | [SO95] | 1 | no — via [Ke05], rank 4 | none |

Three of the nine have no primary source at all. Per the verification methodology, those
three are judged only by measurable properties, and their attribution headers say so
instead of implying a specification exists.

## Adding to this file

If you implement a new algorithm, or change an existing one on the strength of a source:

1. Add the source here with its full citation, a resolvable link, its rank, and whether
   you read it or worked from a restatement. "I couldn't get the paper" is a fine thing
   to write down; pretending otherwise is not.
2. Add or update the algorithm's section in
   [`algorithm-provenance.md`](algorithm-provenance.md), including the delta table.
3. Put the citation in the header comment of the `src/hashes/*.c` file, so it is visible
   to whoever is reading the code rather than the docs.
4. If your change makes this library differ from a source deliberately, say why on the
   spot. A reason recorded in a commit message is a reason nobody will find.
