# ColorMoments — Color Moments

Describe each of the image's three color channels by three numbers: how bright it is on
average, how widely its levels spread around that average, and to which side the spread
leans. Nine numbers in all, which say what the image's tones are overall, and nothing
about where anything is: an image and any rearrangement of its pixels get the same
digest.

| | |
|---|---|
| **Call** | [`ph_compute_color_moments_hash()`](../api/digests.md#ph_compute_color_moments_hash), or [`PH_ALGO_COLOR_MOMENTS`](../api/algorithms.md#PH_ALGO_COLOR_MOMENTS) in [`ph_compute_digest()`](../api/algorithms.md#ph_compute_digest) |
| **Output** | digest of 18 bytes, nine signed 16-bit numbers, compared with [`ph_l2_distance()`](../api/compare.md#ph_l2_distance), never bit by bit |
| **Source** | the color moments of Markus Stricker and Markus Orengo, "Similarity of color images", 1995, known from a restatement of the paper; computed on RGB where the restatement uses HSV ([provenance](../algorithm-provenance.md#9-colormoments)) |
--8<-- "docs/assets/generated/color_moments/cost-row.md"

## The steps

ColorMoments has no parameters. Every picture on this page is computed by the library
from the same photograph, and the digest at the end is what
`ph_compute_color_moments_hash()` returns for it.

![The example photograph beside the histograms of its red, green and blue channels, each with its mean as a line, the band from one standard deviation below the mean to one above, and an arrow from the mean as long as the skewness, pointing to the side of the long tail](../assets/generated/color_moments/pipeline.light.svg#only-light)
![The example photograph beside the histograms of its red, green and blue channels, each with its mean as a line, the band from one standard deviation below the mean to one above, and an arrow from the mean as long as the skewness, pointing to the side of the long tail](../assets/generated/color_moments/pipeline.dark.svg#only-dark)

The histograms on the right are what the nine numbers summarize. Each channel of the
example has a tall peak on the light side, the white pot and the background, and a long,
low tail toward the dark, the cactus and the shadows: so its mean lies left of the peak,
and its skewness is negative, the arrow pointing to the tail.

**1. Channels.** There is no grayscale step: the moments are taken over the $N$ pixels of
each of the channels $R$, $G$ and $B$ separately, as they were decoded. Below, $p_j$ is
the level, 0 to 255, of pixel $j$ in one channel.

**2. Mean.**

$$
E = \frac{1}{N} \sum_{j=1}^{N} p_j .
$$

The library adds the levels as integers and divides once.

**3. Standard deviation.**

$$
\sigma = \sqrt{ \frac{1}{N} \sum_{j=1}^{N} \left( p_j - E \right)^2 } ,
$$

over $N$, not $N - 1$: it describes the image's own pixels, not a sample of anything.

**4. Skewness.**

$$
s = \sqrt[3]{ \frac{1}{N} \sum_{j=1}^{N} \left( p_j - E \right)^3 } .
$$

The third central moment is positive when the long tail of the channel's levels lies
above the mean, toward the light, and negative when it lies below; the cube root keeps
that sign. It also brings the third moment back to channel levels, the unit of $E$ and
$\sigma$, so that the three can be added in one distance. This is not the skewness of
statistics, $\frac{1}{N}\sum (p_j - E)^3 / \sigma^3$, which has no unit; that ratio
appears again in [A skewness near zero](#a-skewness-near-zero). The second and third
moments are summed in one pass, in double precision.

**5. Encode.** Each of the nine numbers is rounded to the nearest $1/128$ of a level,
halves away from zero, and written as a signed 16-bit integer, most significant byte
first:

$$
q = \operatorname{round}(128 \cdot x), \qquad -32768 \le q \le 32767 .
$$

The scale is the largest power of two that fits every value an 8-bit channel can give:
the mean reaches at most 255, $\sigma$ 127.5 and the skewness ±116.85, all from images of
two levels, and $255 \cdot 128 = 32640$. No value is ever clipped.

--8<-- "docs/assets/generated/color_moments/stage-numbers.md"

## Digest layout

Bytes $2i$ and $2i + 1$ hold number $i$, with $i = 3 \cdot \text{channel} + \text{moment}$:
red first, then green, then blue, and within each channel the mean, the standard
deviation and the skewness.

![The 18 bytes of the example's digest as nine pairs, three rows for red, green and blue and three columns for mean, standard deviation and skewness, each pair labeled with its byte positions, the signed integer it holds, and that integer divided by 128](../assets/generated/color_moments/bit-order.light.svg#only-light)
![The 18 bytes of the example's digest as nine pairs, three rows for red, green and blue and three columns for mean, standard deviation and skewness, each pair labeled with its byte positions, the signed integer it holds, and that integer divided by 128](../assets/generated/color_moments/bit-order.dark.svg#only-dark)

In hexadecimal ([`ph_digest_to_hex()`](../api/text.md#ph_digest_to_hex) writes byte 0
first) every four digits are one number: `438e` is 17294, a red mean of
$17294 / 128 = 135.109$. A negative number is in two's complement: `e5ab` is $-6741$, a
red skewness of $-52.664$. [`PH_VECTOR16_SCALE`](../api/digests.md#PH_VECTOR16_SCALE)
names the 128 for a caller that wants the numbers back.

The bytes are numbers, not bits. Two digests $a$ and $b$ are compared by
[`ph_l2_distance()`](../api/compare.md#ph_l2_distance), which decodes the nine numbers of
each and returns the Euclidean distance between them, in channel levels:

$$
d(a, b) = \sqrt{ \sum_{i=0}^{8} \left( a_i - b_i \right)^2 } .
$$

0 is the same nine numbers; there is no upper end short of the largest values the moments
can take. Every number counts alike: a level of mean weighs as much as a level of
skewness.

![The example and the example with its hue turned 30 degrees, and the nine differences between their numbers as bars, grouped by channel, with the distance they add up to](../assets/generated/color_moments/distance.light.svg#only-light)
![The example and the example with its hue turned 30 degrees, and the nine differences between their numbers as bars, grouped by channel, with the distance they add up to](../assets/generated/color_moments/distance.dark.svg#only-dark)

Turning the hue by 30 degrees raises the green mean and moves green's skewness further
toward the dark tail, lowers the red mean, and leaves blue nearly where it was. The two
largest differences make most of the distance, which squares each before adding.

## What changes the hash

The nine numbers describe the levels of each channel, so anything that changes levels
moves the digest and anything that only moves pixels around does not. How far depends on
how many pixels change, by how much, and on which side of the mean.

![L2 distance from the original under nine transforms](../assets/generated/color_moments/robustness.light.svg#only-light)
![L2 distance from the original under nine transforms](../assets/generated/color_moments/robustness.dark.svg#only-dark)

- **Brightness, contrast and gamma** change the level of every pixel. Brightness
  multiplies every level by its factor and so, wherever no channel clips, all nine
  numbers by the same factor: the distance grows with the image's own moments, tens of
  levels for a change of 15 %. These are the edits that move ColorMoments most, in both
  directions.
- **Blur and downscaling** replace pixels by averages of their neighbors: the means stay
  where they were, and the spread and the long tails shrink, so the standard deviations
  and the skewnesses move. **Cropping** removes the pixels of the border, and moves all
  three by as much as the border differs from the rest.
- **Recompression, noise and rotation** move little. JPEG changes levels by a little, as
  often up as down. Noise adds its own spread to each channel's, and the two add in
  quadrature, $\sqrt{\sigma^2 + \sigma_n^2}$: next to a channel's spread of tens of
  levels, noise of 5 or 10 adds almost nothing, and noise of 40 adds a good part. A turn
  fills its corners with the image's mean color, which leaves the means nearly where they
  were.

These are measurements on one photograph; the same edits over two whole corpora follow
below.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/color_moments/robustness-table.md"

??? info "How this was measured"

    Each transform is applied alone to the photograph above, and the edited copy is
    saved losslessly, as PPM (as a JPEG of the given quality for that panel). The library
    hashes the original and every copy, and compares each copy with the original by
    [`ph_l2_distance()`](../api/compare.md#ph_l2_distance), whose distance is the number on
    the chart. Below is the code that ran, not a copy of it.

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

![L2 distance from the original under nine transforms, median and middle half over each corpus](../assets/generated/color_moments/robustness-corpus.light.svg#only-light)
![L2 distance from the original under nine transforms, median and middle half over each corpus](../assets/generated/color_moments/robustness-corpus.dark.svg#only-dark)

The corpora repeat the single photograph: the tonal edits, brightness, contrast and
gamma, move the digest furthest, by about the same distance on both corpora, and the
others less. The synthetic images move more under recompression: they are made of flat
colors with sharp edges, and recompression spreads new levels around every edge, where a
photograph's levels are spread already.

### Copies and different images

Robustness alone proves little: a hash that never changes is perfectly robust. What makes
a hash useful is that copies of an image land closer together than different images do.
Here a *copy* is an original after one moderate edit (one strength of each of the nine,
listed under "How this was measured"), and *different images* are every pair of distinct
originals in the corpus.

![Distribution of the L2 distance, for copies and for pairs of different images, in both corpora](../assets/generated/color_moments/separability.light.svg#only-light)
![Distribution of the L2 distance, for copies and for pairs of different images, in both corpora](../assets/generated/color_moments/separability.dark.svg#only-dark)

Most copies land within a few levels of their original. The copies that change the tone
do not: a brightness, contrast or gamma change of 15 to 20 % moves the digest by tens of
levels, as far as some pairs of different photographs lie apart. The dashed line is the
distance that accepts 95 % of the copies, and it has to reach past those tonal copies; its
label says how many different pairs it accepts as well. *d′* sums up the gap in one
number, the distance between the two means in units of their spread. ColorHash, on the
same copies, separates the photographs more widely
([its page](color-hash.md#copies-and-different-images)).

A few different synthetic images lie at 0 from each other: they are drawn in the same
colors in the same proportions, in a different pattern.

??? info "The numbers behind the charts"

    --8<-- "docs/assets/generated/color_moments/corpus-table.md"

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
copies, in each corpus's color: an edited image below it would be taken for a copy.

![L2 distance from the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/color_moments/edits.light.svg#only-light)
![L2 distance from the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/color_moments/edits.dark.svg#only-dark)

- **A patch** moves the digest by about as much as its pixels differ from those it
  covers, and a patch of the image's own corner repeats levels the image already has: up
  to 16 % of the frame, most patched images stay below the threshold.
- **Turning the hue** moves the digest less than the tonal copies do: most photographs
  with their hue turned 30 degrees, and about half with it turned 90, stay below the
  threshold. The next section measures why.
- **Turns and the mirror** keep every pixel, so the distance is exactly 0.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/color_moments/edits-table.md"

??? info "How this was measured"

    The same measurement as for the copies, over the same images: each edit is applied
    alone to the original, the copy is saved losslessly, and the library compares it with
    the original. The threshold is the one of the chart above, computed from the copies.
    Below is the code of the edits.

    ```python title="tools/site/transforms.py"
    --8<-- "tools/site/transforms.py:content_edits"
    ```

### Tone against color

ColorMoments is a color hash, and a color hash is the tool for images that are the same
picture in different colors, where the structural hashes move a few bits at most. On
that task it loses to the tone. Turning the hue keeps each pixel's lightest and darkest
channel levels and moves them from one channel to another; for the muted colors most
photographs are made of, the three channels of a pixel are close, and swapping them
changes little. A change of brightness moves every level of every channel.

![L2 distance from the original for brightness, contrast and gamma changes of 15 to 20 %, which copies go through, beside hue turns of 30, 90 and 180 degrees, over both corpora, against the threshold that accepts 95 % of copies](../assets/generated/color_moments/tone-color.light.svg#only-light)
![L2 distance from the original for brightness, contrast and gamma changes of 15 to 20 %, which copies go through, beside hue turns of 30, 90 and 180 degrees, over both corpora, against the threshold that accepts 95 % of copies](../assets/generated/color_moments/tone-color.dark.svg#only-dark)

On the photographs, a hue turned by 30 degrees moves the digest less than a brightness
change of 15 % does, and the threshold that keeps those copies keeps most of the
30-degree turns and about half of the larger ones. ColorHash, which counts colors rather than levels, takes most of the same
recolorings for different images. To tell a recoloring from the original, ColorHash is
the hash; ColorMoments' distance says first how far the tone moved.

--8<-- "docs/assets/generated/color_moments/tone-color-table.md"

??? info "How this was measured"

    The values are those of the corpus charts above, the copies of
    [Over two corpora](#over-two-corpora) and the hue rotations of
    [Edits that change the picture](#edits-that-change-the-picture), each read against
    its corpus's threshold; ColorHash's column reads its own measurements against its own
    threshold.

### A skewness near zero

The cube root of step 4 is steep around zero. For a channel whose distribution is nearly
symmetric, the third central moment is a small fraction of $\sigma^3$, and its cube root
is not small: a third moment of 1 % of $\sigma^3$ is a skewness of $0.22\,\sigma$, about
13 levels for a channel of $\sigma = 60$. A copy that tips such a channel's
balance to the other side changes the sign of its skewness, a jump of twice that, from
one of the nine numbers.

![On the left, the skewness in the digest against the third central moment divided by sigma cubed, for sigma 60, steep around zero, with plus and minus 0.01 marked at about 13 levels; on the right, every channel of every photograph under the six copies that keep the tone, the change of its skewness against how asymmetric the channel was, the changes of sign marked](../assets/generated/color_moments/skew.light.svg#only-light)
![On the left, the skewness in the digest against the third central moment divided by sigma cubed, for sigma 60, steep around zero, with plus and minus 0.01 marked at about 13 levels; on the right, every channel of every photograph under the six copies that keep the tone, the change of its skewness against how asymmetric the channel was, the changes of sign marked](../assets/generated/color_moments/skew.dark.svg#only-dark)

On the right, each point is one channel of one photograph under one copy. A skewness
mostly moves by a level or less; the few that change sign jump by tens of levels, and
they are all channels that were close to symmetric. The skewness carries most of the
distance of the copies that keep the tone, noise apart. On the synthetic images, whose
flat colors often make a nearly symmetric channel, a copy changes the sign of up to a
quarter of the channels.

Dropping the skewness would not help: the copies would land closer, but different images
closer still, and *d′* falls without it.

--8<-- "docs/assets/generated/color_moments/skew-table.md"

??? info "How this was measured"

    The library computes the digest of every original of both corpora and of its nine
    copies, through `site_stages color_moments-digests`; the nine numbers are decoded from
    each digest, and the distances recomputed from them are checked against the corpus
    charts' before anything is drawn.

    ```c title="tools/site/stages.c"
    --8<-- "tools/site/stages.c:color_moments-digests"
    ```

    ```python title="tools/site/algo_color_moments.py"
    --8<-- "tools/site/algo_color_moments.py:decode"
    ```

    ```python title="tools/site/algo_color_moments.py"
    --8<-- "tools/site/algo_color_moments.py:skew"
    ```

    ```python title="tools/site/corpus.py"
    --8<-- "tools/site/corpus.py:variants"
    ```

How the nine algorithms compare is on
[choosing an algorithm](../algorithms.md#comparison-summary).

## Cost

ColorMoments reads every pixel twice, in color: once to add up the levels for the means,
and once more for the second and third powers of each level's distance from them, in
double precision. It shares nothing with the grayscale hashes: it neither uses nor makes
the grayscale image they cache. Its cost grows with the image's pixel count, and it costs
more than ColorHash, which reads every pixel once, with three table lookups. The nine
numbers at the end cost nothing next to the pixels.

??? info "How this was measured"

    --8<-- "docs/assets/generated/color_moments/timing-table.md"

    Each case runs once to warm up, then until it has run at least five times and for at
    least a second; the time is the minimum. The grayscale image and the area grid the
    library caches are dropped before every run, so each run is the first hash on a
    loaded image. The cases, and the timing loop:

    ```c title="tools/site/stages.c"
    --8<-- "tools/site/stages.c:time"
    ```

    The times are measured again only when the library, the tool or the machine changes:

    ```python title="tools/site/timing.py"
    --8<-- "tools/site/timing.py:timing"
    ```

## Settings that affect it

ColorMoments has no parameters of its own, and the grayscale settings do not reach it.
Two context settings change the pixels it reads.

| Setting | Effect on ColorMoments |
|---|---|
| [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale) | a decoded single-channel image has no color, and ColorMoments refuses it with [`PH_ERR_REQUIRES_COLOR`](../api/errors.md#PH_ERR_REQUIRES_COLOR) rather than compute three identical channels |
| [`ph_context_set_alpha_mode()`](../api/loading.md#ph_context_set_alpha_mode) | the color transparent pixels are composited onto, whose levels become part of every moment |
| [`ph_context_set_auto_orient()`](../api/loading.md#ph_context_set_auto_orient) | none: turning or mirroring an image keeps every pixel |

[`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) has no
effect: ColorMoments reads $R$, $G$ and $B$ directly. An image that is gray but stored
with three channels gets a digest whose three channels hold the same three numbers.

--8<-- "docs/assets/generated/color_moments/load-grayscale.md"

## In code

ColorMoments returns a digest, like mHash, BMH, Radial and ColorHash. This example
computes all five for two images and compares each pair with the function its kind calls
for, ColorMoments' through [`ph_l2_distance()`](../api/compare.md#ph_l2_distance), and
shows that a bit-vector comparison refuses a digest that is not bits:

```c title="examples/digest_and_metrics.c"
--8<-- "examples/digest_and_metrics.c"
```

## Where it comes from

Color moments are the method of "Similarity of color images" by Markus Stricker and
Markus Orengo (Proc. SPIE 2420, 1995). The paper is paywalled and the library could not
read it. The formulas come from the one restatement found, N. Keen's "Color Moments"
(University of Edinburgh course notes, 2005), student coursework and a third-party
account: the weakest source of any algorithm in the library.
The implementation follows that restatement's three formulas exactly, including the cube
root that keeps the skewness's sign, and anything changed on the strength of the
restatement is to be checked against the paper first.

Two choices differ from the restatement, each recorded in
[provenance § 9](../algorithm-provenance.md#9-colormoments). The restatement computes the
moments in HSV, and says that another encoding could be used as well; the library takes
them on the RGB channels as decoded, and does not change the color space on the word of
a source it cannot check against the paper. And the restatement compares two images by a
weighted sum of absolute differences, with the weights left to the user, so there is no
default to conform to; the library uses the unweighted Euclidean distance, in channel
levels, through [`ph_l2_distance()`](../api/compare.md#ph_l2_distance).

--8<-- "docs/assets/generated/timing/footnote.md"
