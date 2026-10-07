# dHash — Difference Hash

Shrink the image to 9×8, and record for each pair of horizontal neighbors whether the
left one is darker than the right. Where aHash compares every cell with one number for
the whole image, dHash compares each cell with the one beside it: the hash describes
which way the brightness goes, not where it is high.

| | |
|---|---|
| **Call** | [`ph_compute_dhash()`](../api/hash64.md#ph_compute_dhash), or [`PH_HASH_DHASH`](../api/hash64.md#PH_HASH_DHASH) in [`ph_compute_multi()`](../api/hash64.md#ph_compute_multi) |
| **Output** | 64-bit hash, compared with [`ph_hamming_distance()`](../api/compare.md#ph_hamming_distance) |
| **Source** | David Oftedal and Neal Krawetz, "Kind of Like That", 2013 ([provenance](../algorithm-provenance.md#2-dhash--difference-hash)) |
--8<-- "docs/assets/generated/dhash/cost-row.md"

## The steps

Every picture on this page is computed by the library from the same photograph, and the
hash at the end is what `ph_compute_dhash()` returns for it.

![The four stages of dHash on the example photograph](../assets/generated/dhash/pipeline.light.svg#only-light)
![The four stages of dHash on the example photograph](../assets/generated/dhash/pipeline.dark.svg#only-dark)

**1. Grayscale.** Each pixel becomes one luminance value,

$$
Y = \left\lfloor \frac{38R + 75G + 15B}{128} \right\rfloor ,
$$

the same integer approximation of the ITU-R BT.601 weights as for every grayscale hash.
[`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) changes them.

**2. Reduce to 9×8.** The image is resampled to 9 columns and 8 rows, regardless of its
aspect ratio, with the Mitchell–Netravali cubic filter ($B = C = \tfrac13$):

$$
k(x) = \frac{1}{18}\begin{cases}
21|x|^3 - 36|x|^2 + 16 & |x| < 1 \\
-7|x|^3 + 36|x|^2 - 60|x| + 32 & 1 \le |x| < 2 \\
0 & \text{otherwise}
\end{cases}
$$

The kernel is stretched to the reduction, so each output cell is a weighted mean of the
source pixels within two cells' width of its center, rows first, then columns. Pixels
beyond the border repeat the edge, and the result is rounded and clamped to 0–255. Unlike
an area average, neighboring cells share source pixels, and the weights fall off smoothly
from the center: why that matters for dHash is in
[where it comes from](#where-it-comes-from).

![The 9×8 grid with its values, and the comparison between each pair of neighbors](../assets/generated/dhash/grid.light.svg#only-light){ width="480" }
![The 9×8 grid with its values, and the comparison between each pair of neighbors](../assets/generated/dhash/grid.dark.svg#only-dark){ width="480" }

**3. Compare neighbors.** With $q_{r,c}$ the cell in row $r$ and column $c$, each of the 8
pairs of neighbors in each of the 8 rows gives one bit:

$$
b_{r,c} = \begin{cases} 1 & q_{r,c} < q_{r,c+1} \\ 0 & q_{r,c} \ge q_{r,c+1} \end{cases}
\qquad r, c = 0 \dots 7 .
$$

A set bit means the brightness rises to the right. Two equal neighbors give 0, so in a
flat area every bit is clear.

![The 64 bits](../assets/generated/dhash/bits.light.svg#only-light){ width="420" }
![The 64 bits](../assets/generated/dhash/bits.dark.svg#only-dark){ width="420" }

**4. Pack the bits.** The first pair of the top row goes into the most significant bit:

$$
h = \sum_{r=0}^{7} \sum_{c=0}^{7} b_{r,c} \cdot 2^{\,63 - (8r + c)} .
$$

### Why nine columns

Eight comparisons need nine values: the ninth column exists only to be the right-hand
neighbor of the eighth. Here is the top row of the grid above, and the eight bits it gives.

![Nine cells of the top row and the eight bits their eight pairs of neighbors give](../assets/generated/dhash/nine-to-eight.light.svg#only-light)
![Nine cells of the top row and the eight bits their eight pairs of neighbors give](../assets/generated/dhash/nine-to-eight.dark.svg#only-dark)

## Bit layout

Reading the hash in hexadecimal reads the grid row by row: the first two hex digits are
the eight comparisons of the top row, the last two those of the bottom row. A bit belongs
to the boundary between two cells, not to a cell.

![Which bit each pair of neighbors sets](../assets/generated/dhash/bit-order.light.svg#only-light){ width="480" }
![Which bit each pair of neighbors sets](../assets/generated/dhash/bit-order.dark.svg#only-dark){ width="480" }

## What changes the hash

A bit records which of two neighbors is darker. An edit that keeps that order keeps the
bit; where two neighbors are almost equal, as across a flat wall or sky, almost any edit
can tip it.

![Bits that differ from the original under nine transforms](../assets/generated/dhash/robustness.light.svg#only-light)
![Bits that differ from the original under nine transforms](../assets/generated/dhash/robustness.dark.svg#only-dark)

- **Resizing and blur** leave the order of neighboring cells as it was, and the hash with
  it.
- **Recompression, brightness, contrast, gamma and noise** move a few bits at most: not
  the strong edges, but the pairs of nearly equal neighbors, which a level or two of
  change tips one way or the other.
- **Rotation and cropping** move content between cells, and the hash follows them more
  steeply than aHash's: a degree of rotation already moves several bits. dHash is not the
  algorithm for collections where images are rotated or reframed.

These are measurements on one photograph; the same edits over two whole corpora follow
below.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/dhash/robustness-table.md"

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

![Bits that differ from the original under nine transforms, median and middle half over each corpus](../assets/generated/dhash/robustness-corpus.light.svg#only-light)
![Bits that differ from the original under nine transforms, median and middle half over each corpus](../assets/generated/dhash/robustness-corpus.dark.svg#only-dark)

On the photographs, rotation and cropping are again the edits that matter; every other
edit moves the median by a few bits at most, a little more than it moves aHash's, and
blur and resizing hardly at all. The synthetic images are made of repeating patterns and
flat fields, where many pairs of neighboring cells come out nearly equal: there noise,
and to a lesser degree heavy recompression, tip many of the bits, where aHash's barely
move.

### Copies and different images

Robustness alone proves little: a hash that never changes is perfectly robust. What makes
a hash useful is that copies of an image land closer together than different images do.
Here a *copy* is an original after one moderate edit (one strength of each of the nine,
listed under "How this was measured"), and *different images* are every pair of distinct
originals in the corpus.

![Distribution of bits that differ, for copies and for pairs of different images, in both corpora](../assets/generated/dhash/separability.light.svg#only-light)
![Distribution of bits that differ, for copies and for pairs of different images, in both corpora](../assets/generated/dhash/separability.dark.svg#only-dark)

Copies pile up at a few bits, different images spread around 32, half of the 64, where two
unrelated hashes are expected to fall. The dashed line is the distance that accepts 95 % of
the copies, and its label says how many different pairs it would accept as well. *d′* sums
up the gap in one number, the distance between the two means in units of their spread.

This is where dHash earns its place beside aHash. Its copies sit a little further from
the original, so the threshold is higher; but on the photographs, different images are
spread more tightly around 32, and the gap between the two is wider than aHash's, with
fewer different pairs inside the threshold. On the synthetic images, where its copies
move more, the order is reversed.

??? info "The numbers behind the charts"

    --8<-- "docs/assets/generated/dhash/corpus-table.md"

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

![Bits that differ from the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/dhash/edits.light.svg#only-light)
![Bits that differ from the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/dhash/edits.dark.svg#only-dark)

- **A patch** changes the differences only along the rows it crosses: over 4 % of the
  frame nearly every edited photograph still passes for a copy, over 16 % fewer than
  half do.
- **Turning the hue** hardly moves the hash on photographs, whose muted colors keep their
  luminance under a hue rotation, which keeps each pixel's saturation and brightest
  channel. On the flat, saturated colors of the synthetic images it moves the hash far.
- **A turn or a mirror image** moves the hash as far as an unrelated image. A mirror
  reverses the direction of every difference along a row, so even the edit that keeps
  the most of the picture's layout leaves no bit to rely on.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/dhash/edits-table.md"

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

On a small image dHash costs what aHash does. On a large one it costs more than twice as
much: the Mitchell filter reads every source pixel and spreads it over neighboring cells
with floating-point weights, where aHash, pHash, wHash and BMH share one integer
area-average pass. On a 20-megapixel photograph that resampling pass is most of dHash's
time, and it is not shared with the other hashes when they are computed together. The
times are in the table at the top of the page.

??? info "How this was measured"

    --8<-- "docs/assets/generated/dhash/timing-table.md"

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

dHash has no parameters of its own: the grid is always 9×8. Four context settings change
the pixels it reads.

| Setting | Effect on dHash |
|---|---|
| [`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) | the grayscale formula of step 1 |
| [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale) | a native decoder converts to grayscale itself, which can tip a pair of nearly equal neighbors |
| [`ph_context_set_auto_orient()`](../api/loading.md#ph_context_set_auto_orient) | on by default: the hash describes the image as displayed, after its EXIF rotation |
| [`ph_context_set_alpha_mode()`](../api/loading.md#ph_context_set_alpha_mode) | how transparent pixels are composited before grayscale |

With [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale)
on, a JPEG is decoded straight to the luminance channel the file stores, which the
encoder computed with its own rounding, instead of being decoded to RGB and converted by
step 1. The values differ by a level here and there. For most cells that changes nothing,
but a bit decided by a one-level difference between neighbors can flip. The example
photograph has such a pair:

![The rows of the grid where the decoder's grayscale changes a bit, under both grayscale conversions](../assets/generated/dhash/load-grayscale.light.svg#only-light)
![The rows of the grid where the decoder's grayscale changes a bit, under both grayscale conversions](../assets/generated/dhash/load-grayscale.dark.svg#only-dark)

The same file hashed both ways is a near match, not an exact one: hashes meant to be
compared with each other are best computed with the same setting.

## In code

Hashing two images and counting the bits their hashes differ in, for dHash and the other
three 64-bit algorithms at once:

```c title="examples/hash_distance.c"
--8<-- "examples/hash_distance.c"
```

## Where it comes from

dHash was proposed by David Oftedal in a comment on Neal Krawetz's aHash post, and named,
described and evaluated by Krawetz in a blog post in 2013; there is no paper. The post
fixes the 9×8 reduction, the grayscale step, the direction of the comparison ("a '1' to
indicate that P[x] < P[x+1]") and the bit order (left to right, top to bottom,
big-endian), and this implementation follows all four exactly.

The post leaves the resampling filter open. This implementation uses Mitchell rather than
the area average aHash uses: dHash compares neighboring cells, and the hard cell
boundaries of an area average make those differences noisier. With an area average
instead, separability drops on the synthetic corpus and more pairs of different
photographs share a hash; the numbers are in
[provenance § 2](../algorithm-provenance.md#2-dhash--difference-hash).

--8<-- "docs/assets/generated/timing/footnote.md"
