# wHash — Wavelet Hash

Shrink the image to 16×16, split it with one level of the Haar wavelet into a half-size
average and three bands of differences, and keep the average: 64 values, one per 2×2
block. Each bit records whether its value is above the median of the 64. The differences
are computed and set aside, so what the hash describes is where the image is light and
where dark, at the scale of an 8×8 grid, the same layout aHash reads, cut at the median
instead of the mean.

| | |
|---|---|
| **Call** | [`ph_compute_whash()`](../api/hash64.md#ph_compute_whash), or [`PH_HASH_WHASH`](../api/hash64.md#PH_HASH_WHASH) in [`ph_compute_multi()`](../api/hash64.md#ph_compute_multi) |
| **Output** | 64-bit hash, compared with [`ph_hamming_distance()`](../api/compare.md#ph_hamming_distance) |
| **Source** | none: the `whash` of Johannes Buchner's ImageHash library is the reference implementation ([provenance](../algorithm-provenance.md#4-whash--wavelet-hash)) |
--8<-- "docs/assets/generated/whash/cost-row.md"

## The steps

The steps below are those of the default mode,
[`PH_WHASH_FAST`](../api/params.md#PH_WHASH_FAST), which is the one to use unless a hash
must follow ImageHash's choice of scale; the other mode, `PH_WHASH_FULL`, differs only in
steps 2 and 3 and is [below](#the-full-mode). Every picture on this page is computed by
the library from the same photograph, and the hash at the end is what
`ph_compute_whash()` returns for it.

![The five stages of wHash on the example photograph](../assets/generated/whash/pipeline.light.svg#only-light)
![The five stages of wHash on the example photograph](../assets/generated/whash/pipeline.dark.svg#only-dark)

**1. Grayscale.** Each pixel becomes one luminance value,

$$
Y = \left\lfloor \frac{38R + 75G + 15B}{128} \right\rfloor ,
$$

the same integer approximation of the ITU-R BT.601 weights as for every grayscale hash.
[`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) changes them; [image preparation](preparation.md#grayscale) has why these weights.

**2. Reduce to 16×16.** The image is divided into a 16×16 grid of equal areas, regardless
of its aspect ratio, and each cell becomes the exact mean of the pixels it covers, rounded
to the nearest integer: the same area average as [aHash's 8×8](ahash.md#the-steps), on a
grid twice as fine. The values are then divided by 255, so the grid $g$ holds numbers
from 0 to 1.

**3. One Haar level.** The Haar wavelet replaces each pair of neighbors $x, y$ by their
sum and their difference, both divided by $\sqrt 2$:

$$
(x,\; y) \;\mapsto\; \left( \frac{x + y}{\sqrt 2},\; \frac{x - y}{\sqrt 2} \right) .
$$

Applied to the pairs of every row, it puts eight sums in the left half and eight
differences in the right; applied then to the pairs of every column, it does the same
top and bottom. The 16×16 grid becomes four 8×8 bands. The top-left one, *LL*, holds sums
both ways; the other three hold the differences across, down, and both ways, which are
the image's edges at this scale.

![The 16×16 grid, and the four 8×8 bands one Haar level makes of it: LL in the top left, the three bands of differences around it](../assets/generated/whash/haar-level.light.svg#only-light)
![The 16×16 grid, and the four 8×8 bands one Haar level makes of it: LL in the top left, the three bands of differences around it](../assets/generated/whash/haar-level.dark.svg#only-dark)

The LL band is a smaller copy of the grid, and the bands of differences are near zero
wherever the image is smooth. The division by $\sqrt 2$ makes the transform orthonormal:
the grid can be rebuilt from the four bands, and nothing is lost.

**4. Keep the LL band.** The hash reads only LL. Written out, each of its values is the
sum of a 2×2 block of the grid, halved:

$$
L_{i,j} = \frac{g_{2i,2j} + g_{2i,2j+1} + g_{2i+1,2j} + g_{2i+1,2j+1}}{2}
\qquad i, j = 0 \dots 7 ,
$$

which is twice the block's mean. Since the 16×16 cells nest exactly in the 8×8 ones, a
block's mean is the mean of the image over one cell of an 8×8 grid, the grid aHash
thresholds, up to the rounding of step 2. The three bands of differences take no part in
the hash.

![The 8×8 LL band of the example, its values, with the cells above the median highlighted](../assets/generated/whash/ll-band.light.svg#only-light){ width="480" }
![The 8×8 LL band of the example, its values, with the cells above the median highlighted](../assets/generated/whash/ll-band.dark.svg#only-dark){ width="480" }

**5. Threshold at the median.** With 64 values the median $m$ is the mean of the 32nd and
33rd smallest, and a bit is set where its value is above it:

$$
b_i = \begin{cases} 1 & L_i > m \\ 0 & L_i \le m \end{cases}
\qquad i = 0 \dots 63 ,
$$

with the band read row by row, $L_i = L_{\lfloor i/8 \rfloor,\, i \bmod 8}$. Unless values
tie at the median, exactly 32 bits are set. An edit that lifts one value above the median
therefore pushes another one below it, and two hashes of the same image tend to differ
in an even number of bits.

**6. Pack the bits.** Value $i$ sets bit $i$, so the top-left value goes into the least
significant bit:

$$
h = \sum_{i=0}^{63} b_i \cdot 2^{\,i} .
$$

![The 64 bits of wHash on the example photograph as an 8×8 grid, a cell filled where its bit is set: where the average is above the median](../assets/generated/whash/bits.light.svg#only-light){ width="420" }
![The 64 bits of wHash on the example photograph as an 8×8 grid, a cell filled where its bit is set: where the average is above the median](../assets/generated/whash/bits.dark.svg#only-dark){ width="420" }

### What the wavelet adds

Steps 3 and 4 take a 16×16 grid to the 8×8 grid of its block means, which the area
average of step 2 could have produced directly. The hash is therefore, up to rounding and
bit order, aHash's grid thresholded at its median rather than at its mean. Measured, the
two agree to the bit on most images; where they do not, a value lies within rounding of
the median, and a flat image, whose values tie, can differ in many bits. Here is the
FAST hash against the 8×8 area grid thresholded at its median and packed the same way:

--8<-- "docs/assets/generated/whash/modes-grid.md"

Reading the same grid does not make the two the same hash: where they cut it decides
which edits move them, as [measured below](#whash-and-ahash).

## Bit layout

Bit $i$ is value $i$ of the LL band, row by row, from the least significant bit up. In
hexadecimal the hash therefore reads the band backwards: the last hex digit holds the
first four values of the top row, the first hex digit the last four of the bottom row.
This is the order pHash uses for its block, and the reverse of aHash's, so a wHash and an
aHash of the same image do not line up bit for bit even where they agree.

![Which bit each value of the LL band sets](../assets/generated/whash/bit-order.light.svg#only-light){ width="420" }
![Which bit each value of the LL band sets](../assets/generated/whash/bit-order.dark.svg#only-dark){ width="420" }

## What changes the hash

The hash describes the coarse layout of light and dark, as aHash does, and the same
edits move it.

![Bits that differ from the original under nine transforms, on the example photograph](../assets/generated/whash/robustness.light.svg#only-light)
![Bits that differ from the original under nine transforms, on the example photograph](../assets/generated/whash/robustness.dark.svg#only-dark)

- **Recompression, resizing, blur and noise** average out over the cells of the grid:
  the hash does not move, or moves by a pair of bits at the strongest settings.
- **Brightness and contrast** scale the values, which keeps their order and the
  median's, until the brightest pixels clip at white. **Gamma** bends them instead, and
  can swap two block means that lie close. Either moves a pair of bits at most here.
- **Rotation and cropping** move the image's content across the grid, and every block
  mean changes with them. These are the edits that matter for wHash, as for aHash.

These are measurements on one photograph; the same edits over two whole corpora follow
below.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/whash/robustness-table.md"

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

![Bits that differ from the original under nine transforms, median and middle half over each corpus](../assets/generated/whash/robustness-corpus.light.svg#only-light)
![Bits that differ from the original under nine transforms, median and middle half over each corpus](../assets/generated/whash/robustness-corpus.dark.svg#only-dark)

The photographs repeat the single image: rotation and cropping move the hash, and the
other edits move it by a pair of bits at most, at their strongest. On the synthetic
images cropping spreads widest. Several of them are regular patterns, stripes, checks and
rings, whose block means sit close together, and a crop that shifts the pattern against
the grid reorders many of them at once; a uniform field, like the background of a disc,
gives many equal means, which the median splits by the smallest change.

### Copies and different images

Robustness alone proves little: a hash that never changes is perfectly robust. What makes
a hash useful is that copies of an image land closer together than different images do.
Here a *copy* is an original after one moderate edit (one strength of each of the nine,
listed under "How this was measured"), and *different images* are every pair of distinct
originals in the corpus.

![Distribution of bits that differ, for copies and for pairs of different images, in both corpora](../assets/generated/whash/separability.light.svg#only-light)
![Distribution of bits that differ, for copies and for pairs of different images, in both corpora](../assets/generated/whash/separability.dark.svg#only-dark)

Copies pile up at a few bits, different images spread around 32, half of the 64, where two
unrelated hashes are expected to fall. The dashed line is the distance that accepts 95 % of
the copies, and its label says how many different pairs it would accept as well. *d′* sums
up the gap in one number, the distance between the two means in units of their spread.

On both corpora wHash separates copies from different images about as well as aHash,
which is what reading the same grid predicts: the two differ in where they cut it, and
on these images the cut matters little.

??? info "The numbers behind the charts"

    --8<-- "docs/assets/generated/whash/corpus-table.md"

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

![Bits that differ from the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/whash/edits.light.svg#only-light)
![Bits that differ from the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/whash/edits.dark.svg#only-dark)

- **A patch** changes the block means it covers, as for aHash: over 4 % of the frame most
  edited photographs still pass for copies, over 16 % most do not.
- **Turning the hue** hardly moves the hash on photographs, whose muted colors keep their
  luminance under a hue rotation. On the flat, saturated colors of the synthetic images
  it moves the hash far.
- **A quarter or half turn** moves the hash as far as an unrelated image, and a mirror
  image about half as far: it keeps the layout from top to bottom.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/whash/edits-table.md"

??? info "How this was measured"

    The same measurement as for the copies, over the same images: each edit is applied
    alone to the original, the copy is saved losslessly, and the library compares it with
    the original. The threshold is the one of the chart above, computed from the copies.
    Below is the code of the edits.

    ```python title="tools/site/measure/transforms.py"
    --8<-- "tools/site/measure/transforms.py:content_edits"
    ```

### wHash and aHash

The two hashes read the same 8×8 grid and differ in one thing, the threshold: aHash
compares each cell with the grid's mean, wHash with its median. That one difference
shows in what moves them. Here are both, edit by edit, over both corpora, as the mean
number of bits that differ from the original, for the edits where they part and for
contrast, where they do not:

![Mean bits that differ from the original for aHash and wHash under gamma, contrast, rotation, crop and noise, over the photographs and the synthetic images](../assets/generated/whash/vs-ahash.light.svg#only-light)
![Mean bits that differ from the original for aHash and wHash under gamma, contrast, rotation, crop and noise, over the photographs and the synthetic images](../assets/generated/whash/vs-ahash.dark.svg#only-dark)

- **Gamma: wHash moves less.** A median depends only on the order of the values. A tone
  curve keeps the order of the block means nearly intact, so the median stays between the
  same two cells, while the mean moves against the cells and those next to it cross. The
  gap is widest on the synthetic images.
- **Brightness and contrast: no difference on photographs.** Scaling the values moves
  the mean and the median alike. On the synthetic images the strongest settings move
  one hash or the other a bit more, in either direction (the table has the numbers).
- **Rotation and cropping: wHash moves a little more.** Unless values tie, wHash always
  has 32 bits set, so a value that crosses the median pushes another one back across it,
  and a reordering costs two bits where it costs aHash one.
- **Noise on the synthetic images: wHash moves more.** Their flat areas give many equal
  block means, which tie at the median; noise breaks the tie one way or the other. On
  photographs ties are rare, and neither hash moves.

Separating copies from different images, the two are close on both corpora (the tables
of this page and of [aHash's](ahash.md#copies-and-different-images)). The median also
sets exactly half of the bits on every image, which a mean does not:

--8<-- "docs/assets/generated/whash/ahash-bits.md"

On photographs, then, aHash's count stays near half on its own, and the median's
balance seldom changes the outcome.

??? info "The numbers behind the chart"

    Mean bits that differ from the original, over each corpus:

    --8<-- "docs/assets/generated/whash/vs-ahash-table.md"

??? info "How this was measured"

    The values are those of the corpus charts above, from the same cached measurement,
    for both algorithms; the figure and the table take their mean over the corpus
    instead of the median, since the median of a small edit is 0 for both. The count of
    aHash bits is [`ph_compute_ahash()`](../api/hash64.md#ph_compute_ahash) on each photograph, by `site_stages whash-modes`.

How the nine algorithms compare is on
[choosing an algorithm](choosing.md).

## Cost

In the default mode wHash costs what aHash does, on a small image and on a large one (the
times are in the table at the top of the page). The 16×16 grid comes from the same cached
area pass over the grayscale image as aHash's, pHash's and BMH's, which is what grows with
the image; the transform that follows works on 256 values whatever the image's size. The
full mode resamples the whole grayscale image to a power of two of its own size, and
costs about three times as much on a large one.

??? info "How this was measured"

    --8<-- "docs/assets/generated/whash/timing-table.md"

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

[`ph_context_set_whash_mode()`](../api/params.md#ph_context_set_whash_mode) chooses the
mode, and
[`ph_context_set_whash_remove_max_haar_ll()`](../api/params.md#ph_context_set_whash_remove_max_haar_ll)
an extra step taken from ImageHash.

| Setting | Values | Default | Meaning |
|---|---|---|---|
| mode | `PH_WHASH_FAST`, `PH_WHASH_FULL` | `PH_WHASH_FAST` | a 16×16 grid and one level, or a grid as large as the image allows and as many levels as it takes to reach 8×8 |
| `remove_max_haar_ll` | 0, 1 | 0 | subtract the image's mean before the transform |

Hashes are comparable only when computed with the same settings.

### The full mode

`PH_WHASH_FULL` follows ImageHash's choice of scale. The grayscale image is resampled
with a box filter to the largest power of two that fits its shorter side, at least 8 (256
for the 400×400 example), and the Haar level of step 3 is applied repeatedly, each
time to the LL band of the previous one, until LL is 8×8. Every detail band of every level
is set aside; only the last LL is thresholded, as in step 5.

![The example resampled to 256×256 and its five-level Haar decomposition, each level's LL band split in its own top-left corner, with the 8×8 LL band at the very corner](../assets/generated/whash/pyramid.light.svg#only-light)
![The example resampled to 256×256 and its five-level Haar decomposition, each level's LL band split in its own top-left corner, with the 8×8 LL band at the very corner](../assets/generated/whash/pyramid.dark.svg#only-dark)

After $k$ levels an LL value is $2^k$ times the mean of its $2^k \times 2^k$ block, so the
8×8 band is the image averaged over an 8×8 grid: the same 64 numbers as the fast mode's,
up to resampling and rounding. Here they are side by side, both divided by
$2^k$ and scaled back to gray levels; cells whose bit differs between the two would be
outlined in orange.

![The 8×8 LL band of the example in both modes, as gray levels, with the bits each sets and the two hashes](../assets/generated/whash/modes.light.svg#only-light)
![The 8×8 LL band of the example in both modes, as gray levels, with the bits each sets and the two hashes](../assets/generated/whash/modes.dark.svg#only-dark)

Over both corpora the two modes give the same hash on most images and a few bits apart on
the rest; FAST against FULL:

--8<-- "docs/assets/generated/whash/modes-full.md"

The full mode buys closeness to the reference implementation's scale rule, not a
different hash, at about three times the cost on a large image. Use it when hashes must
be computed the way ImageHash computes them; otherwise the fast mode gives the same
result for less.

### remove_max_haar_ll

ImageHash, by default, decomposes the image all the way down to a single LL value,
zeroes it, and reconstructs the image before computing the hash, to keep overall
brightness out of it. That single value is the image's mean, scaled, so zeroing it and
reconstructing subtracts the mean $\mu$ from every pixel and changes nothing else. Every
LL value then drops by the same amount, $2^k \mu$, and so does the median: every
comparison $L_i > m$ comes out as before. The median threshold has already removed what
the option is meant to remove.

The one exception is a value that equals the median. As computed, it sets no bit; after
the subtraction, the extra transform and its inverse leave it a rounding error above or
below the median, and that error decides its bit. On an image with a uniform area many
values tie: here is the synthetic image where the option changes the most bits.

![A synthetic image of a disc on a uniform field, its LL band with and without the mean subtracted, and the bits the subtraction changes outlined](../assets/generated/whash/removal.light.svg#only-light)
![A synthetic image of a disc on a uniform field, its LL band with and without the mean subtracted, and the bits the subtraction changes outlined](../assets/generated/whash/removal.dark.svg#only-dark)

The uniform background gives more than half of the band one value, which is therefore
the median, and as computed none of it is above: the hash is all zeros. With the mean
subtracted, each of those values lands a little above or below the median by rounding
alone. On photographs ties are rarer; in the fast mode, whose grid holds whole numbers,
they still occur. Each mode's hash against the same mode's with the option on:

--8<-- "docs/assets/generated/whash/modes-removal.md"

It is therefore off by default, and offered for callers who need ImageHash's
configuration. On the tests' synthetic corpus, turning it on lowers separability
([provenance](../algorithm-provenance.md#4-whash--wavelet-hash) has the numbers).

## Settings that affect it

Five context settings change the pixels wHash reads.

| Setting | Effect on wHash |
|---|---|
| [`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) | the grayscale formula of step 1 |
| [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale) | a JPEG decoder converts to grayscale itself ([measured](preparation.md#the-decoders-grayscale)), which can move a value by one level |
| [`ph_context_set_auto_orient()`](../api/loading.md#ph_context_set_auto_orient) | on by default: the hash describes the image as displayed, after its EXIF rotation |
| [`ph_context_set_alpha_mode()`](../api/loading.md#ph_context_set_alpha_mode) | how transparent pixels are composited before grayscale ([which background](preparation.md#which-background)) |
| [`ph_context_set_decode_scale()`](../api/loading.md#ph_context_set_decode_scale) | a JPEG read by libjpeg-turbo is decoded at ½, ¼ or ⅛ of its size ([measured](preparation.md#decoding-at-a-reduced-scale)); the 16×16 grid averages the smaller image into the same cells, and nearly every hash stays within the threshold for copies, down to images a few times the grid |

A level here and there in the grid moves a block mean by a fraction of a level, and can
tip only a value that lies next to the median.

--8<-- "docs/assets/generated/whash/load-grayscale.md"

## In code

wHash is one of the four 64-bit hashes
[`ph_compute_multi()`](../api/hash64.md#ph_compute_multi) computes from one grayscale
conversion. This example hashes two images with all four and prints how far apart each
pair is:

```c title="examples/hash_distance.c"
--8<-- "examples/hash_distance.c"
```

## Where it comes from

wHash has no paper. It is the `whash` of Johannes Buchner's ImageHash library, whose only
reference is a blog post by Alexander Petrov; ImageHash is a third-party implementation,
the closest thing to a specification there is. "Robust Image Hashing" by Venkatesan, Koon,
Jakubowski and Moulin (ICIP 2000) is an earlier wavelet-based hash, but a different
algorithm: it draws statistics from randomly tiled wavelet bands under a secret key, which
makes it a keyed hash, and a keyed hash cannot give the same value on every machine with
no shared secret, the property this library is built on.

With no source to conform to, wHash is judged by what can be measured: robustness,
discrimination, and above all the separation of copies from different images, on this
page and in the library's property tests
([methodology](../methodology.md#measurable-properties)).

Three things differ from ImageHash's `whash`. The default mode fixes the grid at 16×16,
where ImageHash takes the largest power of two that fits the image; `PH_WHASH_FULL` takes
that. The image is resampled by an area average, or a box filter in the full mode, where
ImageHash uses Lanczos. And `remove_max_haar_ll`, on by default in ImageHash, is off here,
for the reason above. The bit order, which nothing specifies, is this library's choice.
The comparison is in [provenance § 4](../algorithm-provenance.md#4-whash--wavelet-hash).

--8<-- "docs/assets/generated/timing/footnote.md"
