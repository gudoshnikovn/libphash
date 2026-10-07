"""What a corpus measurement shows on an algorithm's page: robustness as a spread over the
corpus, and separability, the gap between copies and different images.

Each algorithm page module says how its metric reads: BITS for a bit hash (the chart
shows the bits that differ, lower is closer); a module with BITS = None names its own
METRIC label and LOWER_IS_CLOSER.
"""
import os

import matplotlib.pyplot as plt
import numpy as np

from common import save, style_axes
from measure import bits_that_differ
from transforms import transforms

# --8<-- [start:copies]
# A "copy" is an original after one moderate edit: one strength of each transform, the
# kind of change a picture goes through between two places it is stored.
COPY_STRENGTHS = {
    "JPEG quality": 50,
    "Downscale": 0.5,
    "Rotation": 2,
    "Brightness": 1.15,
    "Contrast": 1.15,
    "Gamma": 1.2,
    "Gaussian blur": 1,
    "Noise": 5,
    "Crop": 5,
}
# --8<-- [end:copies]

RECALL = 0.95


def metric(mod):
    """(axis label, lower is closer, raw value -> value on the chart, axis limits)."""
    if mod.BITS:
        return ("bits that differ", True, lambda v: bits_that_differ(v, mod.BITS),
                (0, mod.BITS))
    return (mod.METRIC, mod.LOWER_IS_CLOSER, lambda v: v, mod.METRIC_RANGE)


# --8<-- [start:separability]
def separability(copies, different, lower_is_closer):
    """d', the threshold that keeps RECALL of the copies, and the share of different
    pairs that threshold also accepts.

    d' is the gap between the two means in units of their pooled spread, as
    tests/src/test_hash_properties.c defines it (separability()), signed so that a
    positive d' means copies are closer than different images.
    """
    c, d = np.asarray(copies, float), np.asarray(different, float)
    sign = 1.0 if lower_is_closer else -1.0
    pooled = np.sqrt((c.std() ** 2 + d.std() ** 2) / 2.0)
    dprime = sign * (d.mean() - c.mean()) / pooled if pooled > 0 else float("inf")
    # The threshold: the closest value that still accepts RECALL of the copies.
    t = sign * np.quantile(sign * c, RECALL, method="inverted_cdf")
    accepted = (d <= t) if lower_is_closer else (d >= t)
    return dprime, t, accepted.mean()
# --8<-- [end:separability]


def copies_and_different(data, algo, convert):
    copies = []
    for name, points in data["robust"][algo].items():
        for strength, values in points:
            if strength == COPY_STRENGTHS[name]:
                copies += [convert(v) for v in values if v is not None]
    different = [convert(v) for v in data["different"][algo] if v is not None]
    return copies, different


def corpus_robustness_figure(datasets, algo, mod, out_dir):
    """Small multiples, one panel per transform: the median over each corpus, with the
    band from the 25th to the 75th percentile."""
    label, _, convert, limits = metric(mod)
    specs = transforms()
    available = [(k, d) for k, d in datasets if d["n"]]

    def fig(c):
        colors = {"photos": c["accent"], "synthetic": c["accent2"]}
        figure, axes = plt.subplots(3, 3, figsize=(10, 7.6), sharey=True)
        for ax, (name, xlabel, steps) in zip(axes.flat, specs):
            xs = list(range(len(steps)))
            for key, data in available:
                med, lo, hi = [], [], []
                for strength, values in data["robust"][algo][name]:
                    v = [convert(x) for x in values if x is not None]
                    q = np.percentile(v, [25, 50, 75]) if v else [np.nan] * 3
                    lo.append(q[0])
                    med.append(q[1])
                    hi.append(q[2])
                ax.fill_between(xs, lo, hi, color=colors[key], alpha=0.18, linewidth=0)
                ax.plot(xs, med, color=colors[key], linewidth=2, marker="o", markersize=4,
                        solid_capstyle="round")
            ax.set_xticks(xs, [f"{s:g}" for s, _ in steps])
            ax.set_ylim(limits[0], limits[0] + (limits[1] - limits[0]) / 2)
            ax.set_title(name, color=c["ink"], fontsize=10)
            ax.set_xlabel(xlabel, color=c["muted"], fontsize=9)
            style_axes(ax, c)
        for ax in axes[:, 0]:
            ax.set_ylabel(label, color=c["muted"], fontsize=9)
        figure.tight_layout(rect=(0, 0, 1, 0.95))
        handles = [plt.Line2D([], [], color=colors[k], linewidth=2, marker="o", markersize=4)
                   for k, _ in available]
        figure.legend(handles, [d["label"] for _, d in available], loc="upper center",
                      ncol=len(available), frameon=False, labelcolor=c["ink"], fontsize=9.5,
                      bbox_to_anchor=(0.5, 1.0))
        missing = [d for k, d in datasets if not d["n"]]
        note = ("Line: median over the corpus; band: the middle half of its images "
                "(25th to 75th percentile).")
        if missing:
            note += " The photographs were not available to this build."
        figure.text(0.5, -0.01, note, ha="center", color=c["muted"], fontsize=9)
        return figure

    save(fig, os.path.join(out_dir, algo), "robustness-corpus")


def separability_figure(datasets, algo, mod, out_dir):
    """Per corpus, the distribution of copies against the distribution of different
    images, on one axis, with the threshold that keeps RECALL of the copies."""
    label, lower, convert, limits = metric(mod)

    def fig(c):
        figure, axes = plt.subplots(1, len(datasets), figsize=(10, 3.6), sharey=False)
        for ax, (key, data) in zip(np.atleast_1d(axes), datasets):
            style_axes(ax, c)
            ax.set_xlim(*limits)
            ax.set_xlabel(label, color=c["muted"], fontsize=9)
            if not data["n"]:
                ax.set_title(LABEL_MISSING, color=c["muted"], fontsize=9.5)
                ax.set_yticks([])
                continue
            copies, different = copies_and_different(data, algo, convert)
            dprime, t, fmr = separability(copies, different, lower)
            bins = (np.arange(limits[0], limits[1] + 2) - 0.5 if mod.BITS
                    else np.linspace(*limits, 41))
            for values, color, name in ((different, c["muted"], "different images"),
                                        (copies, c["accent"], "copies")):
                w = np.full(len(values), 100.0 / len(values))
                ax.hist(values, bins=bins, weights=w, color=color, alpha=0.30, linewidth=0)
                hist, edges = np.histogram(values, bins=bins, weights=w)
                ax.stairs(hist, edges, color=color, linewidth=1.5)
                peak = int(np.argmax(hist))
                # A label beside its peak, on the side away from the y axis.
                at_edge = peak < len(hist) // 8
                ax.annotate(name, ((edges[peak] + edges[peak + 1]) / 2, hist[peak]),
                            xytext=(8 if at_edge else 0, -4 if at_edge else 4),
                            textcoords="offset points", ha="left" if at_edge else "center",
                            va="top" if at_edge else "bottom", color=c["ink"], fontsize=9,
                            bbox={"facecolor": c["surface"], "edgecolor": "none", "pad": 1})
            ax.axvline(t, color=c["ink"], linewidth=1, linestyle=(0, (4, 3)))
            ax.annotate(f"threshold {t:g}: accepts\n{RECALL:.0%} of copies and\n"
                        f"{share(fmr, len(different))} of different pairs",
                        (t, 0.97), xycoords=("data", "axes fraction"), xytext=(6, 0),
                        textcoords="offset points", va="top", color=c["ink"], fontsize=8.5)
            # Headroom above the tallest bar for the threshold's label.
            ax.set_ylim(0, ax.get_ylim()[1] * 1.45)
            ax.set_title(f"{data['label']}\nd′ = {dprime:.2f}", color=c["ink"], fontsize=9.5)
        np.atleast_1d(axes)[0].set_ylabel("% of pairs", color=c["muted"], fontsize=9)
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out_dir, algo), "separability")


LABEL_MISSING = "photographs: not available to this build"


def share(fraction, n):
    """A share of n pairs, with the count when the percentage alone would round to 0."""
    k = round(fraction * n)
    return f"{fraction:.1%}" if fraction >= 0.001 else f"{k} of {n:,}"


def corpus_tables(datasets, algo, mod, out_dir):
    """The numbers behind both corpus figures, as Markdown."""
    label, lower, convert, _ = metric(mod)
    lines = []
    lines += ["| Corpus | Copies | Different pairs | d′ | Threshold | Different pairs within it |",
              "|---|---|---|---|---|---|"]
    for key, data in datasets:
        if not data["n"]:
            lines.append(f"| {LABEL_MISSING} | — | — | — | — | — |")
            continue
        copies, different = copies_and_different(data, algo, convert)
        dprime, t, fmr = separability(copies, different, lower)
        lines.append(f"| {data['label']} | {len(copies)} | {len(different)} | {dprime:.2f} | "
                     f"{t:g} | {share(fmr, len(different))} |")
    lines += ["", f"Median {label} over each corpus, with the 25th and 75th percentiles:", ""]
    present = [(k, d) for k, d in datasets if d["n"]]
    lines.append("| Transform | Strength | " + " | ".join(d["label"] for _, d in present) + " |")
    lines.append("|---|---|" + "---|" * len(present))
    for name, _, steps in transforms():
        for i, (strength, _) in enumerate(steps):
            cells = []
            for _, data in present:
                v = [convert(x) for x in data["robust"][algo][name][i][1] if x is not None]
                q = np.percentile(v, [25, 50, 75])
                cells.append(f"{q[1]:g} ({q[0]:g}–{q[2]:g})")
            lines.append(f"| {name} | {strength:g} | " + " | ".join(cells) + " |")
    path = os.path.join(out_dir, algo, "corpus-table.md")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
