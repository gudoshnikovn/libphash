# Comparing hashes

A hash on its own says nothing; two of them compared say how alike two images are, and a
threshold on that comparison says whether they are the same picture. This page describes
the four ways the library compares, which one goes with which algorithm, and what a
threshold costs, measured for all nine algorithms on the two corpora the algorithm pages
use. It ends with the text form in which hashes are stored and read back, and with what makes
a stored hash comparable with one computed on another machine.

## Which function for which hash

Every digest says what its bytes hold in its `kind` field, and each comparison function
accepts one kind:

| `kind` | Produced by | Compare with | Result |
|---|---|---|---|
| [`PH_DIGEST_KIND_BITS`](../api/digests.md#PH_DIGEST_KIND_BITS) | aHash, dHash, pHash, wHash (as a digest), mHash, BMH | [`ph_hamming_distance_digest()`](../api/compare.md#ph_hamming_distance_digest), [`ph_similarity_digest()`](../api/compare.md#ph_similarity_digest) | bits that differ, 0 to 8 × size; share of equal bits, 0.0 to 1.0 |
| [`PH_DIGEST_KIND_COEFFICIENTS`](../api/digests.md#PH_DIGEST_KIND_COEFFICIENTS) | Radial | [`ph_radial_similarity()`](../api/compare.md#ph_radial_similarity) | peak correlation, −1.0 to 1.0 |
| [`PH_DIGEST_KIND_HISTOGRAM`](../api/digests.md#PH_DIGEST_KIND_HISTOGRAM) | ColorHash | [`ph_histogram_intersection()`](../api/compare.md#ph_histogram_intersection) | the share the two histograms have in common, 0.0 to 1.0 |
| [`PH_DIGEST_KIND_VECTOR16`](../api/digests.md#PH_DIGEST_KIND_VECTOR16) | ColorMoments | [`ph_l2_distance()`](../api/compare.md#ph_l2_distance) | Euclidean distance, 0.0 and up |
| [`PH_DIGEST_KIND_VECTOR`](../api/digests.md#PH_DIGEST_KIND_VECTOR) | none: a feature vector the caller builds | [`ph_l2_distance()`](../api/compare.md#ph_l2_distance) | Euclidean distance, 0.0 and up |

The four 64-bit hashes also come as a plain `uint64_t`, compared with
[`ph_hamming_distance()`](../api/compare.md#ph_hamming_distance) (0 to 64) and
[`ph_similarity()`](../api/compare.md#ph_similarity).

Given a digest of another kind, a function refuses rather than answer. A Hamming distance
over Radial's quantized coefficients would be a plausible-looking integer that means
nothing, and the tag exists so that such a call fails. The distance and similarity
functions return −1 for it; `ph_radial_similarity()` and `ph_histogram_intersection()`,
whose result goes through a pointer, return
[`PH_ERR_INVALID_ARGUMENT`](../api/errors.md#PH_ERR_INVALID_ARGUMENT). The same answer
comes back for a NULL pointer, two digests of different sizes, or a size of 0 or above
[`PH_DIGEST_MAX_BYTES`](../api/digests.md#PH_DIGEST_MAX_BYTES): a size of 0 has no bits to
compare, and a distance of 0 for it would read as "identical".

```c title="examples/digest_and_metrics.c"
--8<-- "examples/digest_and_metrics.c:refused"
```

[`PH_DIGEST_KIND_UNSPECIFIED`](../api/digests.md#PH_DIGEST_KIND_UNSPECIFIED), the zero
that a struct filled in by hand holds, is accepted by every function. A binding that sets
only `data` and `size` gets no check, and no error either.

## Bits: Hamming distance and similarity

Six algorithms set bits, and two of their hashes are compared by the number of bit
positions in which they differ, the **Hamming distance**. Over a digest of $n$ bytes,

$$
d(a, b) = \sum_{i=0}^{n-1} \operatorname{popcount}(a_i \oplus b_i), \qquad 0 \le d \le 8n ,
$$

and the **similarity** is the share of bits that agree,

$$
s(a, b) = 1 - \frac{d(a, b)}{8n} .
$$

For a `uint64_t` hash $n$ is 8: `ph_hamming_distance()` is the popcount of `a ^ b`, and
`ph_similarity()` is $1 - d / 64$. The two forms agree:
[`ph_compute_digest()`](../api/algorithms.md#ph_compute_digest) returns a 64-bit hash as an
8-byte digest with its most significant byte first, so the digest functions on two such
digests give exactly what the `uint64_t` functions give on the two hashes.

Two hashes of unrelated images differ in about half of their bits, whatever the length:
on both corpora, the median distance between different images is half of the 64, 256 or
576 bits, or within a few bits of it. The similarity puts every length on that one scale,
1 for identical and about 0.5 for unrelated. It does not make thresholds comparable,
though: how close a copy comes depends on the algorithm, and [below](#choosing-a-threshold)
a copy of an mHash digest scores barely above 0.5 where a copy of an aHash scores above
0.9.

## Radial: peak correlation

Radial's 40 bytes are quantized coefficients of a profile around the image's center
([Radial](radial.md#digest-layout)), numbers rather than bits. Two digests $a$ and $b$ of
$n$ bytes are compared by the Pearson correlation of their bytes, with $b$ shifted
cyclically by every $k$ from 0 to $n - 1$, and the largest of the $n$ is the score:

$$
r = \max_{k} \frac{\sum_i (a_i - \bar a)(b_{(i - k) \bmod n} - \bar b)}
{\sqrt{\sum_i (a_i - \bar a)^2 \, \sum_i (b_i - \bar b)^2}} .
$$

It runs from −1 to 1, and 1 means the same shape up to a shift. A rotation of the image
shifts its profile, and the maximum over shifts is meant to find it again; on the digest
the score holds through a few degrees and a half turn, as the
[Radial](radial.md#turning-the-image) page measures. Swapping $a$ and $b$ gives the same
score.

A digest whose bytes are all equal has no spread, and no correlation is defined against
it. [`ph_compute_radial_hash()`](../api/digests.md#ph_compute_radial_hash) returns such a
digest, all zeros, for an image with no angular structure (flat, or the same in every
direction), and `ph_radial_similarity()` answers a pair with one in it with
[`PH_ERR_NO_STRUCTURE`](../api/errors.md#PH_ERR_NO_STRUCTURE). Any number would be
invented, and 1.0 would call two unrelated flat images identical; whether "no data" counts
as a match is the caller's decision.

[`PH_RADIAL_PCC_THRESHOLD`](../api/compare.md#PH_RADIAL_PCC_THRESHOLD), 0.9, is the score
at which the algorithm's source takes two images to be the same. It is a starting point
with a citation behind it, not a value measured for a collection. On the two corpora:

--8<-- "docs/assets/generated/comparing/radial-reference.md"

On the photographs 0.9 accepts nearly every copy and lets very few different pairs
through. On the synthetic images it accepts fewer copies than the threshold below that is
set for 95 % of them, and more different pairs than on the photographs.

## Histograms: intersection

ColorHash's 108 bytes are a histogram of the image's colors
([ColorHash](color-hash.md#digest-layout)), one count per bin. Two histograms $a$ and $b$,
with totals $A$ and $B$, are compared by their **intersection**, the share of each that the
other also holds:

$$
I(a, b) = \sum_i \min\!\left(\frac{a_i}{A}, \frac{b_i}{B}\right), \qquad 0 \le I \le 1 .
$$

It is 1 for the same distribution of colors and 0 for two images with no color in common.
Two empty histograms score 1, and an empty one against another 0. Swain and Ballard divide
both by the reference histogram's total, which makes the score depend on the order of its
arguments when the two images have different numbers of pixels; dividing each by its own
total gives the same number when the counts match, and the same number both ways when they
do not. The library computes the sum in integers, as $\sum_i \min(a_i B, b_i A) / (AB)$, so
that a histogram compared with itself scores exactly 1.

## Feature vectors: L2 distance

ColorMoments' 18 bytes are nine numbers, a mean, a spread and a skewness for each of red,
green and blue ([ColorMoments](color-moments.md#digest-layout)), each a signed 16-bit
integer in units of $1/128$ of a level
([`PH_VECTOR16_SCALE`](../api/digests.md#PH_VECTOR16_SCALE)). Two of them are compared by
the Euclidean distance between the decoded numbers $u$ and $v$:

$$
L_2(u, v) = \sqrt{\sum_{j} (u_j - v_j)^2} ,
$$

in channel levels, 0 for identical moments, with no upper end that a threshold could be
read against. A digest of `PH_DIGEST_KIND_VECTOR` holds one unsigned byte per feature and
is compared the same way, byte by byte. A digest of `PH_DIGEST_KIND_VECTOR16` with an odd
number of bytes is refused, as half a number.

## Choosing a threshold

A comparison becomes a decision when a threshold is placed on it: two images are the same
when their score is at the threshold or closer. Every threshold accepts some share of the
copies of an image and lets through some share of pairs of different images, and moving it
trades one against the other. Here, as on the algorithm pages, a *copy* is an image of a
corpus after one moderate edit (one strength of each of the nine edits: JPEG, downscale,
rotation, brightness, contrast, gamma, blur, noise, crop), and *different images* are every
pair of distinct images of the corpus.

![Nine panels, one per algorithm: the share of copies accepted against the share of different pairs accepted, on a logarithmic axis, for every threshold, one curve for the photographs and one for the synthetic images, with the threshold that accepts 95 % of the copies marked on each](../assets/generated/comparing/curves.light.svg#only-light)
![Nine panels, one per algorithm: the share of copies accepted against the share of different pairs accepted, on a logarithmic axis, for every threshold, one curve for the photographs and one for the synthetic images, with the threshold that accepts 95 % of the copies marked on each](../assets/generated/comparing/curves.dark.svg#only-dark)

Each curve is one corpus, and each point on it one threshold: how many copies it accepts
(up) against how many different pairs it lets through as well (right, on a logarithmic
scale, with 0 at the left edge). A curve that climbs high at the left edge separates
well. The dot is the threshold that accepts 95 % of the copies, the one the algorithm
pages mark.

--8<-- "docs/assets/generated/comparing/thresholds.md"

- **The threshold belongs to the collection.** For every algorithm, the synthetic images
  need a looser threshold than the photographs to accept 95 % of their copies, and that
  threshold lets through a larger share of different pairs. The synthetic images are
  checkerboards, stripes, rings and discs: their copies move further than a photograph's,
  and their different images are more alike than two photographs are. A threshold
  measured on one collection is a guess on another.
- **A similarity is not a threshold.** The similarity of the 95 % threshold runs from
  little above the 0.5 of unrelated images, for mHash, to above 0.9, for aHash, wHash and
  BMH on the photographs. One similarity cut applied to several algorithms is a different
  decision for each.
- **The last copies are the dearest.** The table under the chart gives the threshold for
  90 %, 95 % and 99 % of the copies. For nearly every algorithm, on both corpora, the 99 %
  threshold lets through several times as many different pairs as the 95 % one: the copies
  it adds are the hardest edits, and they sit among the different images. pHash on the
  photographs lets through none at any of the three.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/comparing/curves-table.md"

??? info "How this was measured"

    The values are the ones the algorithm pages chart: for each corpus, every image is
    edited with each of the nine edits, and the copy is compared with the original by the
    algorithm's own function, and every pair of distinct originals is compared the same way.
    The copies are one strength of each edit; the curve is every threshold the measured
    values allow, and the 95 % threshold is the closest value that still accepts 95 % of the
    copies. Nothing is measured for this page: it reads the measurements of the algorithm
    pages from the cache. Below is the code that ran, not a copy of it.

    ```python title="tools/site/measure/separability.py"
    --8<-- "tools/site/measure/separability.py:copies"
    ```

    ```python title="tools/site/measure/separability.py"
    --8<-- "tools/site/measure/separability.py:separability"
    ```

    ```python title="tools/site/pages/comparing.py"
    --8<-- "tools/site/pages/comparing.py:curve"
    ```

    ```python title="tools/site/measure/corpus.py"
    --8<-- "tools/site/measure/corpus.py:corpus"
    ```

    ```c title="tools/site/stages/measure.c"
    --8<-- "tools/site/stages/measure.c:compare"
    ```

    ```python title="tools/site/measure/metric.py"
    --8<-- "tools/site/measure/metric.py:bits"
    ```

    ```sh
    make site
    ```

### What counts as a copy

A threshold set for 95 % of all the copies does not accept 95 % of each kind. Here is the
share of each edit's copies that it accepts:

--8<-- "docs/assets/generated/comparing/by-edit.md"

The copies left outside are the edits each algorithm's design does not absorb. On the
photographs, for the hashes of the image's layout (aHash to BMH), they are the crop and the
rotation, which move content between cells; for Radial, the rotation; for the two color
hashes, brightness and gamma, which move colors between bins and shift the moments, while
a rotation leaves them untouched. The synthetic images, with their checkerboards and
stripes, add other misses, noise for pHash and mHash among them. A collection whose copies
are never cropped can take a stricter threshold than these, and one whose copies lose more
than the 5 % border removed here needs a looser one, or a hash that tolerates it.

### Measuring a threshold on your own collection

The same measurement fits any collection, with the library's comparison functions:

1. Collect pairs that are known to be the same picture, with the edits your copies
   actually go through, and pairs that are known to be different, as many as you can.
2. Compare every pair with the function for the algorithm's digest.
3. Sort the scores of the copies and take the one at the share of copies you need
   accepted (the 95th percentile, for 95 %). That is the threshold; count the different
   pairs at it or closer, and that is its price.

If that price is too high, no threshold of that algorithm will do, and the choice is
another algorithm: the curves above show which separate better on images like the
photographs and like the synthetic set.

## Hashes as text

A hash stored in a database or sent to another process goes as text.

- [`ph_hash_to_hex()`](../api/text.md#ph_hash_to_hex) writes a 64-bit hash as 16 lowercase
  hexadecimal digits, the most significant byte first, and
  [`ph_hash_from_hex()`](../api/text.md#ph_hash_from_hex) reads exactly 16 digits back, in
  either case, with nothing before or after them.
- [`ph_digest_to_hex()`](../api/text.md#ph_digest_to_hex) writes a digest as its kind, a
  colon and its bytes, byte 0 first: `bits:…`, `coefficients:…`, `histogram:…`,
  `vector16:…`. [`ph_digest_from_hex()`](../api/text.md#ph_digest_from_hex) requires the
  prefix and restores the kind with the bytes, so two digests read back from storage are
  refused the wrong comparison exactly as two fresh ones are. A string without a prefix is
  refused rather than read as untagged; `unspecified:` in front of it accepts it
  knowingly. [`PH_DIGEST_HEX_BUFFER_SIZE`](../api/text.md#PH_DIGEST_HEX_BUFFER_SIZE)
  holds the text of any digest.

A 64-bit hash taken as a digest has its bytes in the same order, so its text is `bits:`
followed by the 16 digits of `ph_hash_to_hex()`.

```c title="examples/digest_and_metrics.c"
--8<-- "examples/digest_and_metrics.c:text"
```

## Same hash on every machine

A stored hash is worth comparing only if a hash computed elsewhere would have come out the
same. A hash is a function of the decoded pixels and the context's settings, and the
library computes it the same way on every machine: no fused multiply-add contraction, one
plain loop for pHash's DCT, integer area averaging and grayscale conversion, exact
histogram intersection, and Radial's correlations summed in integers and rounded once,
at the end. A build for arm64 or x86-64, with GCC, Clang or MSVC, with or
without SIMD, gives the same bits; `tests/src/test_golden_hashes.c` holds every algorithm
to that exactly, with no tolerance.

The one thing that changes the pixels is **the JPEG decoder**. libjpeg-turbo (the bundled
decoder of the CMake build) and stb_image (the zero-dependency fallback) round their
inverse DCT differently, so the same JPEG reaches the hash functions as slightly
different pixels. [`ph_get_build_info()`](../api/build.md#ph_get_build_info) names the
decoder (`jpeg=libjpeg-turbo` or `jpeg=stb`). PNG and WebP decode to the same pixels in
every build.

For a collection of stored hashes this means:

- Hashes from builds with the same JPEG decoder can be compared by equality.
- Across the two JPEG decoders, compare by distance with a threshold, never by equality.
  On the test fixtures the 64-bit hashes and BMH come out the same; mHash differs by 3
  or 4 of its 576 bits, and Radial, ColorHash and ColorMoments by small amounts in a few
  of their features.
- Settings that change the pixels a hash sees, such as
  [`ph_context_set_decode_scale()`](../api/loading.md#ph_context_set_decode_scale),
  [`ph_context_set_gamma()`](../api/params.md#ph_context_set_gamma),
  [`ph_context_set_gray_weights()`](../api/params.md#ph_context_set_gray_weights) and
  [`ph_context_set_auto_orient()`](../api/loading.md#ph_context_set_auto_orient), are part
  of a hash's identity just like the algorithm's own parameters. Store them with the
  hashes.

## In code

Two images compared by pHash, as a Hamming distance and a similarity:

```c title="examples/compare_two_images.c"
--8<-- "examples/compare_two_images.c"
```

Each digest compared with the function its kind calls for, from the example that hashes
two images with every digest algorithm:

```c title="examples/digest_and_metrics.c"
--8<-- "examples/digest_and_metrics.c:compare"
```
