# aHash — Average Hash

Shrink the image to 8×8, and record for each of the 64 cells whether it is brighter or
darker than the average. The simplest of the nine algorithms, and the cheapest: it shows
the whole idea of perceptual hashing in two lines of arithmetic.

| | |
|---|---|
| **Call** | [`ph_compute_ahash()`](../api/hash64.md#ph_compute_ahash), or [`PH_HASH_AHASH`](../api/hash64.md#PH_HASH_AHASH) in [`ph_compute_multi()`](../api/hash64.md#ph_compute_multi) |
| **Output** | 64-bit hash, compared with [`ph_hamming_distance()`](../api/compare.md#ph_hamming_distance) |
| **Source** | Neal Krawetz, "Looks Like It", 2011 ([provenance](../algorithm-provenance.md#1-ahash--average-hash)) |
--8<-- "docs/assets/generated/ahash/cost-row.md"

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

![Bits that differ from the original under nine transforms](../assets/generated/ahash/robustness.light.svg#only-light)
![Bits that differ from the original under nine transforms](../assets/generated/ahash/robustness.dark.svg#only-dark)

- **Recompression, resizing, noise and blur** leave the 8×8 averages where they were: the
  hash does not move until the image is blurred to a smear.
- **Brightness and contrast** scale every cell and the mean together. The order is kept,
  and the hash with it, until values start to clip at white.
- **Rotation and cropping** move content from one cell to another, and the hash follows
  them: a 5° turn or a 10 % crop already moves a handful of bits. aHash is not the
  algorithm for collections where images are rotated or reframed.

These are measurements on one photograph; the same edits over two whole corpora follow
below.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/ahash/robustness-table.md"

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

    ```c title="tools/site/stages/measure.c"
    --8<-- "tools/site/stages/measure.c:measure"
    ```

    ```c title="tools/site/stages/measure.c"
    --8<-- "tools/site/stages/measure.c:compare"
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

![Bits that differ from the original under nine transforms, median and middle half over each corpus](../assets/generated/ahash/robustness-corpus.light.svg#only-light)
![Bits that differ from the original under nine transforms, median and middle half over each corpus](../assets/generated/ahash/robustness-corpus.dark.svg#only-dark)

The photographs say what the single example says: rotation and cropping are the edits
that move aHash, and everything else moves its median by a few bits at most. The
synthetic images spread wider, as their bands show: they are made of hard edges and fine
patterns, where a small shift moves a whole stripe from one cell into the next.

### Copies and different images

Robustness alone proves little: a hash that never changes is perfectly robust. What makes
a hash useful is that copies of an image land closer together than different images do.
Here a *copy* is an original after one moderate edit (one strength of each of the nine,
listed under "How this was measured"), and *different images* are every pair of distinct
originals in the corpus.

![Distribution of bits that differ, for copies and for pairs of different images, in both corpora](../assets/generated/ahash/separability.light.svg#only-light)
![Distribution of bits that differ, for copies and for pairs of different images, in both corpora](../assets/generated/ahash/separability.dark.svg#only-dark)

Copies pile up at a few bits, different images spread around 32, half of the 64, where two
unrelated hashes are expected to fall. The dashed line is the distance that accepts 95 % of
the copies, and its label says how many different pairs it would accept as well: that is
the price of the threshold. *d′* sums up the gap in one number, the distance between the
two means in units of their spread: above about 1 the distributions are usefully apart.
On the photographs the two barely touch; the synthetic images, built to be hard, overlap
more.

??? info "The numbers behind the charts"

    --8<-- "docs/assets/generated/ahash/corpus-table.md"

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

    ```c title="tools/site/stages/measure.c"
    --8<-- "tools/site/stages/measure.c:pairs"
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

### Edits that change the picture

The edits above are those a copy goes through. The ones here change what the picture
shows: a patch of the image's own top-left corner pasted over its center, covering 1 to
16 % of the frame; the hue of every pixel turned, which recolors the picture; and a
quarter turn, a half turn and a mirror image. Whether a hash should notice them depends
on what it is for: a search for copies wants to ignore a recoloring, a search for
retouched pictures wants to catch the patch.

![The example photograph with a patch over 4 and 16 % of the frame, its hue turned by 90 and 180 degrees, turned by 90 and 180 degrees, and mirrored](../assets/generated/edits/examples.light.svg#only-light)
![The example photograph with a patch over 4 and 16 % of the frame, its hue turned by 90 and 180 degrees, turned by 90 and 180 degrees, and mirrored](../assets/generated/edits/examples.dark.svg#only-dark)

Each panel shows how far the edited images land from their originals over both corpora.
The dashed line is the threshold of the chart above, the one that accepts 95 % of the
copies, in each corpus's color: an edited image below it would be taken for a copy.

![Bits that differ from the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/ahash/edits.light.svg#only-light)
![Bits that differ from the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/ahash/edits.dark.svg#only-dark)

- **A patch** changes only the cells it covers, and moves the hash in proportion to its
  area: over 4 % of the frame most edited photographs still pass for copies, over 16 %
  most do not.
- **Turning the hue** hardly moves the hash on photographs. A hue rotation keeps each
  pixel's saturation and its brightest channel, and on the muted colors of most
  photographs the luminance barely changes. The synthetic images are flat, saturated
  colors, whose luminance a hue rotation changes a great deal, and there the hash moves
  as far as for an unrelated image.
- **A quarter or half turn** moves the hash as far as an unrelated image. A mirror image
  moves it about half as far: it reverses the layout from left to right and keeps it
  from top to bottom.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/ahash/edits-table.md"

??? info "How this was measured"

    The same measurement as for the copies, over the same images: each edit is applied
    alone to the original, the copy is saved losslessly, and the library compares it with
    the original. The threshold is the one of the chart above, computed from the copies.
    Below is the code of the edits.

    ```python title="tools/site/transforms.py"
    --8<-- "tools/site/transforms.py:content_edits"
    ```

How the nine algorithms compare is on
[choosing an algorithm](../algorithms.md#comparison-summary).

## Cost

aHash costs a grayscale conversion and one area-average pass over the image, which read
every pixel and so grow with it; the 64 comparisons that follow do not (the times are in
the table at the top of the page). The area pass is cached: pHash, wHash and BMH reduce
from the same grid.

??? info "How this was measured"

    --8<-- "docs/assets/generated/ahash/timing-table.md"

    Each case runs once to warm up, then until it has run at least five times and for at
    least a second; the time is the minimum. The grayscale image and the area grid the
    library caches are dropped before every run, so each run is the first hash on a
    loaded image. The cases, and the timing loop:

    ```c title="tools/site/stages/timing.c"
    --8<-- "tools/site/stages/timing.c:time"
    ```

    The times are measured again only when the library, the tool or the machine changes:

    ```python title="tools/site/timing.py"
    --8<-- "tools/site/timing.py:timing"
    ```

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

--8<-- "docs/assets/generated/timing/footnote.md"
