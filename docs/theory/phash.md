# pHash — Perceptual Hash

Shrink the image to 32×32, describe it as a sum of cosine patterns with the discrete
cosine transform, and keep the 64 coarsest of them. Each bit records whether its pattern
is present more strongly than the typical one. Where aHash and dHash compare pixels,
pHash compares frequencies: the hash describes the large-scale layout of light and dark,
and the detail finer than that never reaches it.

| | |
|---|---|
| **Call** | [`ph_compute_phash()`](../api/hash64.md#ph_compute_phash), or [`PH_HASH_PHASH`](../api/hash64.md#PH_HASH_PHASH) in [`ph_compute_multi()`](../api/hash64.md#ph_compute_multi) |
| **Output** | 64-bit hash, compared with [`ph_hamming_distance()`](../api/compare.md#ph_hamming_distance) |
| **Source** | the pHash library by Evan Klinger and David Starkweather, as documented by Christoph Zauner, 2010 ([provenance](../algorithm-provenance.md#3-phash--dct-based-hash)) |
| **Cost** | 0.05 ms on a 400×400 image, 2.5 ms on 20 Mpx, after decoding |

## The steps

Every picture on this page is computed by the library from the same photograph, and the
hash at the end is what `ph_compute_phash()` returns for it.

![The five stages of pHash on the example photograph](../assets/generated/phash/pipeline.light.svg#only-light)
![The five stages of pHash on the example photograph](../assets/generated/phash/pipeline.dark.svg#only-dark)

**1. Grayscale.** Each pixel becomes one luminance value,

$$
Y = \left\lfloor \frac{38R + 75G + 15B}{128} \right\rfloor ,
$$

the same integer approximation of the ITU-R BT.601 weights as for every grayscale hash.
[`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) changes them.

**2. Reduce to 32×32.** The image is divided into a 32×32 grid of equal areas, regardless
of its aspect ratio, and each cell becomes the exact mean of the pixels it covers, rounded
to the nearest integer: the same area average as
[aHash's 8×8](ahash.md#the-steps), on a finer grid.

**3. Transform.** The two-dimensional type-II DCT rewrites the 32×32 values $X_{y,x}$ as
1024 coefficients $F_{u,v}$, one for each cosine pattern with $u$ half-periods down the
image and $v$ across it:

$$
F_{u,v} = c_u\, c_v \sum_{y=0}^{31} \sum_{x=0}^{31} X_{y,x}
\cos\frac{\pi u\,(2y+1)}{64} \cos\frac{\pi v\,(2x+1)}{64} ,
\qquad c_0 = \sqrt{\tfrac{1}{32}},\; c_{k>0} = \sqrt{\tfrac{2}{32}} .
$$

The scaling makes the transform orthonormal: the grid is exactly the sum of the 1024
patterns, each weighted by its coefficient. $F_{0,0}$, the *DC* coefficient, is the flat
pattern, $32$ times the mean brightness; every other one, an *AC* coefficient, is positive
where the image looks like its pattern and negative where it looks like the pattern's
negative. The library computes it as two passes of matrix products in single precision,
in a fixed order and with no vector path, so every build gives the same coefficients bit
for bit.

Here are the 64 patterns the hash keeps, $u$ and $v$ from 0 to 7. The top-left one is
flat; going right, the pattern alternates faster across the image, going down, faster
down it.

![The 64 cosine patterns of the 8×8 block: horizontal frequency 0 to 7 across, vertical frequency 0 to 7 down](../assets/generated/phash/basis.light.svg#only-light){ width="480" }
![The 64 cosine patterns of the 8×8 block: horizontal frequency 0 to 7 across, vertical frequency 0 to 7 down](../assets/generated/phash/basis.dark.svg#only-dark){ width="480" }

**4. Keep the low frequencies.** Of the 1024 coefficients, only the 8×8 block in the top
left corner goes into the hash, $F_{u,v}$ for $u, v = 0 \dots 7$. These are the patterns
with at most three and a half periods across the image: they say where the image is
light and where dark at the scale of its main shapes. On the example they are also the
largest coefficients, as the brighter corner of the map of all 1024 shows.

![The magnitude of all 32×32 DCT coefficients of the example on a log scale, with the 8×8 block outlined](../assets/generated/phash/dct-map.light.svg#only-light){ width="480" }
![The magnitude of all 32×32 DCT coefficients of the example on a log scale, with the 8×8 block outlined](../assets/generated/phash/dct-map.dark.svg#only-dark){ width="480" }

Recompression, noise and resizing change the image mostly in fine detail, which lives in
the coefficients outside the block. That is what pHash is built on.

**5. Threshold.** The threshold is the median $m$ of the 63 AC coefficients of the block,
raised by a tenth of a percent of their range:

$$
t = m + 0.001 \left( \max_{i \ge 1} F_i - \min_{i \ge 1} F_i \right) ,
\qquad
b_i = \begin{cases} 1 & F_i > t \\ 0 & F_i \le t \end{cases}
\qquad i = 0 \dots 63 ,
$$

with the block read row by row, $F_i = F_{\lfloor i/8 \rfloor,\, i \bmod 8}$. With 63
values the median is the 32nd smallest of them, so at most 31 AC bits are set: those
above the median, less any the margin catches.
DC takes part in the comparison but not in choosing $t$: it is the largest coefficient of
any ordinary image, so its bit is always 1. Why the margin, and why DC is left out of the
median but not out of the hash, is below.

![The 8×8 block of coefficients, with the cells above the threshold highlighted and DC outlined](../assets/generated/phash/block.light.svg#only-light){ width="480" }
![The 8×8 block of coefficients, with the cells above the threshold highlighted and DC outlined](../assets/generated/phash/block.dark.svg#only-dark){ width="480" }

**6. Pack the bits.** Coefficient $i$ sets bit $i$, so DC goes into the least
significant bit:

$$
h = \sum_{i=0}^{63} b_i \cdot 2^{\,i} .
$$

![The 64 bits](../assets/generated/phash/bits.light.svg#only-light){ width="420" }
![The 64 bits](../assets/generated/phash/bits.dark.svg#only-dark){ width="420" }

### Why a margin above the median

Every bit is a comparison with the median, so a coefficient that lies close to the median
is decided by whatever nudges it. On a photograph few do. On an image whose low
frequencies hold little — a smooth gradient, a regular pattern, a near-flat field — most
AC coefficients are close to zero, and so is their median: there, a bare median would
decide many bits by rounding. The margin moves the threshold just above that crowd and
sends all of it to 0 together.

Here are the 63 AC coefficients of the example and of the image of the tests' synthetic
corpus where they crowd the most, sorted, as distances from their median in units of
their range. The shaded band is the margin, and the coefficients in it are orange; the
scale is linear inside the band and logarithmic outside, so both the band and the spread
of the rest show.

![The 63 AC coefficients of two images sorted by value, as distances from their median, with the margin shaded](../assets/generated/phash/margin.light.svg#only-light)
![The 63 AC coefficients of two images sorted by value, as distances from their median, with the margin shaded](../assets/generated/phash/margin.dark.svg#only-dark)

--8<-- "docs/assets/generated/phash/margin-table.md"

On the photograph only the median itself lies in the band, and the margin changes no bit.
On the synthetic image nearly every coefficient does, and without the margin those bits
would be the float residue of a transform of zeros. Measured over 800 photographs, the
margin halves the share of different images that land within the distance that accepts
95 % of copies; the numbers, and the cost of it, are in
[provenance § 3](../algorithm-provenance.md#3-phash--dct-based-hash).

## Bit layout

Bit $i$ is coefficient $i$ of the block, row by row, from the least significant bit up.
In hexadecimal the hash therefore reads the block backwards: the last hex digit holds DC
and the next three coefficients of the top row, the first hex digit the last four of the
bottom row. Since the DC bit is set, the last hex digit is odd for any ordinary image.

![Which bit each coefficient of the block sets](../assets/generated/phash/bit-order.light.svg#only-light){ width="420" }
![Which bit each coefficient of the block sets](../assets/generated/phash/bit-order.dark.svg#only-dark){ width="420" }

## What changes the hash

The hash describes the coarse layout of light and dark. An edit that leaves the main
shapes where they are leaves the low frequencies, and the hash, alone; one that moves the
shapes moves every coefficient at once.

![Bits that differ from the original under nine transforms](../assets/generated/phash/robustness.light.svg#only-light)
![Bits that differ from the original under nine transforms](../assets/generated/phash/robustness.dark.svg#only-dark)

- **Recompression, resizing, blur and noise** act on fine detail, outside the block: the
  hash does not move, or moves by a bit at the strongest settings.
- **Brightness and contrast** scale the AC coefficients, and the median and the margin
  with them, so the bits stay until values start to clip at white. **Gamma** bends the
  values instead of scaling them, and moves a few bits.
- **Rotation and cropping** shift the shapes against the cosine patterns, and every
  coefficient changes with them. pHash follows them at least as steeply as aHash: it is
  not the algorithm for collections where images are rotated or reframed.

These are measurements on one photograph; the same edits over two whole corpora follow
below.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/phash/robustness-table.md"

??? info "How this was measured"

    Each transform is applied alone to the photograph above, and the edited copy is
    saved losslessly, as PPM (as a JPEG of the given quality for that panel). The library
    hashes the original and every copy, and compares each copy with the original by
    [`ph_similarity_digest()`](../api/compare.md#ph_similarity_digest). The chart shows
    $64 \times (1 - \text{similarity})$, which is the Hamming distance: how many of the
    64 bits the edit flipped. Below is the code that ran, not a copy of it.

    The edits, in Python with Pillow:

    ```python title="tools/site/transforms.py"
    --8<-- "tools/site/transforms.py:transforms"
    ```

    Writing the copies and handing them to the measuring tool:

    ```python title="tools/site/measure.py"
    --8<-- "tools/site/measure.py:measure"
    ```

    The measuring tool, linked against the library: it computes every algorithm's digest
    of the original, then of each copy, and compares the two by that algorithm's own
    metric.

    ```c title="tools/site/stages.c"
    --8<-- "tools/site/stages.c:measure"
    ```

    ```c title="tools/site/stages.c"
    --8<-- "tools/site/stages.c:compare"
    ```

    The number on the chart:

    ```python title="tools/site/measure.py"
    --8<-- "tools/site/measure.py:bits"
    ```

    To reproduce, `make site` builds the tool and redraws every figure. The tool can also
    be run by hand on any pair of images; it prints one JSON line per variant, with each
    algorithm's comparison:

    ```sh
    cmake --preset release && cmake --build --preset release --target site_stages
    build/release/site_stages measure reference.png variant.png …
    ```

### Over two corpora

The same nine edits, applied to every image of two corpora: 200 public-domain photographs
from Wikimedia Commons ([the list](../project/corpus.md)), and the 24 generated images the
library's tests measure ([methodology](../methodology.md#the-corpus)), which are not
photographs. Each line is the median over the corpus and the band around it holds the
middle half of its images.

![Bits that differ from the original under nine transforms, median and middle half over each corpus](../assets/generated/phash/robustness-corpus.light.svg#only-light)
![Bits that differ from the original under nine transforms, median and middle half over each corpus](../assets/generated/phash/robustness-corpus.dark.svg#only-dark)

On the photographs the picture is the single image's: rotation and cropping are the edits
that matter, and they move pHash's median further than aHash's; everything else moves it
by a few bits at most. The synthetic images are where pHash is weakest. Many of them are
regular patterns, checks, stripes and rings, whose block holds a few large coefficients
and many near zero, and there a degree of rotation, noise or heavy recompression moves
many bits.

### Copies and different images

Robustness alone proves little: a hash that never changes is perfectly robust. What makes
a hash useful is that copies of an image land closer together than different images do.
Here a *copy* is an original after one moderate edit (one strength of each of the nine,
listed under "How this was measured"), and *different images* are every pair of distinct
originals in the corpus.

![Distribution of bits that differ, for copies and for pairs of different images, in both corpora](../assets/generated/phash/separability.light.svg#only-light)
![Distribution of bits that differ, for copies and for pairs of different images, in both corpora](../assets/generated/phash/separability.dark.svg#only-dark)

Copies pile up at a few bits, different images spread around 32, half of the 64, where two
unrelated hashes are expected to fall. The dashed line is the distance that accepts 95 % of
the copies, and its label says how many different pairs it would accept as well. *d′* sums
up the gap in one number, the distance between the two means in units of their spread.

On photographs this is pHash's strength. Its copies sit a little further from the
original than aHash's, but different photographs land more tightly around 32 than with any of the other
64-bit hashes: the coarse layout of light and dark varies from photograph to photograph
the way independent bits would. The gap between copies and different images is the widest
of the four, and the threshold accepts the fewest pairs of different photographs. On the
synthetic images, where many hashes are decided by a few coefficients, the gap is the
narrowest of the four.

??? info "The numbers behind the charts"

    --8<-- "docs/assets/generated/phash/corpus-table.md"

??? info "How this was measured"

    For each corpus, every image goes through the nine edits above, and each copy is
    compared with its original exactly as on the single photograph. Every pair of
    distinct originals is compared too, by `site_stages pairs`. Below is the code that
    ran.

    The photographs are downloaded once and checked against the SHA-256 the manifest
    records for each:

    ```python title="tools/site/fetch_corpus.py"
    --8<-- "tools/site/fetch_corpus.py:fetch"
    ```

    Measuring a corpus, or taking the result from the cache when nothing that decides it
    has changed:

    ```python title="tools/site/corpus.py"
    --8<-- "tools/site/corpus.py:corpus"
    ```

    Comparing every pair of originals:

    ```c title="tools/site/stages.c"
    --8<-- "tools/site/stages.c:pairs"
    ```

    The edits that make a copy, and the numbers on the second chart:

    ```python title="tools/site/corpus_charts.py"
    --8<-- "tools/site/corpus_charts.py:copies"
    ```

    ```python title="tools/site/corpus_charts.py"
    --8<-- "tools/site/corpus_charts.py:separability"
    ```

    The synthetic corpus is generated by `make_base()` in
    `tests/src/synthetic_corpus.h`, the same code the tests use. `make site` measures
    both corpora and draws every figure.

How the nine algorithms compare is on
[choosing an algorithm](../algorithms.md#comparison-summary).

## Cost

pHash costs what aHash does, on a small image and on a large one
([measured](../algorithms.md#cost)). The 32×32 grid comes from the same cached area pass
over the grayscale image as aHash's, wHash's and BMH's, which is what grows with the
image; the transform that follows works on 1024 values whatever the image's size, and
computes only the 64 coefficients of the block, not all 1024.

## Parameters

[`ph_context_set_phash_params()`](../api/params.md#ph_context_set_phash_params) sets two
sizes:

| Parameter | Range | Default | Meaning |
|---|---|---|---|
| `dct_size` | `reduction_size` … 32 | 32 | the side of the grid of step 2 and of the transform |
| `reduction_size` | 4 … 8 | 8 | the side of the block of step 4; the hash has `reduction_size`² bits |

A smaller `dct_size` reduces the image to a coarser grid before the transform: it costs
less and sees less detail. A smaller `reduction_size` keeps fewer patterns: the threshold
is the median of that block's AC coefficients, and the bits go into the low
`reduction_size`² bits of the hash, row by row, the rest staying 0. Here is the example
at both ends of the range:

![The 4×4 and the 8×8 block of the example's coefficients, with the bits each sets and the resulting hash](../assets/generated/phash/reduction.light.svg#only-light)
![The 4×4 and the 8×8 block of the example's coefficients, with the bits each sets and the resulting hash](../assets/generated/phash/reduction.dark.svg#only-dark)

The 4×4 block is the top-left corner of the 8×8 one, but its median is its own and its
bits are packed four to a row, so the two hashes do not share a bit layout: hashes are
comparable only when computed with the same parameters. Below 4 the hash is degenerate:
over 400 photographs, a 3×3 block gives 70 distinct hashes and a 2×2 block 4, against 304
at 4 and 350 at 8, which is why 4 is the lower bound. Between 4 and 8, a smaller block
trades precision for a shorter hash.

## Settings that affect it

Four context settings change the pixels pHash reads.

| Setting | Effect on pHash |
|---|---|
| [`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) | the grayscale formula of step 1 |
| [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale) | a native decoder converts to grayscale itself, which can move a value by one level |
| [`ph_context_set_auto_orient()`](../api/loading.md#ph_context_set_auto_orient) | on by default: the hash describes the image as displayed, after its EXIF rotation |
| [`ph_context_set_alpha_mode()`](../api/loading.md#ph_context_set_alpha_mode) | how transparent pixels are composited before grayscale |

A level here and there in the 32×32 grid moves every coefficient a little, and can tip
only one that lies near the threshold.

--8<-- "docs/assets/generated/phash/load-grayscale.md"

## In code

Loading an image and printing its pHash, with the decoder's grayscale, since pHash reads
nothing else:

```c title="examples/basic_hash.c"
--8<-- "examples/basic_hash.c"
```

## Where it comes from

pHash has no paper. It is what the pHash library's `ph_dct_imagehash()` computes, written
by Evan Klinger and David Starkweather; the two careful descriptions of it, Christoph
Zauner's thesis (2010) and Neal Krawetz's "Looks Like It" (2011), are third parties
describing that code. Zauner traces the choice of 64 low-frequency coefficients to a DCT
video hash by Coskun and Sankur (2004). This implementation follows the code where it is
unambiguous: the 32×32 reduction, the type-II DCT with Zauner's matrix, the 8×8 block at
DCT(0,0), the median taken over the 63 AC coefficients only, and the strict `>`.

Both written descriptions say the block leaves out DC, starting at DCT(1,1). The code
does not: DC keeps its bit and loses only its vote on the median, and since it is always
above that median, its bit is always set, so the hash is effectively 63 bits wide.
Moving the block to (1,1) would give all 64 bits information; measured, it trades the
dead bit for coefficients that separate copies from different images worse, and the
block stays where pHash's code has it.

Three things differ from `ph_dct_imagehash()`. pHash smooths the image with a 7×7 mean
filter before resizing; the exact area average here is a low-pass step of the same kind.
The margin of step 5 is this library's own, so the hash values are not pHash's. And the
bit order, which no source specifies, is this library's choice. The comparison, with the
numbers behind each choice, is in
[provenance § 3](../algorithm-provenance.md#3-phash--dct-based-hash).
