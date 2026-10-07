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
from transforms import content_edits, transforms


# --8<-- [start:measure]
def measure_variants(tool, base, ref):
    """{algorithm: {transform: [(strength, value)]}}: every edit of `base` (a Pillow
    image), of both groups of transforms.py, compared with `ref`, the same pixels saved
    losslessly.

    Every variant is written to a file, and `site_stages measure` hashes them with the
    library and compares each variant with the reference, by each algorithm's own
    metric; `value` is None where the comparison does not apply.
    """
    with tempfile.TemporaryDirectory() as tmp:
        files, keys = [], []
        for name, _, steps in transforms() + content_edits():
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


def metric(mod):
    """How an algorithm page module's metric reads: (axis label, lower is closer, raw value
    -> value on the chart, the metric's range, format of one value).

    A bit hash sets BITS, and its charts show the bits that differ, lower closer. A
    module with BITS = None names its own METRIC, LOWER_IS_CLOSER, METRIC_RANGE (whose top
    is None for a metric without one) and FORMAT, and the raw value is the chart's value.
    """
    if mod.BITS:
        return ("bits that differ", True, lambda v: bits_that_differ(v, mod.BITS),
                (0, mod.BITS), lambda v: f"{round(float(v), 1):g}")
    return (mod.METRIC, mod.LOWER_IS_CLOSER, lambda v: v, mod.METRIC_RANGE,
            lambda v: mod.FORMAT.format(v))


def robust_limits(mod, values=()):
    """The y range of a robustness chart. For bits, half of them, where unrelated images
    land, raised if an edit goes past it; for another metric, its whole range, widened to
    any value outside it."""
    if mod.BITS:
        return 0, max([mod.BITS / 2] + [v * 1.05 for v in values])
    lo, hi = close_range(mod.METRIC_RANGE, values)
    return min([lo] + list(values)), max([hi] + list(values))


def close_range(limits, values):
    """A metric's range, with an open top (None, ColorMoments' L2 distance) closed just
    above the largest of `values`."""
    lo, hi = limits
    if hi is None:
        hi = max(values, default=1.0) * 1.05
    return lo, hi


def reference_line(ax, mod, c):
    """The module's REFERENCE, a value the metric is read against (Radial's threshold), as
    a dashed line; nothing for a module without one. reference_note() names it."""
    ref = getattr(mod, "REFERENCE", None)
    if ref:
        ax.axhline(ref[0], color=c["muted"], linewidth=1, linestyle=(0, (4, 3)))


def reference_note(mod):
    """The caption's sentence for reference_line(); empty without one."""
    ref = getattr(mod, "REFERENCE", None)
    return f" Dashed: {ref[1]}, {ref[0]:g}." if ref else ""


def robustness_figure(data, algo, out_dir, mod, caption):
    """Small multiples, one panel per transform: the algorithm's metric against the
    original (for a bit hash, the bits that differ)."""
    specs = transforms()
    label, _, convert, _, _ = metric(mod)
    limits = robust_limits(mod, [convert(v) for name, _, _ in specs for _, v in data[algo][name]
                                 if v is not None])

    def fig(c):
        figure, axes = plt.subplots(3, 3, figsize=(10, 7.2), sharey=True)
        for ax, (name, xlabel, _) in zip(axes.flat, specs):
            pts = [(s, convert(v)) for s, v in data[algo][name] if v is not None]
            xs = list(range(len(pts)))
            reference_line(ax, mod, c)
            ax.plot(xs, [p[1] for p in pts], color=c["accent"], linewidth=2, marker="o",
                    markersize=4.5, solid_capstyle="round")
            ax.set_xticks(xs, [f"{p[0]:g}" for p in pts])
            ax.set_ylim(*limits)
            ax.set_title(name, color=c["ink"], fontsize=10)
            ax.set_xlabel(xlabel, color=c["muted"], fontsize=9)
            style_axes(ax, c)
        for ax in axes[:, 0]:
            ax.set_ylabel(label, color=c["muted"], fontsize=9)
        figure.text(0.5, -0.03, caption + reference_note(mod), ha="center", color=c["muted"],
                    fontsize=9)
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out_dir, algo), "robustness")


def robustness_table(data, algo, out_dir, mod):
    label, _, convert, _, fmt = metric(mod)
    head = "Bits that differ" if mod.BITS else label[0].upper() + label[1:]
    lines = [f"| Transform | Strength | {head} |", "|---|---|---|"]
    for name, _, _ in transforms():
        for strength, value in data[algo][name]:
            cell = ("—" if value is None else f"{round(convert(value))}" if mod.BITS
                    else fmt(value))
            lines.append(f"| {name} | {strength:g} | {cell} |")
    path = os.path.join(out_dir, algo, "robustness-table.md")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
