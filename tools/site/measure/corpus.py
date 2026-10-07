"""The corpora the site measures over, and the measurement itself.

Two corpora, each labeled on every chart drawn from it:

- synthetic: the 24 generated images of the tests (tests/src/synthetic_corpus.h), written
  by `site_stages corpus`;
- photos: the public-domain photographs of tools/site/corpus_photos.tsv, downloaded by
  fetch_corpus.py. Without a network and without a cached copy there are none; the
  charts then say so, and the build goes on.

For every image of a corpus, every edit of transforms.py is measured against the
original (robustness), and every pair of distinct originals is compared (discrimination).
Both are kept as raw values, per image and per pair, so a page can draw any statistic of
them. The result is cached (measure/cache.py) under a key computed from everything that
decides it, so a rebuild with no change to the library, the tool, the edits or the
corpus measures nothing.
"""
import concurrent.futures
import glob
import os
import subprocess
import sys
import tempfile

from PIL import Image

import fetch_corpus
from measure.cache import CACHE, LIBRARY, cached, fingerprint
from measure.robustness import measure_variants
from measure.separability import COPY_STRENGTHS
from measure.tool import run_lines
from measure.transforms import content_edits, transforms, write_edit

CORPORA = ("synthetic", "photos")

# What a chart says it was drawn from.
LABELS = {
    "synthetic": "synthetic corpus, {n} images; not photographs",
    "photos": "{n} public-domain photographs from Wikimedia Commons",
}

# The corpora by a short name, for a table's first column or a figure's title.
SHORT = {"photos": "photographs", "synthetic": "synthetic images"}

# The files whose content decides a measurement over a corpus, beside the library and the
# images: a change to any of them measures again.
KEY_FILES = LIBRARY + ["tests/src/synthetic_corpus.h"] + [
    f"tools/site/measure/{m}.py" for m in ("tool", "transforms", "robustness", "corpus",
                                            "separability")]


def cache_key(images, extra="", also=()):
    """The key of a measurement over `images` (paths): KEY_FILES, the files `also` names
    (the page module whose own code measures, say), and `extra`."""
    return fingerprint(KEY_FILES + list(also), images, extra)


def images(tool, corpus):
    """The corpus's image files, in a fixed order; an empty list when none are available."""
    if corpus == "synthetic":
        out = os.path.join(CACHE, "work", "synthetic")
        os.makedirs(out, exist_ok=True)
        subprocess.run([tool, "corpus", out], check=True)
        return sorted(glob.glob(os.path.join(out, "*.ppm")))
    paths, problems = fetch_corpus.fetch()
    for msg in problems:
        print(f"warning: photo corpus: {msg}", file=sys.stderr)
    return paths


def label(corpus, n):
    return LABELS[corpus].format(n=n)


def _measure_one(args):
    tool, path, ref = args
    base = Image.open(path).convert("RGB")
    base.save(ref)
    return measure_variants(tool, base, ref)


# --8<-- [start:corpus]
def measure_corpus(tool, corpus):
    """{"n", "label", "robust": {algo: {transform: [[strength, [value per image]]]}},
    "edits": {the same, for the edits that change the picture}, "different": {algo:
    [value per pair]}} for one corpus, from the cache when nothing that decides it has
    changed. Values are each algorithm's own metric, as `site_stages measure` and
    `site_stages pairs` print it; None where it does not apply."""
    paths = images(tool, corpus)

    def measure():
        with tempfile.TemporaryDirectory() as tmp:
            # The originals are decoded once, by Pillow, and written losslessly, so that
            # the reference an edit is compared with and the image that is edited are the
            # same pixels.
            refs = [os.path.join(tmp, f"{i:04d}.ppm") for i in range(len(paths))]
            with concurrent.futures.ProcessPoolExecutor() as pool:
                per_image = list(pool.map(_measure_one, [(tool, p, r) for p, r in
                                                         zip(paths, refs)]))
            rows = run_lines(tool, "pairs", *refs) if len(refs) >= 2 else []

        robust, edits = {}, {}
        content = {name for name, _, _ in content_edits()}
        for result in per_image:
            for algo, by_transform in result.items():
                for name, points in by_transform.items():
                    group = edits if name in content else robust
                    slot = group.setdefault(algo, {}).setdefault(
                        name, [[s, []] for s, _ in points])
                    for (strength, value), cell in zip(points, slot):
                        cell[1].append(value)
        different = {}
        for row in rows:
            for algo, value in row.items():
                if algo not in ("a", "b"):
                    different.setdefault(algo, []).append(value)
        return {"n": len(paths), "label": label(corpus, len(paths)), "robust": robust,
                "edits": edits, "different": different}

    return cached(corpus, cache_key(paths), measure, f"{corpus} corpus, {len(paths)} images")
# --8<-- [end:corpus]


# --8<-- [start:variants]
def write_copies(base, tmp, stem):
    """The copies of the separability measurement: one moderate strength of each of the
    nine edits (COPY_STRENGTHS), saved as measure/robustness.py saves them; their paths."""
    return [write_edit(dict(steps)[COPY_STRENGTHS[name]], base,
                       os.path.join(tmp, f"{stem}-{k}"))
            for k, (name, _, steps) in enumerate(transforms())]


def _variants_one(args):
    """One original and its copies, through `site_stages <mode>`: one row per file."""
    tool, mode, path, tmp, stem = args
    base = Image.open(path).convert("RGB")
    ref = os.path.join(tmp, f"{stem}.ppm")
    base.save(ref)
    files = [ref] + write_copies(base, tmp, stem)
    rows = run_lines(tool, mode, *files)
    for f in files:
        os.remove(f)
    return [{k: v for k, v in row.items() if k != "file"} for row in rows]


def measure_settings(tool, mode):
    """{corpus: {"n", "label", "images": [[original, copy…] rows per image]}}: every
    original of both corpora and its nine copies through `site_stages <mode>`, which
    prints one JSON row of digests per file, one digest per setting it tries. Cached like
    the corpora themselves, as <mode>-<corpus>."""
    result = {}
    for corpus in CORPORA:
        paths = images(tool, corpus)
        rows = []
        if paths:
            def measure():
                with tempfile.TemporaryDirectory() as tmp, \
                        concurrent.futures.ProcessPoolExecutor() as pool:
                    return list(pool.map(_variants_one, [(tool, mode, p, tmp, f"{i:04d}")
                                                         for i, p in enumerate(paths)]))
            rows = cached(f"{mode}-{corpus}", cache_key(paths), measure,
                          f"{mode}, {len(paths)} images and their copies")
        result[corpus] = {"n": len(paths), "label": label(corpus, len(paths)), "images": rows}
    return result
# --8<-- [end:variants]
