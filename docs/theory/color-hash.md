# ColorHash — Color Histogram

Count how many pixels of the image have each color, after sorting the colors into 108
coarse groups: so much dark green, so much light gray, so much brown. The digest is that
count, one byte per group. It describes which colors the picture is made of and in what
proportion, and nothing about where they are: an image and any rearrangement of its
pixels get the same digest.

| | |
|---|---|
| **Call** | [`ph_compute_color_hash()`](../api/digests.md#ph_compute_color_hash), or [`PH_ALGO_COLOR_HASH`](../api/algorithms.md#PH_ALGO_COLOR_HASH) in [`ph_compute_digest()`](../api/algorithms.md#ph_compute_digest) |
| **Output** | digest of 108 bytes, compared with [`ph_histogram_intersection()`](../api/compare.md#ph_histogram_intersection), never bit by bit |
| **Source** | a color histogram with histogram intersection, after Michael Swain and Dana Ballard, "Color Indexing", 1991, implemented from secondary descriptions; the quantization is the library's ([provenance](../algorithm-provenance.md#8-colorhash--color-histogram)) |
--8<-- "docs/assets/generated/color_hash/cost-row.md"

## The steps

ColorHash has no parameters. Every picture on this page is computed by the library from
the same photograph, and the digest at the end is what `ph_compute_color_hash()` returns
for it.

![The stages of ColorHash on the example photograph: decoded, each pixel painted in the color of its bin, and the 108 bytes of the digest as bars in the colors of their bins](../assets/generated/color_hash/pipeline.light.svg#only-light)
![The stages of ColorHash on the example photograph: decoded, each pixel painted in the color of its bin, and the 108 bytes of the digest as bars in the colors of their bins](../assets/generated/color_hash/pipeline.dark.svg#only-dark)

The middle picture is how the hash sees the photograph: each pixel replaced by the
average of all the colors that share its bin. The bars on the right are the digest, each
in the color of its bin; their colors are data, not a palette.

**1. Opponent axes.** There is no grayscale step: each pixel's $R$, $G$ and $B$ become
three numbers,

$$
\begin{aligned}
rg &= R - G && \text{red against green}, & -255 &\le rg \le 255,\\
by &= 2B - R - G && \text{blue against yellow}, & -510 &\le by \le 510,\\
wb &= R + G + B && \text{light against dark}, & 0 &\le wb \le 765 .
\end{aligned}
$$

The first two say what hue the pixel leans to and how strongly, and are both zero for a
neutral gray, $R = G = B$. The third is its brightness.

**2. Bin per axis.** Each axis is shifted to start at zero, $s = rg + 255$,
$s = by + 510$ or $s = wb$, and divided evenly over its count of distinct values $V$
(511, 1021 and 766) into $n$ bins (6, 6 and 3):

$$
k = \left\lfloor \frac{s \cdot n}{V} \right\rfloor .
$$

Every value of every axis falls into some bin, and none is clipped. The chart below
shows the example's pixels on the two color axes, one panel per third of brightness,
with the bin edges:

![The example photograph's pixels plotted by R − G against 2B − R − G, in three panels for the dark, middle and light thirds of R + G + B, with the bin edges as lines, the unreachable bins shaded, and neutral gray circled](../assets/generated/color_hash/plane.light.svg#only-light)
![The example photograph's pixels plotted by R − G against 2B − R − G, in three panels for the dark, middle and light thirds of R + G + B, with the bin edges as lines, the unreachable bins shaded, and neutral gray circled](../assets/generated/color_hash/plane.dark.svg#only-dark)

A photograph's colors are mostly muted, and they crowd around neutral gray, circled.
Gray sits where four bins meet: with an even number of bins on each color axis, the
middle of the axis is an edge, not the middle of a bin. $rg = 0$ is the last value of
bin 2 and $rg = 1$ the first of bin 3, and the same holds for $by$. The section
[Gray on a corner](#gray-on-a-corner) measures what that does.

**3. Bin and count.** The three axis bins make one bin number,

$$
\text{bin} = (6\,k_{rg} + k_{by}) \cdot 3 + k_{wb}, \qquad 0 \le \text{bin} < 108,
$$

and every pixel adds one to its bin's count. The library reads the three axis bins from
three tables, one per axis, built for each call, so a pixel costs three lookups and no
division.

**4. Scale.** The counts become bytes by the largest of them, $c_{\max}$, rounded to the
nearest:

$$
\text{byte}_i = \left\lfloor \frac{255 \cdot c_i + \lfloor c_{\max} / 2 \rfloor}{c_{\max}} \right\rfloor .
$$

The largest bin is always 255. Scaling by the pixel count instead would leave the
average bin, under 1 % of the image, with two or three of the 255 levels. A bin holding
less than $1/510$ of the largest one rounds to zero.

--8<-- "docs/assets/generated/color_hash/stage-numbers.md"

## Digest layout

Byte $i$ is bin $i$. The bins run through the light–dark axis fastest, then blue–yellow,
then red–green: bytes 0 to 2 are the most green and most yellow colors from dark to
light, and bytes 105 to 107 the most red and most blue.

![The 108 bytes laid out as six rows of red–green bins by six groups of blue–yellow bins, each group of three cells from dark to light, each cell painted in its bin's color with its byte in hexadecimal and its bin number; the bins no 8-bit color reaches are empty](../assets/generated/color_hash/bit-order.light.svg#only-light)
![The 108 bytes laid out as six rows of red–green bins by six groups of blue–yellow bins, each group of three cells from dark to light, each cell painted in its bin's color with its byte in hexadecimal and its bin number; the bins no 8-bit color reaches are empty](../assets/generated/color_hash/bit-order.dark.svg#only-dark)

The empty cells are combinations of the three axes no 8-bit color reaches: no color is
strongly green and strongly blue at once, bins 15 to 17, for instance. In hexadecimal
([`ph_digest_to_hex()`](../api/text.md#ph_digest_to_hex) writes byte 0 first) each pair
of digits is one bin.

The bytes are counts, not bits. Two digests are compared by
[`ph_histogram_intersection()`](../api/compare.md#ph_histogram_intersection): each is
divided by its own sum, so that it adds up to 1, and the score is the sum over the bins
of the smaller of the two shares,

$$
\cap(a, b) = \sum_{i=0}^{107} \min\!\left( \frac{a_i}{\sum_j a_j},\; \frac{b_i}{\sum_j b_j} \right) .
$$

It is the part of the two color distributions they have in common: 1 for the same
proportions, 0 for no color in common. The library computes it in integers and divides
once, so the same proportions score exactly 1.0, and the order of the two digests does
not matter.

![The example's share of each bin beside the share of the example with its hue turned 30 degrees, and the smaller of the two as a line; the score is the sum under that line](../assets/generated/color_hash/intersection.light.svg#only-light)
![The example's share of each bin beside the share of the example with its hue turned 30 degrees, and the smaller of the two as a line; the score is the sum under that line](../assets/generated/color_hash/intersection.dark.svg#only-dark)

Turning the hue by 30 degrees is a mild recoloring, and it keeps less than half. The
green of the cactus moves to the neighboring bins, and so does the near-white of the
pot and the background: bin 62, light and a little red, empties into bin 44, light and
a little green, across the corner at gray.

## What changes the hash

The digest counts colors, so anything that changes colors moves it and anything that
only moves pixels around does not. How far a change goes depends on how many pixels it
pushes across a bin edge.

![Histogram intersection with the original under nine transforms](../assets/generated/color_hash/robustness.light.svg#only-light)
![Histogram intersection with the original under nine transforms](../assets/generated/color_hash/robustness.dark.svg#only-dark)

- **Brightness, contrast and gamma** move every pixel along the light–dark axis, whose
  three bins are wide: a pixel that crosses a third moves its whole count to another
  bin. These are the edits that move ColorHash most, in both directions.
- **Recompression and noise** move each pixel's color a little, at random, and the
  pixels near an edge cross it; strong noise spreads a flat color over several bins.
- **Downscaling, blur, rotation and cropping** mostly keep the colors: resampling blends
  neighboring pixels into colors between them, a turn fills its corners with the
  image's mean color, and a crop takes away the pixels of the border.

These are measurements on one photograph; the same edits over two whole corpora follow
below.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/color_hash/robustness-table.md"

??? info "How this was measured"

    Each transform is applied alone to the photograph above, and the edited copy is
    saved losslessly, as PPM (as a JPEG of the given quality for that panel). The library
    hashes the original and every copy, and compares each copy with the original by
    [`ph_histogram_intersection()`](../api/compare.md#ph_histogram_intersection), whose
    score is the number on the chart. Below is the code that ran, not a copy of it.

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

![Histogram intersection with the original under nine transforms, median and middle half over each corpus](../assets/generated/color_hash/robustness-corpus.light.svg#only-light)
![Histogram intersection with the original under nine transforms, median and middle half over each corpus](../assets/generated/color_hash/robustness-corpus.dark.svg#only-dark)

The corpora repeat the single photograph: the tonal edits, brightness, contrast and
gamma, move the digest furthest, and the geometric ones least. The synthetic images
react more strongly to the tonal edits: they are made of a few flat colors, and a flat
color that crosses an edge moves all its pixels at once. Halving their brightness takes
three quarters of them to a score of 0, no color in common with the original.

### Copies and different images

Robustness alone proves little: a hash that never changes is perfectly robust. What makes
a hash useful is that copies of an image land closer together than different images do.
Here a *copy* is an original after one moderate edit (one strength of each of the nine,
listed under "How this was measured"), and *different images* are every pair of distinct
originals in the corpus.

![Distribution of the histogram intersection, for copies and for pairs of different images, in both corpora](../assets/generated/color_hash/separability.light.svg#only-light)
![Distribution of the histogram intersection, for copies and for pairs of different images, in both corpora](../assets/generated/color_hash/separability.dark.svg#only-dark)

Copies pile up near 1. Different photographs spread widely below them: two photographs
always share some of the muted colors around gray, and rarely most of them. The dashed
line is the score that accepts 95 % of the copies, and its label says how many different
pairs it would accept as well. *d′* sums up the gap in one number, the distance between
the two means in units of their spread.

Many different synthetic images score 0 against each other: they are drawn in different
flat colors. A few score near 1, the same colors in a different pattern, which the
histogram cannot tell apart ([below](#what-a-histogram-cannot-see)).

??? info "The numbers behind the charts"

    --8<-- "docs/assets/generated/color_hash/corpus-table.md"

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
16 % of the frame; the hue of every pixel turned, which recolors the picture; and a
quarter turn, a half turn and a mirror image. Whether a hash should notice them depends
on what it is for: a search for copies wants to ignore a recoloring, a search for
retouched pictures wants to catch the patch.

![The example photograph with a patch over 4 and 16 % of the frame, its hue turned by 90 and 180 degrees, turned by 90 and 180 degrees, and mirrored](../assets/generated/edits/examples.light.svg#only-light)
![The example photograph with a patch over 4 and 16 % of the frame, its hue turned by 90 and 180 degrees, turned by 90 and 180 degrees, and mirrored](../assets/generated/edits/examples.dark.svg#only-dark)

Each panel shows how far the edited images land from their originals over both corpora.
The dashed line is the threshold of the chart above, the one that accepts 95 % of the
copies, in each corpus's color: an edited image above it would be taken for a copy.

![Histogram intersection with the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/color_hash/edits.light.svg#only-light)
![Histogram intersection with the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/color_hash/edits.dark.svg#only-dark)

- **A patch** costs at most its share of the frame, and less where it repeats colors the
  image already has, as a patch of the image's own corner does. Up to 16 % of the frame,
  every patched image stays above the threshold.
- **Turning the hue** is what ColorHash is for: a recoloring that leaves the luminance
  hashes nearly unmoved takes most images below the threshold, from 30 degrees on.
- **Turns and the mirror** keep every pixel, so the score is exactly 1.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/color_hash/edits-table.md"

??? info "How this was measured"

    The same measurement as for the copies, over the same images: each edit is applied
    alone to the original, the copy is saved losslessly, and the library compares it with
    the original. The threshold is the one of the chart above, computed from the copies.
    Below is the code of the edits.

    ```python title="tools/site/measure/transforms.py"
    --8<-- "tools/site/measure/transforms.py:content_edits"
    ```

### What a histogram cannot see

A histogram records how much of each color there is, and nothing else. Two images with
the same colors in the same proportions get the same digest, whatever the arrangement:
the example with its pixels shuffled, or turned a quarter, scores exactly 1 against the
original. pHash, beside it, sees a different picture in both. And within a bin the
histogram sees no difference: black and dark gray share the darkest third of the
light–dark axis, light gray and white the lightest.

![The example, the example with its pixels shuffled and turned 90 degrees, each scoring 1 against the original where pHash differs in about half its bits; and four pairs of flat colors: black and dark gray score 1, light gray and white 1, black and white 0, and gray and gray with one level more red 0](../assets/generated/color_hash/blind-spots.light.svg#only-light)
![The example, the example with its pixels shuffled and turned 90 degrees, each scoring 1 against the original where pHash differs in about half its bits; and four pairs of flat colors: black and dark gray score 1, light gray and white 1, black and white 0, and gray and gray with one level more red 0](../assets/generated/color_hash/blind-spots.dark.svg#only-dark)

The last pair is the opposite case, and the next section's subject: two grays one level
of red apart, which no eye tells apart, have no color in common.

--8<-- "docs/assets/generated/color_hash/blind-spots.md"

This is why ColorHash belongs beside a structural hash, not in place of one: it answers
"are the colors the same", and a structural hash answers "is the picture the same".

??? info "How this was measured"

    The library scores each pair, through `site_stages measure`; the pixels are shuffled
    by a fixed permutation, and each flat color is a 64×64 image.

    ```python title="tools/site/pages/color_hash.py"
    --8<-- "tools/site/pages/color_hash.py:intersection"
    ```

    The intersection above, computed in Python from the two digests, is checked against
    the library's score for the example and its hue turned 30 degrees; the build stops if
    they differ.

### Gray on a corner

Neutral gray, where photographs keep much of their color, lies on the corner of four
chroma bins ([step 2](#the-steps)). A tint too faint to see moves a pixel from one of
them into another: one level more red than green crosses from bin 2 to bin 3 of the
red–green axis. A photograph with large neutral areas, a white wall, a gray sky,
paper, can lose much of its histogram to such a tint.

![Histogram intersection of each image with the same image tinted by one or two levels of red, two of blue, or three of all three channels, over both corpora, against the threshold that accepts 95 % of copies](../assets/generated/color_hash/tints.light.svg#only-light)
![Histogram intersection of each image with the same image tinted by one or two levels of red, two of blue, or three of all three channels, over both corpora, against the threshold that accepts 95 % of copies](../assets/generated/color_hash/tints.dark.svg#only-dark)

For most images the tint costs little: their colors are away from the corner. For a
tail of them, the ones whose colors sit on it, one level of red takes the score below
the threshold, and for a few as low as two different photographs typically score. Adding the same three levels to all
three channels keeps $rg$ and $by$ where they were and moves only the brightness, and
costs little to any image.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/color_hash/tints-table.md"

??? info "How this was measured"

    Each original of both corpora is tinted by adding the levels to its channels, clipped
    at 255, saved losslessly, and scored against the original by the library, through
    `site_stages measure`. The threshold is that of the
    [copies](#copies-and-different-images).

    ```python title="tools/site/pages/color_hash.py"
    --8<-- "tools/site/pages/color_hash.py:tints"
    ```

    ```python title="tools/site/pages/color_hash.py"
    --8<-- "tools/site/pages/color_hash.py:tint"
    ```

How the nine algorithms compare is on
[choosing an algorithm](../algorithms.md#comparison-summary).

## Cost

ColorHash reads every pixel once, in color: three table lookups and one increment. It
shares nothing with the grayscale hashes: it neither uses nor makes the grayscale image
they cache. Its cost grows with the image's pixel count, and it costs more than aHash,
which reads every pixel once too. Each pixel increments a counter its own color selects,
and neighboring pixels mostly share a bin, so with one histogram every increment would
wait for the previous one to the same counter; the library spreads the pixels over four
histograms and adds them up at the end. The 108 bins cost nothing next to the pixels.

??? info "How this was measured"

    --8<-- "docs/assets/generated/color_hash/timing-table.md"

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

ColorHash has no parameters of its own, and the grayscale settings do not reach it.
Three context settings change the pixels it counts.

| Setting | Effect on ColorHash |
|---|---|
| [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale) | a decoded single-channel image has no color, and ColorHash refuses it with [`PH_ERR_REQUIRES_COLOR`](../api/errors.md#PH_ERR_REQUIRES_COLOR) rather than count every pixel as gray |
| [`ph_context_set_alpha_mode()`](../api/loading.md#ph_context_set_alpha_mode) | the color transparent pixels are composited onto, which becomes part of the histogram ([which background](preparation.md#which-background)) |
| [`ph_context_set_auto_orient()`](../api/loading.md#ph_context_set_auto_orient) | none: turning or mirroring an image keeps every pixel |

[`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) has no
effect: ColorHash reads $R$, $G$ and $B$ directly. An image that is gray but stored with
three channels gets a digest: every pixel lies on the gray corner, and the digest says
how bright the image is, by thirds.

--8<-- "docs/assets/generated/color_hash/load-grayscale.md"

## In code

ColorHash returns a digest, like mHash, BMH, Radial and ColorMoments. This example
computes all five for two images and compares each pair with the function its kind calls
for, ColorHash's through
[`ph_histogram_intersection()`](../api/compare.md#ph_histogram_intersection), and shows
that a bit-vector comparison refuses a histogram:

```c title="examples/digest_and_metrics.c"
--8<-- "examples/digest_and_metrics.c"
```

## Where it comes from

Color indexing by histogram intersection is the method of "Color Indexing" by Michael
Swain and Dana Ballard (International Journal of Computer Vision, 1991). The paper is
paywalled, and the library could not read it; the method is known from several
independent restatements, which agree on the formula of the intersection and on the
opponent axes. No conformance to the paper is claimed: ColorHash is a color histogram
with histogram intersection, after Swain and Ballard.

What the restatements leave open, the library decides, each choice recorded in
[provenance § 8](../algorithm-provenance.md#8-colorhash--color-histogram). The
quantization, 6 × 6 × 3 bins, is the library's own: the paper's resolution would not fit
a digest, and sixteen candidates were measured for separability. Two of them scored
higher and were rejected because they drop the light–dark axis, so that black and white
would get the same digest; a test keeps any future choice from repeating that. The
intersection divides each histogram by its own sum, where the formula as restated
divides by the reference image's, which would make the score depend on the order of the
two digests when the images differ in size.

--8<-- "docs/assets/generated/timing/footnote.md"
