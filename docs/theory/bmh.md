# BMH — Block Mean Hash

Divide the image into a 16×16 grid of blocks, take the mean of each, and record for each
block whether it is at or above the median of the 256 means. The idea is aHash's on a
grid four times as fine, cut at the median instead of the mean: the digest describes
where the image is light and where dark, at a finer scale, in 256 bits instead of 64.

| | |
|---|---|
| **Call** | [`ph_compute_bmh()`](../api/digests.md#ph_compute_bmh), or [`PH_ALGO_BMH`](../api/algorithms.md#PH_ALGO_BMH) in [`ph_compute_digest()`](../api/algorithms.md#ph_compute_digest) |
| **Output** | digest of `block_size²` bits, 256 at the default 16×16, compared with [`ph_hamming_distance_digest()`](../api/compare.md#ph_hamming_distance_digest) |
| **Source** | Bian Yang, Fan Gu and Xiamu Niu, "Block Mean Value Based Image Perceptual Hashing", 2006, method 1 ([provenance](../algorithm-provenance.md#6-bmh--block-mean-value-hash)) |
--8<-- "docs/assets/generated/bmh/cost-row.md"

## The steps

The steps below are those of the default grid, 16×16;
[`ph_context_set_block_params()`](../api/params.md#ph_context_set_block_params) sets
another, [below](#parameters). Every picture on this page is computed by the library from
the same photograph, and the digest at the end is what `ph_compute_bmh()` returns for it.

![The four stages of BMH on the example photograph](../assets/generated/bmh/pipeline.light.svg#only-light)
![The four stages of BMH on the example photograph](../assets/generated/bmh/pipeline.dark.svg#only-dark)

**1. Grayscale.** Each pixel becomes one luminance value,

$$
Y = \left\lfloor \frac{38R + 75G + 15B}{128} \right\rfloor ,
$$

the same integer approximation of the ITU-R BT.601 weights as for every grayscale hash.
[`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) changes them.

**2. Block means.** The image is divided into a 16×16 grid of equal areas, regardless of
its aspect ratio, and each block becomes the exact mean of the pixels it covers, rounded
to the nearest integer: the same area average as [aHash's 8×8](ahash.md#the-steps). A
pixel cut by a block boundary counts in each block by the fraction it covers, so the
means are exact for any image size, not only for a multiple of 16. With the blocks read
row by row, $M_0 \dots M_{255}$ are whole numbers from 0 to 255.

**3. The median.** The means are sorted, and the median $m$ is the 129th smallest,
$m = M_{(128)}$ counting from $M_{(0)}$: of the two central values of an even count, the
upper one. Since the means are bytes, the library counts how many blocks take each of the
256 values and walks the counts, rather than sorting.

**4. Threshold.** A bit is set where its block is at or above the median:

$$
b_i = \begin{cases} 1 & M_i \ge m \\ 0 & M_i < m \end{cases}
\qquad i = 0 \dots 255 .
$$

Unless blocks tie at the median, exactly 128 bits are set: the 128 blocks from the upper
central value up. Any image gives a digest with as many ones as zeros, which is the
property the paper's threshold is chosen for.

![The 16×16 block means of the example, with the blocks at or above the median highlighted](../assets/generated/bmh/grid.light.svg#only-light){ width="560" }
![The 16×16 block means of the example, with the blocks at or above the median highlighted](../assets/generated/bmh/grid.dark.svg#only-dark){ width="560" }

**5. Pack the bits.** Block $i$ sets bit $i \bmod 8$ of byte $\lfloor i/8 \rfloor$:

$$
\text{byte}_k = \sum_{j=0}^{7} b_{8k+j} \cdot 2^{\,j} \qquad k = 0 \dots 31 .
$$

### The median and ties

The paper thresholds at the median of the block means, where aHash takes their mean. On
most photographs the two sit close, but where they part they describe different things.
Here is the photograph of the corpus whose mean threshold sets the fewest or the most
bits, with both:

![A photograph of two swans against a blue sky, the distribution of its 256 block means with the median and the mean, and the bits each threshold sets, with the blocks where they differ outlined](../assets/generated/bmh/median-mean.light.svg#only-light)
![A photograph of two swans against a blue sky, the distribution of its 256 block means with the median and the mean, and the bits each threshold sets, with the blocks where they differ outlined](../assets/generated/bmh/median-mean.dark.svg#only-dark)

The sky is most of the frame and most of the blocks. The mean, pulled up by the few bright
blocks of the birds, marks the birds; the median has to put half of the blocks on each
side, so it cuts the sky along its gradient. Neither is wrong: the mean describes the
object against its background, the median the order of the blocks, which a tone curve
does not change.

Blocks equal to the median all set their bit, so ties tip the balance upward. They are
bytes, and an image with large flat areas gives many equal means. In the extreme, when
more than half of the blocks have the image's lowest value, the median is that value and
every block is at or above it: all 256 bits are set, whatever the rest of the image
shows. A product photographed on a black background does this, and two such photographs
get the same digest. Over the two corpora, the two thresholds at the default grid:

--8<-- "docs/assets/generated/bmh/thresholds.md"

How the two thresholds separate copies from different images is in the table under
[block_size](#block_size).

## Digest layout

Bit $i$ is block $i$, row by row: byte 0 holds the first eight blocks of the top row,
least significant bit first, byte 1 the next eight, and the top row ends at byte 1. In
hexadecimal ([`ph_digest_to_hex()`](../api/text.md#ph_digest_to_hex) writes byte 0
first) each pair of digits is eight blocks, and within a pair the digits read their
blocks backwards: `01` is the first block of its eight, `80` the last. The paper defines a
sequence of bits and no byte layout, so the order is this library's choice.

![Which bit each block sets](../assets/generated/bmh/bit-order.light.svg#only-light){ width="560" }
![Which bit each block sets](../assets/generated/bmh/bit-order.dark.svg#only-dark){ width="560" }

## What changes the hash

The digest describes the layout of light and dark, as aHash does, at a finer scale, and
the same edits move it; with four times as many blocks, more of them lie near the median.

![Bits that differ from the original under nine transforms](../assets/generated/bmh/robustness.light.svg#only-light)
![Bits that differ from the original under nine transforms](../assets/generated/bmh/robustness.dark.svg#only-dark)

- **Recompression, resizing and noise** average out over the blocks: the digest does not
  move, or moves by a bit or two at the strongest settings.
- **Brightness, contrast, gamma and strong blur** keep the order of most block means, and
  the median with it; a few blocks lie close enough to the median to cross. They move a
  few of the 256 bits.
- **Rotation and cropping** move the image's content across the grid, and every block
  mean changes with them. These are the edits that matter for BMH, and, as a share of the
  bits, they move it further than aHash: a finer block is crossed by a smaller shift.

These are measurements on one photograph; the same edits over two whole corpora follow
below.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/bmh/robustness-table.md"

??? info "How this was measured"

    Each transform is applied alone to the photograph above, and the edited copy is
    saved losslessly, as PPM (as a JPEG of the given quality for that panel). The library
    hashes the original and every copy, and compares each copy with the original by
    [`ph_similarity_digest()`](../api/compare.md#ph_similarity_digest). The chart shows
    $256 \times (1 - \text{similarity})$, which is the Hamming distance: how many of the
    256 bits the edit flipped. Below is the code that ran, not a copy of it.

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

![Bits that differ from the original under nine transforms, median and middle half over each corpus](../assets/generated/bmh/robustness-corpus.light.svg#only-light)
![Bits that differ from the original under nine transforms, median and middle half over each corpus](../assets/generated/bmh/robustness-corpus.dark.svg#only-dark)

The photographs repeat the single image: rotation and cropping move the digest, and the
other edits move a few of its 256 bits at their strongest. The synthetic images also
answer to strong blur and noise. Their flat areas give many blocks of one value, which
tie at the median, and a small change to a tied block decides its bit.

### Copies and different images

Robustness alone proves little: a hash that never changes is perfectly robust. What makes
a hash useful is that copies of an image land closer together than different images do.
Here a *copy* is an original after one moderate edit (one strength of each of the nine,
listed under "How this was measured"), and *different images* are every pair of distinct
originals in the corpus.

![Distribution of bits that differ, for copies and for pairs of different images, in both corpora](../assets/generated/bmh/separability.light.svg#only-light)
![Distribution of bits that differ, for copies and for pairs of different images, in both corpora](../assets/generated/bmh/separability.dark.svg#only-dark)

Copies pile up at a few bits, different images spread around 128, half of the 256, where
two unrelated digests are expected to fall. The dashed line is the distance that accepts
95 % of the copies, and its label says how many different pairs it would accept as well.
*d′* sums up the gap in one number, the distance between the two means in units of their
spread.

Rotated and cropped copies spread widest, and they set the threshold: a finer grid moves
more of its blocks across the median under a shift. On the photographs a few pairs of
different images land at 0 bits, the same digest. They are pictures whose blocks are
mostly black, and [the median](#the-median-and-ties) explains why they collide.

??? info "The numbers behind the charts"

    --8<-- "docs/assets/generated/bmh/corpus-table.md"

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

![Bits that differ from the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/bmh/edits.light.svg#only-light)
![Bits that differ from the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/bmh/edits.dark.svg#only-dark)

- **A patch** changes the block means it covers. With 256 blocks a patch over a few
  percent of the frame covers several of them, and the digest notices it about as aHash
  does: over 4 % of the frame most edited photographs still pass for copies, over 16 %
  most do not.
- **Turning the hue** hardly moves the digest on photographs, whose muted colors keep
  their luminance under a hue rotation. On the flat, saturated colors of the synthetic
  images it moves the digest far.
- **A quarter or half turn** moves the digest as far as an unrelated image, and a mirror
  image about half as far on photographs: it keeps the layout from top to bottom.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/bmh/edits-table.md"

??? info "How this was measured"

    The same measurement as for the copies, over the same images: each edit is applied
    alone to the original, the copy is saved losslessly, and the library compares it with
    the original. The threshold is the one of the chart above, computed from the copies.
    Below is the code of the edits.

    ```python title="tools/site/measure/transforms.py"
    --8<-- "tools/site/measure/transforms.py:content_edits"
    ```


How the nine algorithms compare is on
[choosing an algorithm](../algorithms.md#comparison-summary).

## Cost

BMH costs what aHash does (the times are in the table at the top of the page): a
grayscale conversion and one area-average pass over the image, which read every pixel and
so grow with it, and then work on 256 numbers whatever the image's size. The area pass is
cached and shared: aHash, pHash and wHash reduce from the same grid, so BMH computed after
one of them on the same loaded image adds almost nothing. A `block_size` that divides 32 (2, 4, 8,
16, 32) is assembled from that cached pass; any other takes a pass of its own over the
grayscale image, as costly as the first.

??? info "How this was measured"

    --8<-- "docs/assets/generated/bmh/timing-table.md"

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

## Parameters

[`ph_context_set_block_params()`](../api/params.md#ph_context_set_block_params) sets the
grid.

| Setting | Values | Default | Meaning |
|---|---|---|---|
| `block_size` | 2 … 32 | 16 | the grid is `block_size` × `block_size` blocks, one bit each |

Digests are comparable only when computed with the same `block_size`. Another size
almost always gives another length, and the comparison functions refuse digests of
different lengths; sizes 3 and 4 both give two bytes, and a comparison cannot tell those
apart.

### block_size

The grid sets the scale the digest describes and its length, `block_size²` bits:

![The example photograph's block means and digest at block sizes 4, 8, 16 and 32](../assets/generated/bmh/sizes.light.svg#only-light)
![The example photograph's block means and digest at block sizes 4, 8, 16 and 32](../assets/generated/bmh/sizes.dark.svg#only-dark)

The bounds are set by the digest. At 32 the 1024 bits fill a digest's 128 bytes exactly,
and 33 would not fit. At 1 the single block is its own median and always at or above
it, so every image would give the same bit; 2 is the smallest grid whose blocks can
differ.

More bits do not by themselves separate better. Here is each size over both corpora,
with aHash's 64 bits for reference: *d′*, the gap between copies and different images in
units of their spread, and the share of different pairs the threshold that accepts 95 %
of the copies lets through.

![d′ and the share of different pairs within the threshold, against block_size, for both corpora, with aHash as a dashed line](../assets/generated/bmh/sizes-corpus.light.svg#only-light)
![d′ and the share of different pairs within the threshold, against block_size, for both corpora, with aHash as a dashed line](../assets/generated/bmh/sizes-corpus.dark.svg#only-dark)

- **d′ grows with the grid.** Different images spread less, as a share of the bits, the
  more bits there are, and the gap to the copies widens in those units.
- **The threshold does not follow.** A finer grid is moved further by a rotation or a
  crop, so the threshold that keeps 95 % of the copies has to reach further, and on the
  photographs the 8×8 grid lets fewer different pairs through than the default 16×16.
  The 4×4 grid, 16 bits, is too coarse for either corpus.
- **The default's extra false matches** are mostly the photographs on a black
  background that [collide](#the-median-and-ties); the table has the mean threshold for
  comparison.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/bmh/sizes-corpus-table.md"

??? info "How this was measured"

    The copies are the ones of the separability chart above, one moderate strength of
    each of the nine edits; each original and its copies are hashed at every block size
    by `site_stages bmh-variants`, which also thresholds the default grid at its mean.
    The distances, *d′* and the threshold are computed as for the corpus charts, and the
    tool checks that the default size gives the same *d′* as they do. aHash's numbers are
    those of [its page](ahash.md#copies-and-different-images).

    ```python title="tools/site/measure/corpus.py"
    --8<-- "tools/site/measure/corpus.py:variants"
    ```

    ```python title="tools/site/pages/bmh.py"
    --8<-- "tools/site/pages/bmh.py:variants"
    ```

    ```c title="tools/site/stages/bmh.c"
    --8<-- "tools/site/stages/bmh.c:bmh-variants"
    ```

    The digests are computed from the grid as in the steps above, and each is checked
    against `ph_compute_bmh()` with the same block size:

    ```c title="tools/site/stages/bmh.c"
    --8<-- "tools/site/stages/bmh.c:bmh"
    ```

## Settings that affect it

Four context settings change the pixels BMH reads.

| Setting | Effect on BMH |
|---|---|
| [`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) | the grayscale formula of step 1 |
| [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale) | a native decoder converts to grayscale itself, which can move a value by one level |
| [`ph_context_set_auto_orient()`](../api/loading.md#ph_context_set_auto_orient) | on by default: the digest describes the image as displayed, after its EXIF rotation |
| [`ph_context_set_alpha_mode()`](../api/loading.md#ph_context_set_alpha_mode) | how transparent pixels are composited before grayscale |

A level here and there moves a block mean by a fraction of a level, and can tip only a
block that lies next to the median.

--8<-- "docs/assets/generated/bmh/load-grayscale.md"

## In code

BMH returns a digest, like mHash, Radial and the two color hashes. This example computes
all five for two images, compares each pair with the function its kind calls for, BMH's
through [`ph_hamming_distance_digest()`](../api/compare.md#ph_hamming_distance_digest),
and stores the BMH digest as text and reads it back:

```c title="examples/digest_and_metrics.c"
--8<-- "examples/digest_and_metrics.c"
```

## Where it comes from

BMH is method 1 of "Block Mean Value Based Image Perceptual Hashing" by Bian Yang, Fan Gu
and Xiamu Niu (IIH-MSP 2006). The paper is paywalled; its steps are known from Christoph
Zauner's thesis (2010), which restates the method and implemented it in pHash. pHash
carries no block-mean hash today, so there is no reference implementation by the source's
own author to compare against.

The method normalizes the image to a preset size, divides it into blocks, takes their
means, and sets a bit where a mean is at or above the median of all of them (equation
3.9). This library follows it with two differences. It averages straight to the block
grid, which gives the exact block means for any image size, so the preset size adds only
a rounding. And it leaves out the paper's step that permutes the blocks under a secret
key: that is a security property, and the paper names no cipher. The paper leaves open
which central value is the median of an even count; the upper one keeps the digest
balanced with the `≥` of equation 3.9.

OpenCV's `BlockMeanHash`, the other implementation in wide use, thresholds at the mean of
the image, in a variable it calls `median`; its digests differ from this library's. The
comparison is in [provenance § 6](../algorithm-provenance.md#6-bmh--block-mean-value-hash).

--8<-- "docs/assets/generated/timing/footnote.md"
