# tools/site — the measuring and drawing behind the documentation site

Every figure, measured table and number on the site's algorithm pages
(`docs/theory/*.md`) is produced here, from the library the site documents. Nothing is
drawn by hand and no number is typed into a page: a page includes what this directory
writes into `docs/assets/generated/`, and shows the code that wrote it. A picture of an
algorithm's steps is checked against the library while it is made, so it cannot show a
computation the code does not perform.

`make site` (or `scripts/site.sh`) runs all of it; `make site-serve` also serves the site.

## How it fits together

```text
scripts/site.sh
  ├─ cmake --build --preset release --target site_stages      stages/  (C)
  └─ render.py --tool build/release/site_stages --out …        (Python)
       ├─ pages/<algo>.py   each page's own figures, from `site_stages <algo>`
       ├─ measure/          the numbers every page shares: the edits, both corpora,
       │                    the times — each cached in build/site-cache/
       └─ draw/             the figures and tables every page shares
                                     ↓
                       docs/assets/generated/<algo>/*.svg, *.md
                                     ↓
          docs/theory/<algo>.md   ![…](…light.svg#only-light), --8<-- "…/table.md"
```

| Path | What it holds |
|---|---|
| `render.py` | The command line: every page's figures, then the shared charts (`--algo ahash,…` for some pages only) |
| `fetch_corpus.py`, `corpus_photos.tsv` | The photo corpus: its manifest, the download into `~/.cache/libphash-site/` checked against each file's SHA-256, the attribution page (`fetch_corpus.py --help`) |
| `stages/` | `site_stages`, the measuring tool in C, linked against the library like the tests |
| `measure/` | Running `site_stages`, the edits, the corpora, separability, the times, and the cache |
| `draw/` | The two themes, the panels several pages draw, the robustness, corpus and edit charts, the Markdown tables |
| `pages/` | One module per page with figures, registered in `pages/__init__.py`: the algorithm pages (`PAGES`) and the topic pages (`TOPICS`) |

### `stages/` — site_stages

One file per algorithm (`ahash.c` … `color_moments.c`) holds the modes that show its
stages, `measure.c` compares images (every robustness and separability number comes
from it), `timing.c` times them, `main.c` lists the modes. `stages.h` has what they
share: loading an image (`load_image()`, and `load_decoder_gray()` for the decoder's
grayscale), writing PNM and JSON (`json_*()`), and the checks. `site_stages` without
arguments lists every mode.

- `site_stages <algo> <image> <outdir>` writes the stages of one algorithm on one image:
  PNM images and `<algo>.json`. Every result is recomputed from the stages it shows and
  compared with the library's (`check_hash64()`, `check_digest()`); a mismatch exits 1
  and stops the build.
- `site_stages <algo>-<what> <image>...` prints one JSON line per image: a measurement
  over a corpus that one page makes (block sizes, settings, modes). These are built on
  `for_each_image()`.
- `measure`, `pairs`, `time` and `corpus` serve every page; `sizes` prints each
  algorithm's digest size and kind, for the page that compares them.
- `loaded <outdir> <image>...` writes each image as the library loaded it and its
  grayscale; `area <w> <h> <image>` prints the library's area average onto a grid
  (`prepare.c`, for the page on image preparation).
- **Load settings.** `measure`, `pairs` and `loaded` take `--load=<settings>` between
  their images, and every image after it is loaded so: `alpha=grey|white|black|ignore`,
  `scale=full|half|quarter|eighth`, `orient=off`, `gray=decoder`, `weights=R/G/B/SHIFT`
  (a grayscale the tool computes, loaded as one channel), comma-separated, or `default`
  (`take_load_settings()` in `util.c`). A file compared with itself under another
  setting is `measure a.jpg --load=scale=eighth a.jpg`.

### `measure/` — the numbers

| Module | What it does |
|---|---|
| `tool.py` | Runs `site_stages`: `run_stages()` (a stage mode, its JSON and images), `run_lines()` (a mode that prints JSON lines), `run_on_images()` (the same on Pillow images) |
| `transforms.py` | The one registry of edits: `transforms()`, the edits a copy goes through; `content_edits()`, the edits that change the picture |
| `robustness.py` | Every edit of one image measured against the original |
| `corpus.py` | The two corpora (`images()`), every edit of every image and every pair (`measure_corpus()`), and any mode over every original and its copies (`measure_settings()`) |
| `separability.py` | What a copy is (`COPY_STRENGTHS`), d′ and the threshold that accepts 95 % of the copies; `variant_distances()`, the same distances for one setting of a `<algo>-variants` mode |
| `metric.py` | How a page's metric reads: bits that differ, or the algorithm's own score |
| `timing.py` | The times, on the machine that builds the site |
| `digests.py` | Hexadecimal digests read back as bytes and bits |
| `cache.py` | `cached(name, key, compute)`: `build/site-cache/<name>.json` |

### `draw/` — the shared figures

| Module | What it draws |
|---|---|
| `style.py` | The two themes (`THEMES`, one accent and its second, validated on both backgrounds), `save()` (both themes, deterministic SVG), axes, and panels: `stage_strip()`, `value_cells()`, `draw_bits()` |
| `robustness_charts.py` | The chart of the nine edits on the example photograph, and its table |
| `corpus_charts.py` | The same over both corpora, separability, the edits that change the picture; two hashes side by side, edit by edit and by separability (`two_hashes_by_edit()`, `two_hashes_separability()`), for a page that compares its algorithm with its nearest relative |
| `timing_tables.py` | The Cost row of each page, its table of times, the footnote naming the machine |
| `markdown.py` | `write_text()`, `table()`: what a page includes beside a figure |

### `pages/` — one module per page

A page module declares how its algorithm's comparison reads and draws its own figures:

| Name | Meaning |
|---|---|
| `ALGO` | `ph_algorithm_name()` of the algorithm; the directory under `docs/assets/generated/` |
| `BITS` | The digest's length in bits, for a hash compared bit by bit; the charts show the bits that differ |
| `METRIC`, `LOWER_IS_CLOSER`, `METRIC_RANGE`, `FORMAT` | With `BITS = None`: the axis label, which way is closer, the range (top `None` when the metric has none), one value's format |
| `REFUSED` | What the library refuses to compare; those comparisons are left out and counted under the table |
| `REFERENCE` | Optional `(value, name)`: a value the metric is read against, dashed on the charts |
| `COST_VARIANTS` | Optional `[(time case, label)]`: more times on the Cost row |
| `figures(tool, image, out_dir)` | Draws the page's own figures into `out_dir/<ALGO>/` |

The robustness, corpus, separability, edits and time figures of every page are drawn by
`render.py` from these declarations; a page module draws only what is its own.

A **topic page** (`pages/preparation.py`, `pages/comparing.py`, `pages/choosing.py`, in `TOPICS`) has no hash
of its own: it declares `NAME`, the directory under `docs/assets/generated/`, and
`figures(tool, image, out_dir, timing)`, which draws everything it shows, `timing` being
what `measure/timing.py` measured. `render.py --algo preparation` draws it alone. A topic
page that compares the algorithms reads the measurements of their pages
(`measure_corpus()`, from the cache) through the page modules in `PAGES`, and names them
by `TITLES` (`pages/__init__.py`).

## Measurements, caches and determinism

- **The library checks every picture of a step.** A stage mode fails the build when its
  recomputation differs from `ph_compute_*()`, for every parameter and mode it shows and
  for the decoder's grayscale.
- **The edits are one registry.** An edit added to `measure/transforms.py` appears on every
  page. An edit that answers one page's own question (a tint of one level, shuffled
  pixels) is made and measured in that page's module instead.
- **Measurements are cached** in `build/site-cache/<name>.json` under a key of every
  file that decides them (`measure/cache.py`: the library, `stages/`, the measuring
  modules, the images; for the times, also the machine). A rebuild with nothing changed
  measures nothing; `rm -rf build/site-cache` measures everything again (about three
  minutes, most of it the photographs).
- **Output is byte-for-byte reproducible**: SVG text stays text, element ids are salted,
  no dates are written, random edits have fixed seeds, and the times come from the cache.
  A refactoring here can be checked by drawing into two directories and comparing them
  (`diff -r`).

## Code on the pages

The pages show the code that ran, collapsed under each chart, by including sections of
these files:

````markdown
```c title="tools/site/stages/measure.c"
--8<-- "tools/site/stages/measure.c:measure"
```
````

A section is delimited by `# --8<-- [start:name]` and `# --8<-- [end:name]` in Python, and
by `/* --8<-- [start:name] */` and `/* --8<-- [end:name] */` in C; the marker lines do not
reach the page. Moving code into another file or renaming a section means updating the
pages that include it (`git grep -- '--8<-- "tools/site/'`); the strict build fails on an
include it cannot find. A section shows a reader what was computed, so it keeps the
comments that explain it.

## Running it

```sh
make site                    # everything, strict, into build/site/
make site-serve              # the same, served at http://127.0.0.1:8000/libphash/

# One page's figures, into a scratch directory, with a PNG preview of each on its theme's
# background (an SVG is transparent, and an image viewer shows it on the wrong one):
SITE_PREVIEW=/tmp/preview build/site-venv/bin/python tools/site/render.py \
    --tool build/release/site_stages --image tests/data/photo.jpeg \
    --out build/site-figures --algo phash

build/release/site_stages                          # the list of modes
build/release/site_stages measure a.png b.png …     # any images, by every algorithm
```

`scripts/site.sh` creates the Python environment (`build/site-venv/`, pinned by
`scripts/site-requirements.txt`) on first use.

## Extending it

**A new algorithm page.** A stage mode in `stages/<algo>.c` that writes the stages and
checks the result against the library, registered in `main.c` and declared in
`stages.h`; a module `pages/<algo>.py` with the declarations above and `figures()`,
added to `PAGES`; the page itself, `docs/theory/<algo>.md`, in the site's navigation.

**A figure that needs a new number from the library.** A field in the stage mode's JSON,
or, for a measurement over the corpora, a mode `<algo>-<what>` built on
`for_each_image()` and read through `measure_settings()` (every original and its copies,
cached) or `run_lines()`.

**A new edit.** One entry in `transforms()` or `content_edits()`; every page's charts and
tables take it up, and every cached corpus is measured again.

**A new time.** One row of `time_cases[]` in `stages/timing.c`, named `<algo>_<what>` to
appear in bold in that algorithm's Cost table.

**A new color.** Only after checking it against both of the site's backgrounds with the
palette validator; the theme colors are in `draw/style.py`.
