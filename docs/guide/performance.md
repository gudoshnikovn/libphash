# Performance

Hashing an image is two steps: decoding the file into pixels, and computing the hash from
them. This page shows what each step costs and how both grow with the size of the image,
what the settings that make hashing cheaper save, and how to measure on your own machine.
Every time on it is measured when the site is built, on the machine named at the foot of
the page.

## Where the time goes

Both steps read every pixel. The decoder produces each one, and the hash converts the
image to grayscale and reduces it to its working size, which reads each one again, before
the hash proper runs on a few thousand values. One photograph, scaled to six sizes and
saved as a JPEG at each, shows how the time follows the number of pixels:

![Nine small panels, one per algorithm, each with the time of the hash against megapixels on logarithmic axes and the time to decode the same JPEG dashed: every hash rises in step with the pixel count; aHash, pHash, wHash and BMH stay about twenty times below decoding, dHash about ten times, ColorHash and ColorMoments a few times, and mHash and Radial come close to decoding at the largest size](../assets/generated/performance/sizes.light.svg#only-light)
![Nine small panels, one per algorithm, each with the time of the hash against megapixels on logarithmic axes and the time to decode the same JPEG dashed: every hash rises in step with the pixel count; aHash, pHash, wHash and BMH stay about twenty times below decoding, dHash about ten times, ColorHash and ColorMoments a few times, and mHash and Radial come close to decoding at the largest size](../assets/generated/performance/sizes.dark.svg#only-dark)

- **A hash costs in proportion to the pixels.** Twice the pixels, about twice the time:
  the pass over the full image is all of the cost that grows. mHash also does a fixed
  amount of work on its 512×512 working image, which is about half of its time on the
  smallest one.
- **Decoding dominates the cheap hashes.** aHash, pHash, wHash and BMH share one pass,
  and take a small part of the decoding time, the smaller the larger the image; dHash,
  which resamples on its own, about twice theirs.
- **mHash, Radial and the color hashes come close to decoding** on large images: they
  blur, sample or count colors at full size. At 20 megapixels Radial takes about as long
  as the decoding.
- **Decoding grows a little faster than the pixel count** on this photograph, because
  the larger JPEGs hold more bytes per pixel (the file sizes are in the table under the
  next chart): scaling the photograph down averages away fine detail and noise that its
  full size keeps, and the decoder reads every byte.

Which hash to choose for its cost and what it detects is on
[Choosing an algorithm](../theory/choosing.md#cost).

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/performance/sizes.md"

??? info "How this was measured"

    The large example photograph, `tests/data/photo_large.jpeg`, is scaled down with
    Pillow's Lanczos filter to each size, keeping its aspect ratio, and saved as a JPEG at
    quality 90, a PNG and a WebP at quality 90. Each file is timed by `site_stages time`,
    naming the cases it runs: the load alone, and on the JPEG each hash as the first on the
    loaded image. Each case runs once to warm up, then until it has run at least five
    times and for at least a second; the time is the minimum.

    ```python title="tools/site/measure/timing.py"
    --8<-- "tools/site/measure/timing.py:sizes"
    ```

    ```c title="tools/site/stages/timing.c"
    --8<-- "tools/site/stages/timing.c:time"
    ```

    ```sh
    cmake --preset release && cmake --build --preset release --target site_stages
    build/release/site_stages time photo.jpeg decode ahash mhash
    ```

## Decoding by format

The same photograph decodes at very different speeds as JPEG, PNG and WebP, and asking for
grayscale changes the load in a different way for each:

![Three panels, JPEG, PNG and WebP, each with the time to load the photograph in color and as grayscale against megapixels on logarithmic axes: JPEG is the fastest and its grayscale load a little faster than its color load; PNG is about twice JPEG at the largest size, its grayscale load slower than its color load; WebP is the slowest, its two loads equal](../assets/generated/performance/formats.light.svg#only-light)
![Three panels, JPEG, PNG and WebP, each with the time to load the photograph in color and as grayscale against megapixels on logarithmic axes: JPEG is the fastest and its grayscale load a little faster than its color load; PNG is about twice JPEG at the largest size, its grayscale load slower than its color load; WebP is the slowest, its two loads equal](../assets/generated/performance/formats.dark.svg#only-dark)

- **JPEG is the fastest to decode** at every size. PNG takes about twice as long on the
  largest image and several times as long on the smallest; WebP is the slowest of the
  three on the large images.
- **A grayscale load saves time on a JPEG only.** libjpeg-turbo skips its color
  conversion. The PNG decoder decodes in color and converts afterwards, which costs more
  than loading in color and letting the hash convert. libwebp has no grayscale output, so
  a WebP is loaded in color either way
  ([Loading as grayscale](loading.md#loading-as-grayscale)).

The decoders themselves depend on the build. The Full build decodes JPEG with
libjpeg-turbo, PNG with libpng on zlib-ng and WebP with libwebp; the Minimal build decodes
JPEG and PNG with stb_image and has no WebP
([Formats and builds](loading.md#formats-and-builds)). Measured on three platforms,
libjpeg-turbo decodes a JPEG to grayscale 1.4 to 6.4 times as fast as stb_image, and
libpng on zlib-ng a photographic PNG 1.25 to 1.7 times as fast on Linux
([the measurement record](../benchmarks/2026-10-04-decoders-vs-stb.md)). The tables on
this site are measured on the Full build.

??? info "The numbers behind the chart"

    --8<-- "docs/assets/generated/performance/formats.md"

??? info "How this was measured"

    The files of the chart above, timed by the cases `decode` and `gray_decode` of
    `site_stages time`: the load in color, and the load with
    [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale),
    each into a context kept from run to run.

    ```python title="tools/site/measure/timing.py"
    --8<-- "tools/site/measure/timing.py:sizes"
    ```

## Making it cheaper

Four settings and habits cut the cost, each by a different amount on a different part of
it. On a 20-megapixel JPEG, loaded and hashed:

--8<-- "docs/assets/generated/performance/levers.md"

- **Decode at a reduced scale.**
  [`ph_context_set_decode_scale()`](../api/loading.md#ph_context_set_decode_scale) decodes
  a JPEG straight to ½, ¼ or ⅛ of its width and height. The decoding gets only somewhat
  cheaper, since every coefficient of the file is still read, but everything after it reads
  a fraction of the pixels: the saving is largest with the hashes that cost the most per
  pixel. It changes the hash, a little for most algorithms and more for mHash and Radial
  ([Decoding at a reduced scale](../theory/preparation.md#decoding-at-a-reduced-scale)),
  and applies to JPEG only.
- **Load as grayscale**, with
  [`ph_context_set_load_grayscale()`](../api/loading.md#ph_context_set_load_grayscale), when
  every hash you compute is a grayscale one: on a JPEG, a part of the load is saved.
  ColorHash and ColorMoments refuse such an image, and on a PNG it costs more than it
  saves (above).
- **Compute several hashes from one load.** The grayscale image and the area grid that
  aHash, pHash, wHash and BMH reduce from are computed by the first hash that needs them
  and kept until the next load
  ([One context, many images](loading.md#one-context-many-images)), so every hash after
  the first costs only its own part.
  [`ph_compute_multi()`](../api/hash64.md#ph_compute_multi) computes the four 64-bit
  hashes in one call, and costs about half of what the four cost each computed on its own:

    --8<-- "docs/assets/generated/performance/multi.md"

- **Hash many images at once** with
  [`ph_hash_files()`](../api/batch.md#ph_hash_files), which spreads them over a pool of
  worker threads ([Hashing many files](batch.md)).

Creating a context for each image costs nothing measurable beside the load
([Loading images](loading.md#one-context-many-images) has the times), so a context is
kept for convenience, not for speed.

??? info "How this was measured"

    Each cell is a case of `site_stages time` that loads the file into a context kept
    from run to run, with the settings of its row, and computes the hash of its column.
    The four 64-bit hashes are the cases `ahash` … `whash` added up, each the first hash on
    a loaded image, against the case `multi`.

    ```python title="tools/site/pages/performance.py"
    --8<-- "tools/site/pages/performance.py:levers"
    ```

    ```c title="tools/site/stages/timing.c"
    --8<-- "tools/site/stages/timing.c:time"
    ```

## One image, one thread

Parallelism is across images only: the `threads` of a batch sizes its pool of workers and
nothing else. A single image, in a batch worker or through a direct
[`ph_load_from_file()`](../api/loading.md#ph_load_from_file) and `ph_compute_*()` call, is
decoded, converted to grayscale and hashed on the calling thread.

Splitting one image across threads would buy little where the time goes.
--8<-- "docs/assets/generated/performance/decode-share.md"
Neither libjpeg-turbo nor libpng splits one decode across threads, so only the hash would
be split. It would also oversubscribe the machine whenever a batch already runs a worker
per CPU, which is why libwebp's optional second decoding thread is left off as well. A
program with one large JPEG and idle cores gains more from decoding it at a reduced scale.

## Measuring on your machine

`bench_hash` times decoding and hashing on any image, as the minimum, median and 90th
percentile of its runs. The minimum is the number to compare between two builds: the
others move with whatever else the machine is doing. CMake builds it with the tests:

```sh
cmake --preset release && cmake --build --preset release --target bench_hash
build/release/bench_hash hash photo.jpg 100       # every hash, the first on the image
build/release/bench_hash load photo.jpg 100 3     # loading in color and as grayscale, at ⅛
build/release/bench_hash full photo.jpg 100       # a load and pHash, together
build/release/bench_hash --json hash photo.jpg    # the same, as JSON
```

`make benchmark` builds and runs it from the Makefile, whose build decodes with stb_image:
its times are those of the Minimal build. The tables on this page are rebuilt by
`make site`, which runs `site_stages time` on the files above.

--8<-- "docs/assets/generated/timing/footnote.md"
