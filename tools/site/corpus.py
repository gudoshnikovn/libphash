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
them. The result is cached in build/site-cache/<corpus>.json under a key computed from
everything that decides it, so a rebuild with no change to the library, the tool, the
edits or the corpus measures nothing.
"""
import concurrent.futures
import glob
import hashlib
import json
import os
import subprocess
import sys
import tempfile

from PIL import Image

import fetch_corpus
from corpus_charts import COPY_STRENGTHS
from measure import measure_variants
from transforms import content_edits, transforms

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CACHE = os.path.join(ROOT, "build", "site-cache")

# What a chart says it was drawn from.
LABELS = {
    "synthetic": "synthetic corpus, {n} images; not photographs",
    "photos": "{n} public-domain photographs from Wikimedia Commons",
}

# The files whose content decides a measurement: a change to any of them measures again.
KEY_FILES = ["src/**/*.c", "src/**/*.h", "include/libphash.h", "tests/src/synthetic_corpus.h",
             "tools/site/stages.c", "tools/site/transforms.py", "tools/site/measure.py",
             "tools/site/corpus.py"]


def cache_key(images):
    h = hashlib.sha256()
    for pattern in KEY_FILES:
        for path in sorted(glob.glob(os.path.join(ROOT, pattern), recursive=True)):
            h.update(os.path.relpath(path, ROOT).encode())
            with open(path, "rb") as f:
                h.update(hashlib.sha256(f.read()).digest())
    for path in images:
        with open(path, "rb") as f:
            h.update(hashlib.sha256(f.read()).digest())
    return h.hexdigest()


def images(tool, corpus, workdir):
    """The corpus's image files, in a fixed order; an empty list when none are available."""
    if corpus == "synthetic":
        out = os.path.join(workdir, "synthetic")
        os.makedirs(out, exist_ok=True)
        subprocess.run([tool, "corpus", out], check=True)
        return sorted(glob.glob(os.path.join(out, "*.ppm")))
    paths, problems = fetch_corpus.fetch()
    for msg in problems:
        print(f"warning: photo corpus: {msg}", file=sys.stderr)
    return paths


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
    os.makedirs(CACHE, exist_ok=True)
    workdir = os.path.join(CACHE, "work")
    paths = images(tool, corpus, workdir)
    key = cache_key(paths)
    cached = os.path.join(CACHE, f"{corpus}.json")
    if os.path.exists(cached):
        with open(cached) as f:
            data = json.load(f)
        if data.get("key") == key:
            print(f"render: {corpus} corpus: unchanged, measured values from the cache")
            return data
    print(f"render: {corpus} corpus: measuring {len(paths)} images")

    with tempfile.TemporaryDirectory() as tmp:
        # The originals are decoded once, by Pillow, and written losslessly, so that the
        # reference an edit is compared with and the image that is edited are the same
        # pixels.
        refs = [os.path.join(tmp, f"{i:04d}.ppm") for i in range(len(paths))]
        with concurrent.futures.ProcessPoolExecutor() as pool:
            per_image = list(pool.map(_measure_one, [(tool, p, r) for p, r in
                                                     zip(paths, refs)]))
        rows = []
        if len(refs) >= 2:
            out = subprocess.run([tool, "pairs", *refs], check=True, capture_output=True,
                                 text=True).stdout
            rows = [json.loads(line) for line in out.splitlines()]

    robust, edits = {}, {}
    content = {name for name, _, _ in content_edits()}
    for result in per_image:
        for algo, by_transform in result.items():
            for name, points in by_transform.items():
                group = edits if name in content else robust
                slot = group.setdefault(algo, {}).setdefault(name, [[s, []] for s, _ in points])
                for (strength, value), cell in zip(points, slot):
                    cell[1].append(value)
    different = {}
    for row in rows:
        for algo, value in row.items():
            if algo not in ("a", "b"):
                different.setdefault(algo, []).append(value)

    data = {"key": key, "n": len(paths), "label": LABELS[corpus].format(n=len(paths)),
            "robust": robust, "edits": edits, "different": different}
    with open(cached, "w") as f:
        json.dump(data, f)
    return data
# --8<-- [end:corpus]


# --8<-- [start:variants]
def write_copies(base, tmp, stem):
    """The copies of the separability measurement: one moderate strength of each of the
    nine edits (COPY_STRENGTHS), saved as measure.py saves them; their paths."""
    files = []
    for name, _, steps in transforms():
        op = dict(steps)[COPY_STRENGTHS[name]]
        im, (ext, q) = op(base)
        path = os.path.join(tmp, f"{stem}-{len(files)}.{ext}")
        im.save(path, **({"quality": q} if ext == "jpg" else {}))
        files.append(path)
    return files


def _variants_one(args):
    """One original and its copies, through `site_stages <mode>`: one row per file."""
    tool, mode, path, tmp, stem = args
    base = Image.open(path).convert("RGB")
    ref = os.path.join(tmp, f"{stem}.ppm")
    base.save(ref)
    files = [ref] + write_copies(base, tmp, stem)
    rows = subprocess.run([tool, mode, *files], check=True, capture_output=True,
                          text=True).stdout.splitlines()
    for f in files:
        os.remove(f)
    return [{k: v for k, v in json.loads(r).items() if k != "file"} for r in rows]


def measure_settings(tool, mode):
    """{corpus: {"n", "label", "images": [[original, copy…] rows per image]}}: every
    original of both corpora and its nine copies through `site_stages <mode>`, which
    prints one JSON row of digests per file, one digest per setting it tries. Cached like
    the corpora themselves, in build/site-cache/<mode>-<corpus>.json."""
    result = {}
    for key in ("synthetic", "photos"):
        paths = images(tool, key, os.path.join(CACHE, "work"))
        label = LABELS[key].format(n=len(paths))
        if not paths:
            result[key] = {"n": 0, "label": label, "images": []}
            continue
        cache = os.path.join(CACHE, f"{mode}-{key}.json")
        ck = cache_key(paths)
        if os.path.exists(cache):
            with open(cache) as f:
                saved = json.load(f)
            if saved.get("key") == ck:
                result[key] = {"n": len(paths), "label": label, "images": saved["images"]}
                continue
        print(f"render: {mode}: measuring {len(paths)} images and their copies")
        with tempfile.TemporaryDirectory() as tmp, \
                concurrent.futures.ProcessPoolExecutor() as pool:
            rows = list(pool.map(_variants_one, [(tool, mode, p, tmp, f"{i:04d}")
                                                 for i, p in enumerate(paths)]))
        with open(cache, "w") as f:
            json.dump({"key": ck, "images": rows}, f)
        result[key] = {"n": len(paths), "label": label, "images": rows}
    return result
# --8<-- [end:variants]
