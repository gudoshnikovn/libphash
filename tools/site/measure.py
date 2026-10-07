"""The robustness measurement: every transform applied to one image, every variant hashed
and compared with the original by tools/site/stages.c, drawn as small multiples and
written as the table a page shows under the chart."""
import json
import os
import subprocess
import tempfile

import matplotlib.pyplot as plt
from PIL import Image

from common import save, style_axes
from transforms import transforms


# --8<-- [start:measure]
def measure_variants(tool, base, ref):
    """{algorithm: {transform: [(strength, value)]}}: every edit of `base` (a Pillow
    image) compared with `ref`, the same pixels saved losslessly.

    Every variant is written to a file, and `site_stages measure` hashes them with the
    library and compares each variant with the reference, by each algorithm's own
    metric; `value` is None where the comparison does not apply.
    """
    with tempfile.TemporaryDirectory() as tmp:
        files, keys = [], []
        for name, _, steps in transforms():
            for strength, op in steps:
                im, (ext, q) = op(base)
                path = os.path.join(tmp, f"{len(files)}.{ext}")
                if ext == "jpg":
                    im.save(path, quality=q)
                else:
                    im.save(path)
                files.append(path)
                keys.append((name, strength))
        out = subprocess.run([tool, "measure", ref, *files], check=True, capture_output=True,
                             text=True).stdout
    rows = [json.loads(line) for line in out.splitlines()]
    result = {}
    for (name, strength), row in zip(keys, rows):
        for algo, value in row.items():
            if algo != "file":
                result.setdefault(algo, {}).setdefault(name, []).append((strength, value))
    return result


def measure_robustness(tool, image):
    """measure_variants() for one image file."""
    base = Image.open(image).convert("RGB")
    with tempfile.TemporaryDirectory() as tmp:
        ref = os.path.join(tmp, "reference.ppm")
        base.save(ref)
        return measure_variants(tool, base, ref)
# --8<-- [end:measure]


# --8<-- [start:bits]
def bits_that_differ(similarity, bits_total):
    """The number on a bit hash's chart. ph_similarity_digest() is 1 - distance / bits,
    so this is the Hamming distance itself: how many of the hash's bits the edit flipped."""
    return (1.0 - similarity) * bits_total
# --8<-- [end:bits]


def robustness_figure(data, algo, out_dir, bits_total, caption):
    """Small multiples, one panel per transform: bits that differ from the original."""
    specs = transforms()

    def fig(c):
        figure, axes = plt.subplots(3, 3, figsize=(10, 7.2), sharey=True)
        for ax, (name, xlabel, _) in zip(axes.flat, specs):
            pts = [(s, bits_that_differ(v, bits_total)) for s, v in data[algo][name]
                   if v is not None]
            xs = list(range(len(pts)))
            ax.plot(xs, [p[1] for p in pts], color=c["accent"], linewidth=2, marker="o",
                    markersize=4.5, solid_capstyle="round")
            ax.set_xticks(xs, [f"{p[0]:g}" for p in pts])
            ax.set_ylim(0, bits_total / 2)
            ax.set_title(name, color=c["ink"], fontsize=10)
            ax.set_xlabel(xlabel, color=c["muted"], fontsize=9)
            style_axes(ax, c)
        for ax in axes[:, 0]:
            ax.set_ylabel("bits that differ", color=c["muted"], fontsize=9)
        figure.text(0.5, -0.03, caption, ha="center", color=c["muted"], fontsize=9)
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out_dir, algo), "robustness")


def robustness_table(data, algo, out_dir, bits_total):
    lines = ["| Transform | Strength | Bits that differ |", "|---|---|---|"]
    for name, _, _ in transforms():
        for strength, value in data[algo][name]:
            cell = "—" if value is None else f"{round(bits_that_differ(value, bits_total))}"
            lines.append(f"| {name} | {strength:g} | {cell} |")
    path = os.path.join(out_dir, algo, "robustness-table.md")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
