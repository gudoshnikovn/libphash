# Radial — Radial Variance Hash

Draw lines through the center of the image, one per degree, and measure how much the
brightness varies along each. The 180 variances form a profile of the image by angle:
high where a line crosses strong contrast, low where it runs along it. The digest is the
shape of that profile, compressed into 40 numbers. It describes how the picture is
organized around its center, not where its light and dark parts lie.

| | |
|---|---|
| **Call** | [`ph_compute_radial_hash()`](../api/digests.md#ph_compute_radial_hash), or [`PH_ALGO_RADIAL`](../api/algorithms.md#PH_ALGO_RADIAL) in [`ph_compute_digest()`](../api/algorithms.md#ph_compute_digest) |
| **Output** | digest of 40 bytes, compared with [`ph_radial_similarity()`](../api/compare.md#ph_radial_similarity), never bit by bit |
| **Source** | Christophe De Roover, Christophe De Vleeschouwer, Frédéric Lefèbvre and Benoît Macq, "Robust image hashing based on radial variance of pixels", 2005, as pHash implements it ([provenance](../algorithm-provenance.md#7-radial--radial-variance-hash)) |
--8<-- "docs/assets/generated/radial/cost-row.md"

## The steps

The steps below are those of the default settings: 180 lines of 128 points each, a blur
of σ 3.5 and a gamma of 1;
[`ph_context_set_radial_params()`](../api/params.md#ph_context_set_radial_params) and
[`ph_context_set_gamma()`](../api/params.md#ph_context_set_gamma) set others,
[below](#parameters). Every picture on this page is computed by the library from the same
photograph, and the digest at the end is what `ph_compute_radial_hash()` returns for it.

![The stages of Radial on the example photograph: decoded, grayscale, blurred, and the lines through the center](../assets/generated/radial/pipeline.light.svg#only-light)
![The stages of Radial on the example photograph: decoded, grayscale, blurred, and the lines through the center](../assets/generated/radial/pipeline.dark.svg#only-dark)

**1. Grayscale.** Each pixel becomes one luminance value,

$$
Y = \left\lfloor \frac{38R + 75G + 15B}{128} \right\rfloor ,
$$

the same integer approximation of the ITU-R BT.601 weights as for every grayscale hash.
[`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) changes them.

**2. Blur.** The grayscale image is blurred at full resolution by a Gaussian of standard
deviation σ = 3.5 pixels, applied along the rows and then the columns, with a kernel
reaching $\lceil 3\sigma \rceil = 11$ pixels each side; edge pixels are repeated past the
border, and the result is rounded to whole gray levels. The blur takes out the pixel-level
detail that a resampled or recompressed copy would not keep.

**3. Gamma.** Each value $v$ becomes $\left(v / v_{\max}\right)^{\gamma} \cdot v_{\max}$,
where $v_{\max}$ is the brightest value of the blurred image, truncated to a whole level.
At the default $\gamma = 1$ this is the identity, and the library skips it.

**4. Lines through the center.** For each angle $\theta_i = i \cdot 180° / 180$,
$i = 0 \dots 179$, a line runs through the center $(w/2,\, h/2)$ of the image, and 128
points are read on it, spaced $r / 64$ apart from $-r$ to $r - r/64$, where
$r = \min(w, h) / 2$. A point between pixels takes the bilinear blend of its four
neighbors; a point outside the image is skipped. The lines all stay inside the disc of
radius $r$, so the corners of a square image, and the ends of a long one, are never read.

![The blurred photograph with every tenth line, two of them highlighted, and the gray levels along those two lines](../assets/generated/radial/projections.light.svg#only-light)
![The blurred photograph with every tenth line, two of them highlighted, and the gray levels along those two lines](../assets/generated/radial/projections.dark.svg#only-dark)

The highlighted lines are the ones with the largest and the smallest variance, and the
gray levels along them are on the right. The first runs from the dark soil to the bright
background, the whole range of the image; the second meets less contrast end to end.

**5. Variance along each line.** Over the $n$ points $p_1 \dots p_n$ read on line $i$,

$$
R_i = \frac{1}{n} \sum_k p_k^2 - \left( \frac{1}{n} \sum_k p_k \right)^2 ,
$$

the definition of the source. The 180 variances are the profile:

![The variance along each of the 180 lines, against its angle, with its mean dashed](../assets/generated/radial/profile.light.svg#only-light)
![The variance along each of the 180 lines, against its angle, with its mean dashed](../assets/generated/radial/profile.dark.svg#only-dark)

A line at $\theta$ and one at $\theta + 180°$ are the same line, so 180 angles cover every
direction once.

**6. Standardize.** With $\mu$ and $s$ the mean and the standard deviation of the 180
variances, each becomes

$$
x_i = \frac{R_i - \mu}{s} .
$$

This drops the overall level of the variances, which is the image's contrast, and keeps
their shape. If $\mu \le 10^{-6}$, or if $s$ is no more than 1 % of $\mu$
($s^2 \le 10^{-4} \mu^2$), the image has no angular structure, and the digest is 40 zero
bytes ([below](#no-angular-structure)).

**7. Transform.** A one-dimensional DCT-II, orthonormally scaled, gives the first 40
coefficients of the profile:

$$
X_k = c_k \sum_{i=0}^{179} x_i \cos\frac{\pi (2i + 1) k}{360},
\qquad c_0 = \sqrt{1/180},\; c_k = \sqrt{2/180},
\qquad k = 0 \dots 39 .
$$

Coefficient $k$ measures how much of the profile is a wave of $k/2$ cycles over the 180
degrees. $X_0$ is the profile's sum, zero after step 6. Forty coefficients keep the
waves up to 19.5 cycles, which is the outline of the profile without its finest wiggles:

![The standardized profile with the profile its 40 coefficients describe, and the 40 coefficients](../assets/generated/radial/dct.light.svg#only-light)
![The standardized profile with the profile its 40 coefficients describe, and the 40 coefficients](../assets/generated/radial/dct.dark.svg#only-dark)

**8. Quantize.** The coefficients are mapped onto bytes by their own range: with
$X_{\min}$ and $X_{\max}$ the smallest and the largest of the 40,

$$
\text{byte}_k = \left\lfloor 255 \cdot \frac{X_k - X_{\min}}{X_{\max} - X_{\min}} \right\rfloor .
$$

The largest coefficient always becomes 255 and the smallest 0. Multiplying the whole
profile by a constant changes no byte.

--8<-- "docs/assets/generated/radial/stage-numbers.md"

### No angular structure

A profile that hardly varies with the angle has no shape to describe. Standardizing it
would divide by a spread that is mostly rounding, and the digest would describe the
rounding: in the library's own measurement, such a digest correlates with that of the same
image plus a little noise at 0.4 to 0.7, where the digest of an image with structure
correlates at 0.94 or more. So below a spread of 1 % of the mean the library returns 40
zero bytes, and [`ph_radial_similarity()`](../api/compare.md#ph_radial_similarity)
refuses to compare it with anything, with
[`PH_ERR_NO_STRUCTURE`](../api/errors.md#PH_ERR_NO_STRUCTURE).

![Two synthetic images of concentric rings and the example photograph, each with its profile divided by its mean, against a band of 1 % around the mean](../assets/generated/radial/structure.light.svg#only-light)
![Two synthetic images of concentric rings and the example photograph, each with its profile divided by its mean, against a band of 1 % around the mean](../assets/generated/radial/structure.dark.svg#only-dark)

Concentric rings are the typical case: every line through their center crosses the same
rings, and the variance is the same at every angle. A blank image is the other: it has no
variance at all. The bound is relative, so a faint pattern of a gray level or two still
gets a digest of its own, and so does an image with a little angular structure, like the
disc beside the rings, off the center.

--8<-- "docs/assets/generated/radial/structure.md"

The bound and its measurement are in
[provenance § 7](../algorithm-provenance.md#7-radial--radial-variance-hash); what a
refused comparison means for a search is the caller's decision.

## Digest layout

Byte $k$ is coefficient $k$: byte 0 is the coefficient of the profile's mean, zero, and
lands wherever zero falls between $X_{\min}$ and $X_{\max}$; bytes 1 and 2 are the waves
of half a cycle and one cycle over the 180 degrees, and so on up to byte 39. In
hexadecimal ([`ph_digest_to_hex()`](../api/text.md#ph_digest_to_hex) writes byte 0
first) each pair of digits is one coefficient.

![The 40 coefficients with their smallest and largest marked, and the 40 bytes they become, each with its value in hexadecimal](../assets/generated/radial/bit-order.light.svg#only-light)
![The 40 coefficients with their smallest and largest marked, and the 40 bytes they become, each with its value in hexadecimal](../assets/generated/radial/bit-order.dark.svg#only-dark)

The bytes are numbers, not bits. Two digests are compared by
[`ph_radial_similarity()`](../api/compare.md#ph_radial_similarity): the Pearson
correlation of the two byte sequences, computed with the second shifted cyclically by
each of 0 to 39 places, and the largest of the 40. It runs from −1 to 1; 1 means the
same shape up to a shift.
[`PH_RADIAL_PCC_THRESHOLD`](../api/compare.md#PH_RADIAL_PCC_THRESHOLD), 0.9, is the cut
the source uses for "the same image". Hamming distance would count bits of numbers and
mean nothing, and the bit-vector functions refuse a digest of this kind.

## What changes the hash

The digest describes how contrast is spread over the directions through the center.
Anything that keeps that spread keeps the digest; anything that turns the image or
changes what lies near the center moves it.

![Peak correlation with the original under nine transforms, with PH_RADIAL_PCC_THRESHOLD dashed](../assets/generated/radial/robustness.light.svg#only-light)
![Peak correlation with the original under nine transforms, with PH_RADIAL_PCC_THRESHOLD dashed](../assets/generated/radial/robustness.dark.svg#only-dark)

- **Brightness, contrast and gamma** mostly scale the variances, and step 6 takes the
  scale out. What is left is a change in the profile's shape: where a brightening clips
  the light parts to white, or a strong gamma bends dark and light differently, the score
  approaches 0.9.
- **Recompression, noise and blur** change single pixels, which the blur of step 2 and
  the 128 points of each line average away.
- **Downscaling and cropping** change the scale of the disc the lines read, a little:
  the same lines cross the same structures, nearer to or further from the ends.
- **Rotation** turns every line onto another angle, and the score falls by a few
  hundredths per degree, below 0.9 within a few degrees. The section
  [Turning the image](#turning-the-image) explains why.

These are measurements on one photograph; the same edits over two whole corpora follow
below.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/radial/robustness-table.md"

??? info "How this was measured"

    Each transform is applied alone to the photograph above, and the edited copy is
    saved losslessly, as PPM (as a JPEG of the given quality for that panel). The library
    hashes the original and every copy, and compares each copy with the original by
    [`ph_radial_similarity()`](../api/compare.md#ph_radial_similarity), whose score is
    the number on the chart. Below is the code that ran, not a copy of it.

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

![Peak correlation with the original under nine transforms, median and middle half over each corpus](../assets/generated/radial/robustness-corpus.light.svg#only-light)
![Peak correlation with the original under nine transforms, median and middle half over each corpus](../assets/generated/radial/robustness-corpus.dark.svg#only-dark)

The corpora repeat the single photograph: rotation is the one edit that takes the median
below 0.9 at a moderate strength, at a few degrees, and the strongest downscale and crop
come next. On the synthetic images the strongest downscale goes much further, and strong
noise and blur move a quarter of them: their narrow stripes and small checks are the
structure those edits erase. Two of them, the concentric rings, have no angular structure
and take no part ([the table](#copies-and-different-images) counts them).

### Copies and different images

Robustness alone proves little: a hash that never changes is perfectly robust. What makes
a hash useful is that copies of an image land closer together than different images do.
Here a *copy* is an original after one moderate edit (one strength of each of the nine,
listed under "How this was measured"), and *different images* are every pair of distinct
originals in the corpus.

![Distribution of the peak correlation, for copies and for pairs of different images, in both corpora](../assets/generated/radial/separability.light.svg#only-light)
![Distribution of the peak correlation, for copies and for pairs of different images, in both corpora](../assets/generated/radial/separability.dark.svg#only-dark)

Copies pile up near 1. Different images do not spread around 0, as two unrelated
correlations would: the score is the best of 40 shifts, and the shape of most profiles,
a few broad waves, lets some shift line two of them up. The dashed line is the score that
accepts 95 % of the copies, and its label says how many different pairs it would accept
as well. *d′* sums up the gap in one number, the distance between the two means in units
of their spread.

On the photographs the copies that set the threshold are mostly the rotated ones: a turn
of 2 degrees is the moderate edit that moves the digest furthest. Different synthetic
images often score high against each other: their simple patterns give a few profiles of
the same broad shape, and the threshold lets a large share of their pairs through.

??? info "The numbers behind the charts"

    --8<-- "docs/assets/generated/radial/corpus-table.md"

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
copies, in each corpus's color: an edited image above it would be taken for a copy.

![Peak correlation with the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/radial/edits.light.svg#only-light)
![Peak correlation with the original for a patch, a hue rotation, and turns and a mirror, median and middle half over each corpus, against the threshold that accepts 95 % of copies](../assets/generated/radial/edits.dark.svg#only-dark)

- **A patch** at the center is crossed by every one of the 180 lines, and the points of
  each line lie closest together there. Radial notices it more than any part of the
  frame of the same size: on the photographs a patch over a few percent of the frame
  already takes most edited images past the threshold.
- **Turning the hue** hardly moves the digest: the variances change little when the
  luminance does.
- **A half turn** leaves the digest where it was: every line maps onto itself. **A
  quarter turn** moves it as far as an unrelated image. **A mirror image** runs the
  profile backwards, which moves a photograph's digest far and leaves the symmetric
  patterns of the synthetic images in place; the next section has the reason.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/radial/edits-table.md"

??? info "How this was measured"

    The same measurement as for the copies, over the same images: each edit is applied
    alone to the original, the copy is saved losslessly, and the library compares it with
    the original. The threshold is the one of the chart above, computed from the copies.
    Below is the code of the edits.

    ```python title="tools/site/transforms.py"
    --8<-- "tools/site/transforms.py:content_edits"
    ```

### Turning the image

The source credits the radial variance with robustness to rotation, and the profile has
it. Turning the image turns every line with it: the line that was at $\theta$ is now at
$\theta + \alpha$, so the profile of the turned image is the original profile moved
along by $\alpha$, wrapping around at 180°.

![The variance profile of the example photograph and of the photograph turned 15 degrees, and the second moved back into place](../assets/generated/radial/rotation-profile.light.svg#only-light)
![The variance profile of the example photograph and of the photograph turned 15 degrees, and the second moved back into place](../assets/generated/radial/rotation-profile.dark.svg#only-dark)

The digest does not keep it. A shift of the profile is not a shift of its DCT
coefficients: moving a wave along changes how much of it each coefficient's cosine
catches, coefficient by coefficient. `ph_radial_similarity()` tries every cyclic shift of
the 40 bytes, as the source does, but shifting coefficient $k$ into the place of
coefficient $k + 1$ does not undo a turn. What remains is the tolerance of the
coefficients to a small change in the profile:

![Peak correlation of the rotated photograph with the original, for every whole degree from 0 to 180: computed on the digests, it falls below 0.9 within a few degrees and rises back at 180; computed on the profiles, it stays near 1](../assets/generated/radial/rotation.light.svg#only-light)
![Peak correlation of the rotated photograph with the original, for every whole degree from 0 to 180: computed on the digests, it falls below 0.9 within a few degrees and rises back at 180; computed on the profiles, it stays near 1](../assets/generated/radial/rotation.dark.svg#only-dark)

--8<-- "docs/assets/generated/radial/rotation-numbers.md"

- **A few degrees** match. That is the turn a scan or a straightened photograph comes
  with, and the threshold holds for it.
- **A half turn** matches exactly: each line maps onto itself, and the profile does not
  move at all.
- **Anything between** scores as an unrelated image would. Recognizing a picture turned
  by an arbitrary angle would take a comparison of the profiles, which the digest does
  not store.

A mirror image is the same story: the line at $\theta$ becomes the line at
$180° - \theta$, so the profile runs backwards, and a profile read backwards has the same
even coefficients and the odd ones with their signs turned. A pattern symmetric about a
vertical axis has odd coefficients near zero, and its mirror image keeps the digest.
--8<-- "docs/assets/generated/radial/mirror.md"

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/radial/rotation-table.md"

??? info "How this was measured"

    The photograph is turned by each whole degree from 1 to 180 with the rotation of the
    charts above; the tool computes the profile and the digest of each turned image,
    checks the digest against `ph_compute_radial_hash()`, and scores it against the
    original's with `ph_radial_similarity()`. The score on the profiles is the same
    formula applied to the 180 variances instead of the 40 bytes, computed in Python.

    ```python title="tools/site/algo_radial.py"
    --8<-- "tools/site/algo_radial.py:rotation"
    ```

    ```python title="tools/site/algo_radial.py"
    --8<-- "tools/site/algo_radial.py:similarity"
    ```

    ```c title="tools/site/stages.c"
    --8<-- "tools/site/stages.c:radial-profiles"
    ```

    The profile and the digest are computed from the stages as in the steps above, and
    each digest is checked against `ph_compute_radial_hash()`:

    ```c title="tools/site/stages.c"
    --8<-- "tools/site/stages.c:radial"
    ```

How the nine algorithms compare is on
[choosing an algorithm](../algorithms.md#comparison-summary).

## Cost

Radial reads every pixel twice before it reads a line: the grayscale conversion, which
is cached and shared with the other grayscale hashes, and the blur, which is its own. The
blur is most of the time at every image size. Its kernel has
$2\lceil 3\sigma \rceil + 1$ taps, 23 at the default, along each axis, so its cost grows
with the image's pixel count and with σ, and on a large photograph Radial costs about as
much as decoding the JPEG. The lines are $180 \times 128$ bilinear reads whatever the
image's size, a fraction of a millisecond, and the transform is 40 sums over 180
numbers. The table under [projections and samples](#projections-and-samples) and the
cases `radial_sigma_*` below separate the two parts.

??? info "How this was measured"

    --8<-- "docs/assets/generated/radial/timing-table.md"

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

## Parameters

[`ph_context_set_radial_params()`](../api/params.md#ph_context_set_radial_params) sets the
lines and the blur, and [`ph_context_set_gamma()`](../api/params.md#ph_context_set_gamma)
the gamma, which no other hash reads.

| Setting | Values | Default | Meaning |
|---|---|---|---|
| `projections` | 40 … 4096 | 180 | the number of lines, spread over 180° |
| `samples` | 2 … 4096 | 128 | the points read on each line |
| `sigma` | above 0, up to 64/3 | 3.5 | the blur of step 2 |
| `gamma` | above 0.001, up to 1000 | 1 | the gamma of step 3 |

Digests are comparable only when computed with the same settings. All of them give the
same 40 bytes, so a comparison cannot tell that two digests were computed differently.

The bounds come from the steps. Fewer than 40 lines would give fewer than 40
coefficients. One point on a line has no variance, so every image would have no
structure; two is the least that can vary. At σ above 64/3 the blur's kernel would need
more than the 64 taps a side it holds. The upper bounds on lines and points stop a
setting taken from an untrusted source from costing seconds per call, past the point
where the digest stops changing ([below](#projections-and-samples)).

How each setting separates copies from different images is measured over both corpora,
with the copies of [the chart above](#copies-and-different-images).

### sigma

The blur sets the finest structure the lines see:

![The example photograph blurred at sigma 1, 3.5 and 8, and the three standardized profiles](../assets/generated/radial/sigma.light.svg#only-light)
![The example photograph blurred at sigma 1, 3.5 and 8, and the three standardized profiles](../assets/generated/radial/sigma.dark.svg#only-dark)

The profile keeps its broad shape at every σ: the lines cross the same large structures,
and step 6 takes out the overall drop in variance a wider blur brings. Its details move,
and with them the digest. Over the corpora:

--8<-- "docs/assets/generated/radial/sigma-corpus.md"

On the photographs the three blurs separate copies from different images about equally.
On the synthetic images a lighter blur separates better: their narrow stripes and small
checks are structure the default blur erases. A wider blur costs more, in proportion to
its kernel, and separates no better on either corpus.

### projections and samples

More lines and more points per line estimate the same profile more finely. The
digest settles quickly; the time grows with the product of the two:

![Time against the number of lines and points, on a 400×400 and a 20 Mpx image, and how close the example's digest comes to that of the finest grid](../assets/generated/radial/grid.light.svg#only-light)
![Time against the number of lines and points, on a 400×400 and a 20 Mpx image, and how close the example's digest comes to that of the finest grid](../assets/generated/radial/grid.dark.svg#only-dark)

The dashed line is the default. Below it the digest is a coarser estimate of the same
profile; above it the digest hardly changes, and past a few hundred lines the time grows
with the grid, on a small image first. Over the corpora, the default separates the
photographs as well as any finer grid, and a finer grid lets a little fewer of the
different synthetic pairs through:

--8<-- "docs/assets/generated/radial/grid-table.md"

--8<-- "docs/assets/generated/radial/grid-corpus.md"

### gamma

Gamma bends the gray levels before the lines read them: below 1 it lifts the dark parts,
above 1 it deepens them.

![The example photograph after the blur, with gamma 0.5, 1 and 2, and the three standardized profiles](../assets/generated/radial/gamma.light.svg#only-light)
![The example photograph after the blur, with gamma 0.5, 1 and 2, and the three standardized profiles](../assets/generated/radial/gamma.dark.svg#only-dark)

--8<-- "docs/assets/generated/radial/gamma-corpus.md"

Neither gamma separates differently from the default on either corpus. The default, 1,
is pHash's, and the library skips the step at it. The formula of step 3 is pHash's too, so
a gamma taken from a pHash configuration means the same here. Gamma is Radial's alone:
none of the implementations the library follows applies it to another hash, and no other
hash reads the setting.

??? info "How the settings were measured"

    Each original and its nine copies are hashed under every setting by
    `site_stages radial-variants`, through `ph_compute_radial_hash()`. The scores are
    computed from the digests by the same formula as `ph_radial_similarity()`, in Python
    (above, under [Turning the image](#turning-the-image)); the default's *d′* is checked
    against the corpus charts, which take the library's own scores. *d′*, the threshold
    and the share of different pairs are computed as for the corpus charts.

    ```python title="tools/site/corpus.py"
    --8<-- "tools/site/corpus.py:variants"
    ```

    ```c title="tools/site/stages.c"
    --8<-- "tools/site/stages.c:radial-variants"
    ```

## Settings that affect it

Four context settings change the pixels Radial reads, besides its own parameters.

| Setting | Effect on Radial |
|---|---|
| [`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) | the grayscale formula of step 1 |
| [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale) | a native decoder converts to grayscale itself, which can move a value by one level |
| [`ph_context_set_auto_orient()`](../api/loading.md#ph_context_set_auto_orient) | on by default: the digest describes the image as displayed, after its EXIF rotation; a photograph stored on its side and read without it scores as an unrelated image |
| [`ph_context_set_alpha_mode()`](../api/loading.md#ph_context_set_alpha_mode) | how transparent pixels are composited before grayscale |

A level here and there is averaged away by the blur and the 128 points of a line.

--8<-- "docs/assets/generated/radial/load-grayscale.md"

## In code

Radial returns a digest, like mHash, BMH and the two color hashes. This example computes
all five for two images and compares each pair with the function its kind calls for,
Radial's through [`ph_radial_similarity()`](../api/compare.md#ph_radial_similarity),
which reports an image with no angular structure instead of a score, and shows that a
bit-vector comparison refuses a Radial digest:

```c title="examples/digest_and_metrics.c"
--8<-- "examples/digest_and_metrics.c"
```

## Where it comes from

The radial variance hash is the algorithm of "Robust image hashing based on radial
variance of pixels" by Christophe De Roover, Christophe De Vleeschouwer, Frédéric
Lefèbvre and Benoît Macq (ICIP 2005), which improved the authors' earlier RASH. The paper
is paywalled; its steps are known from Christoph Zauner's thesis (2010), which restates
the method as pHash implements it: the variance along 180 lines, a DCT of that profile,
the first 40 coefficients, and comparison by the peak of the cross-correlation with a
threshold of 0.9. pHash is the reference implementation, and the library follows it for
what the paper leaves open: the blur's σ of 3.5 and the gamma of 1 (the thesis reports
the authors suggesting 1 for both), the standardization before the transform, and the
quantization by the coefficients' own range.

The library differs in three places, each recorded in
[provenance § 7](../algorithm-provenance.md#7-radial--radial-variance-hash). Each line is
read at a fixed number of points with bilinear interpolation, where the source sums the
pixels of a one-pixel strip, whose length depends on the angle and the image's size. The
lines stop at the inscribed disc, so they stay inside the image at every angle. And the
peak correlation is symmetric in its two digests, where pHash divides by the first
digest's variance alone. The bound on angular structure is the library's own: the source
does not discuss images without it.

--8<-- "docs/assets/generated/timing/footnote.md"
