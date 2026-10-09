"""The figure and the table of the site's front page (docs/index.md): the example photograph,
three copies of it and an unrelated photograph, each with the pHash the library computes
for it, and the times of decoding and of the nine hashes on the two example photographs.

The copies are edits of the one registry (measure/transforms.py), at strengths of the
copies the corpora measure; the unrelated photograph is one file of the photo corpus. Each
hash comes from `site_stages phash`, which checks its stages against ph_compute_phash(),
and each distance is checked against `site_stages measure`.
"""
import os
import tempfile

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

import fetch_corpus
from draw.markdown import table, write_text
from draw.style import hide_axes, save, value_cells
from measure.metric import bits_that_differ
from measure.tool import run_lines, run_stages
from measure.transforms import transforms, write_edit
from pages import PAGES, TITLES

NAME = "home"

# The copies on the front page: (edit of transforms(), strength, caption).
COPIES = (
    ("Downscale", 0.5, "half the size"),
    ("JPEG quality", 30, "JPEG quality 30"),
    ("Brightness", 1.3, "30 % brighter"),
)
# The unrelated photograph: a file of the photo corpus (tools/site/corpus_photos.tsv).
OTHER = "Young tabby cat keeping watch.jpg"


def _other_photo():
    """The unrelated photograph's local path, or None when the corpus is not available."""
    for row in fetch_corpus.read_manifest():
        if row["file"] == OTHER:
            path = os.path.join(fetch_corpus.cache_dir(), fetch_corpus.local_name(row))
            return path if os.path.exists(path) else None
    raise SystemExit(f"home: {OTHER} is not in the photo corpus manifest")


def _phash(tool, path):
    """pHash of one file, as `site_stages phash` computes and checks it: (hex, 64 bits)."""
    stages, _ = run_stages(tool, "phash", path)
    h = int(stages["hash"], 16)
    return stages["hash"], np.array([(h >> (63 - k)) & 1 for k in range(64)], dtype=np.uint8)


def hero(tool, image, out):
    """The front page's figure: each picture above its pHash as an 8×8 grid of bits, the
    bits that differ from the original's outlined, and the count under it."""
    base = Image.open(image).convert("RGB")
    ops = {(name, s): op for name, _, steps in transforms() for s, op in steps}
    with tempfile.TemporaryDirectory() as tmp:
        ref = os.path.join(tmp, "original.ppm")
        base.save(ref)
        panels = [("the original", ref)]
        for name, strength, caption in COPIES:
            path = write_edit(ops[(name, strength)], base, os.path.join(tmp, name))
            panels.append((caption, path))
        other = _other_photo()
        if other:
            panels.append(("a different photograph", other))
        pictures = [np.asarray(Image.open(p).convert("RGB")) for _, p in panels]
        hashes = [_phash(tool, p) for _, p in panels]
        measured = run_lines(tool, "measure", ref, *[p for _, p in panels[1:]])
    distances = [0] + [int((a ^ hashes[0][1]).sum()) for _, a in hashes[1:]]
    for d, row in zip(distances[1:], measured):
        if round(bits_that_differ(row["phash"], 64)) != d:
            raise SystemExit(f"home: pHash distance {d} differs from site_stages measure")

    def fig(c):
        n = len(panels)
        f, axes = plt.subplots(2, n, figsize=(2.6 * n, 5.4),
                               gridspec_kw={"height_ratios": [1, 1]})
        for k, ((caption, _), picture, (hexhash, bits), d) in enumerate(
                zip(panels, pictures, hashes, distances)):
            top, bottom = axes[0][k], axes[1][k]
            h, w = picture.shape[:2]
            side = min(h, w)  # a square crop from the center, so the panels line up
            y0, x0 = (h - side) // 2, (w - side) // 2
            top.imshow(picture[y0:y0 + side, x0:x0 + side])
            hide_axes(top)
            top.set_title(caption, color=c["ink"], fontsize=10)
            value_cells(bottom, [0] * 64, bits, c, 8, fontsize=0,
                        changed=bits != hashes[0][1], changed_width=1.6)
            label = "pHash" if k == 0 else f"{d} of 64 bits differ"
            bottom.set_title(label, color=c["accent2"] if k else c["muted"], fontsize=10,
                             y=-0.2)
        f.subplots_adjust(wspace=0.12, hspace=0.02)
        return f
    save(fig, os.path.join(out, NAME), "hero")
    rows = [[caption, f"`{hexhash}`", str(d)]
            for (caption, _), (hexhash, _), d in zip(panels, hashes, distances)]
    note = ("" if other else "\n\nThe unrelated photograph comes from the photo corpus, "
            "which is not available to this build.")
    write_text(os.path.join(out, NAME), "hero-table.md",
               table(["Picture", "pHash", "Bits that differ from the original"], rows) + note)


def times(timing, out):
    """Decoding and each algorithm at its defaults, on the two example photographs."""
    def ms(entry, case):
        v = entry["cases"][case]["min_ms"]
        return f"{v:.2f} ms" if v < 1 else f"{v:.1f} ms" if v < 100 else f"{v:.0f} ms"

    small, large = timing["small"], timing["large"]
    rows = [["decode"] + [ms(e, "decode") for e in (small, large)]]
    rows += [[TITLES[m.ALGO]] + [ms(e, m.ALGO) for e in (small, large)] for m in PAGES]
    write_text(os.path.join(out, NAME), "times.md",
               table(["", f"{small['width']}×{small['height']} JPEG",
                      f"{large['width']}×{large['height']} JPEG"], rows))


def figures(tool, image, out, timing):
    hero(tool, image, out)
    times(timing, out)
