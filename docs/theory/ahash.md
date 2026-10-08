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

The comparison is exact — $64\,p_i \ge \sum_j p_j$ in integers — so a cell only ties with
the mean when it is equal to it, and a tie sets the bit.

![The 64 bits](../assets/generated/ahash/bits.light.svg#only-light){ width="420" }
![The 64 bits](../assets/generated/ahash/bits.dark.svg#only-dark){ width="420" }

**4. Pack the bits.** The first cell goes into the most significant bit:

$$
h = \sum_{i=0}^{63} b_i \cdot 2^{\,63-i} .
$$

### The mean and ties

The mean of 64 integers is seldom an integer itself, and comparing with it rounded would
not be the same hash: every cell equal to the rounded-down mean lies below the true one,
and would set its bit. On an image with little contrast, the cells take only a value or
two and that is most of the grid. Here is the image of the two corpora whose hash a
rounded-down mean would change the most:

![A synthetic image of green and magenta stripes, its 8×8 grid of 117s and 118s with the bits the exact mean sets, and the same grid with every bit set by the mean rounded down](../assets/generated/ahash/ties.light.svg#only-light)
![A synthetic image of green and magenta stripes, its 8×8 grid of 117s and 118s with the bits the exact mean sets, and the same grid with every bit set by the mean rounded down](../assets/generated/ahash/ties.dark.svg#only-dark)

The green and the magenta have almost the same luminance, and each cell averages several
stripes into one gray, 117 or 118. The exact mean lies between the two values and splits
them; rounded down, it would equal the lower one and set every bit.

The same happens without any rounding when an image is flat. Every cell then equals the
mean, every comparison is a tie, and the hash is `ffffffffffffffff`, whatever the image's
color. A detail finer than a cell averages out the same way, so a fine pattern hashes
like a flat gray: the synthetic corpus has two fine checkerboards that do, and they get the
same hash.

A mean does not split the bits in half either, as a median does: a dark image with a
bright corner sets few of them. Over the two corpora:

--8<-- "docs/assets/generated/ahash/ties.md"

On the photographs a rounded-down mean would change a few bits of many hashes and
separate copies from different images about as well; on the synthetic images, where
nearly flat grids are common, it would let many more different pairs through.
[wHash](whash.md#whash-and-ahash) thresholds the same grid at its median instead, which
sets exactly half of the bits unless values tie.

??? info "How this was measured"

    Every original of both corpora and its nine copies (those of the separability chart
    below) is hashed by `site_stages ahash-variants`, which computes the hash from the grid
    as in the steps above, checks it against
    [`ph_compute_ahash()`](../api/hash64.md#ph_compute_ahash), and thresholds the same grid
    at its mean rounded down. *d′* and the threshold are computed as for the corpus charts.
    The image in the figure is the one whose two hashes differ in the most bits.

    ```c title="tools/site/stages/ahash.c"
    --8<-- "tools/site/stages/ahash.c:ahash"
    ```

    ```c title="tools/site/stages/ahash.c"
    --8<-- "tools/site/stages/ahash.c:ahash-variants"
    ```

    ```python title="tools/site/measure/corpus.py"
    --8<-- "tools/site/measure/corpus.py:variants"
    ```

    ```python title="tools/site/measure/separability.py"
    --8<-- "tools/site/measure/separability.py:variants"
    ```

### Why an exact area average

The post says only "reduce size", and how the image is reduced decides what a cell is.
The library averages every pixel a cell covers, each weighted by how much of it the cell
covers. Here it is beside two other ways to reach 8×8 from the same grayscale image: the
Mitchell filter, which dHash reduces with and which weighs pixels by a smooth curve that
reaches past the cell's edges, and the nearest pixel, which keeps the one pixel at each
cell's center and drops the rest.

![The example photograph's 8×8 grid by an exact area average, by the Mitchell filter and by the nearest pixel, with the bits each sets and the bits that differ from the area average outlined](../assets/generated/ahash/reductions.light.svg#only-light)
![The example photograph's 8×8 grid by an exact area average, by the Mitchell filter and by the nearest pixel, with the bits each sets and the bits that differ from the area average outlined](../assets/generated/ahash/reductions.dark.svg#only-dark)

The three grids agree in their broad layout and part on the cells that lie near the
mean. Over the two corpora, with the copies and the different pairs of the separability
chart below:

--8<-- "docs/assets/generated/ahash/reductions.md"

- **The nearest pixel** lets 64 pixels decide the hash. A rotation, a crop, a downscale or
  a blur changes which pixels those are, and its copies spread so far that the threshold
  that keeps 95 % of them takes in many different pairs.
- **The Mitchell filter** moves copies of a photograph about as little as the area
  average, and separates different photographs a little less well. On the synthetic
  images it keeps part of the fine patterns that the area average cancels within a cell,
  and noise, blur and JPEG move what it keeps.
- **The exact area average** separates best on both corpora. It is also shared: pHash,
  wHash and BMH reduce from the same cached pass over the image.

??? info "The numbers behind the table"

    Mean bits that differ between an original and its copy, for each edit and reduction:

    --8<-- "docs/assets/generated/ahash/reductions-edits.md"

??? info "How this was measured"

    The same measurement as for the ties above, by `site_stages ahash-variants`: each
    original and its copies reduced the three ways, each grid thresholded at its own
    mean. The area grid is the library's (its hash is checked against
    [`ph_compute_ahash()`](../api/hash64.md#ph_compute_ahash)); the Mitchell grid is the
    library's own Mitchell resampling, the one dHash uses, and the nearest pixel is taken
    by the tool. The figure is `site_stages ahash` on the example photograph.

    ```c title="tools/site/stages/ahash.c"
    --8<-- "tools/site/stages/ahash.c:grids"
    ```

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
- **Gamma** bends the values unevenly. The order of the cells is kept almost intact, but
  the mean moves against them, and cells that lie near it cross. Over the photographs
  below, the strongest gamma moves aHash about as much as a rotation of a few degrees,
  and more than it moves [wHash](whash.md#whash-and-ahash), whose median depends only on
  the order.
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

    ```python title="tools/site/measure/transforms.py"
    --8<-- "tools/site/measure/transforms.py:transforms"
    ```

    Writing the copies and handing them to the measuring tool:

    ```python title="tools/site/measure/robustness.py"
    --8<-- "tools/site/measure/robustness.py:measure"
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

    ```python title="tools/site/measure/metric.py"
    --8<-- "tools/site/measure/metric.py:bits"
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

    ```python title="tools/site/measure/corpus.py"
    --8<-- "tools/site/measure/corpus.py:corpus"
    ```

    Comparing every pair of originals:

    ```c title="tools/site/stages/measure.c"
    --8<-- "tools/site/stages/measure.c:pairs"
    ```

    The edits that make a copy, and the numbers on the second chart:

    ```python title="tools/site/measure/separability.py"
    --8<-- "tools/site/measure/separability.py:copies"
    ```

    ```python title="tools/site/measure/separability.py"
    --8<-- "tools/site/measure/separability.py:separability"
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

    ```python title="tools/site/measure/transforms.py"
    --8<-- "tools/site/measure/transforms.py:content_edits"
    ```

Two hashes are built like aHash and measure themselves against it on their pages: wHash
thresholds the same 8×8 grid at its median ([wHash and aHash](whash.md#whash-and-ahash)),
BMH a 16×16 grid at its median ([block_size](bmh.md#block_size)). How the nine algorithms
compare is on [choosing an algorithm](../algorithms.md#comparison-summary).

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

    ```python title="tools/site/measure/timing.py"
    --8<-- "tools/site/measure/timing.py:timing"
    ```

## Settings that affect it

aHash has no parameters of its own: the grid is always 8×8. Four context settings change
the pixels it reads.

| Setting | Effect on aHash |
|---|---|
| [`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) | the grayscale formula of step 1 |
| [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale) | a native decoder converts to grayscale itself, which can move a value by one level |
| [`ph_context_set_auto_orient()`](../api/loading.md#ph_context_set_auto_orient) | on by default: the hash describes the image as displayed, after its EXIF rotation |
| [`ph_context_set_alpha_mode()`](../api/loading.md#ph_context_set_alpha_mode) | how transparent pixels are composited before grayscale |

A level here and there moves a cell by a fraction of a level, and can tip only a cell that
lies next to the mean.

--8<-- "docs/assets/generated/ahash/load-grayscale.md"

## In code

Hashing two images and counting the bits their hashes differ in, for aHash and the other
three 64-bit algorithms at once:

```c title="examples/hash_distance.c"
--8<-- "examples/hash_distance.c"
```

## Where it comes from

aHash was described by Neal Krawetz in a blog post in 2011; there is no paper. The post
fixes the 8×8 reduction, the grayscale step, the mean and the bit order, and leaves three
things open, which this implementation pins: the resampling filter (an
[exact area average](#why-an-exact-area-average)), the grayscale weights (BT.601) and the
rule for a cell equal to the mean (the bit is set, [compared exactly](#the-mean-and-ties)). With those, the implementation follows the post exactly, bit order included.
The comparison, with the numbers behind each choice, is in
[provenance § 1](../algorithm-provenance.md#1-ahash--average-hash).

--8<-- "docs/assets/generated/timing/footnote.md"
