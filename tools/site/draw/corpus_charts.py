"""What a corpus measurement shows on an algorithm's page: robustness as a spread over the
corpus, and separability, the gap between copies and different images.

Each algorithm page module says how its metric reads (measure/metric.py): BITS for a bit
hash (the chart shows the bits that differ, lower is closer); a module with BITS = None
names its own METRIC, LOWER_IS_CLOSER, METRIC_RANGE and FORMAT. A comparison the library
refuses (None, Radial's image with no angular structure) is left out, and the tables say
how many.
"""
import os

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

from draw.markdown import write_text
from draw.robustness_charts import close_range, reference_line, reference_note, robust_limits
from draw.style import hide_axes, save, style_axes
from measure import corpus
from measure.metric import bits_that_differ, metric
from measure.separability import RECALL, copies_and_different, separability
from measure.transforms import content_edits, transforms

LABEL_MISSING = "photographs: not available to this build"


def corpus_robustness_figure(datasets, algo, mod, out_dir):
    """Small multiples, one panel per transform: the median over each corpus, with the
    band from the 25th to the 75th percentile."""
    label, _, convert, limits, _ = metric(mod)
    specs = transforms()
    available = [(k, d) for k, d in datasets if d["n"]]
    if not mod.BITS:
        limits = robust_limits(mod, [convert(x) for _, d in available
                                     for points in d["robust"][algo].values()
                                     for _, values in points for x in values if x is not None])

    def fig(c):
        colors = {"photos": c["accent"], "synthetic": c["accent2"]}
        figure, axes = plt.subplots(3, 3, figsize=(10, 7.6), sharey=True)
        for ax, (name, xlabel, steps) in zip(axes.flat, specs):
            xs = list(range(len(steps)))
            reference_line(ax, mod, c)
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
            ax.set_ylim(limits[0], limits[0] + (limits[1] - limits[0]) / 2 if mod.BITS
                        else limits[1])
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
                "(25th to 75th percentile)." + reference_note(mod))
        if missing:
            note += " The photographs were not available to this build."
        figure.text(0.5, -0.01, note, ha="center", color=c["muted"], fontsize=9)
        return figure

    save(fig, os.path.join(out_dir, algo), "robustness-corpus")


def separability_figure(datasets, algo, mod, out_dir):
    """Per corpus, the distribution of copies against the distribution of different
    images, on one axis, with the threshold that keeps RECALL of the copies."""
    label, lower, convert, limits, fmt = metric(mod)

    def fig(c):
        figure, axes = plt.subplots(1, len(datasets), figsize=(10, 3.6), sharey=False)
        for ax, (key, data) in zip(np.atleast_1d(axes), datasets):
            style_axes(ax, c)
            ax.set_xlim(*close_range(limits, []))
            ax.set_xlabel(label, color=c["muted"], fontsize=9)
            if not data["n"]:
                ax.set_title(LABEL_MISSING, color=c["muted"], fontsize=9.5)
                ax.set_yticks([])
                continue
            copies, different = copies_and_different(data, algo, convert)
            dprime, t, fmr = separability(copies, different, lower)
            lims = close_range(limits, copies + different)
            ax.set_xlim(*lims)
            bins = (np.arange(lims[0], lims[1] + 2) - 0.5 if mod.BITS
                    else np.linspace(*lims, 41))
            for values, color, name in ((different, c["muted"], "different images"),
                                        (copies, c["accent"], "copies")):
                w = np.full(len(values), 100.0 / len(values))
                ax.hist(values, bins=bins, weights=w, color=color, alpha=0.30, linewidth=0)
                hist, edges = np.histogram(values, bins=bins, weights=w)
                ax.stairs(hist, edges, color=color, linewidth=1.5)
                peak = int(np.argmax(hist))
                # A label beside its peak, on the side away from the edge it is near.
                at_left = peak < len(hist) // 8
                at_right = peak >= len(hist) - len(hist) // 8
                ax.annotate(name, ((edges[peak] + edges[peak + 1]) / 2, hist[peak]),
                            xytext=(8 if at_left else -8 if at_right else 0,
                                    -4 if at_left or at_right else 4),
                            textcoords="offset points",
                            ha="left" if at_left else "right" if at_right else "center",
                            va="top" if at_left or at_right else "bottom", color=c["ink"],
                            fontsize=9,
                            bbox={"facecolor": c["surface"], "edgecolor": "none", "pad": 1})
            ax.axvline(t, color=c["ink"], linewidth=1, linestyle=(0, (4, 3)))
            # The threshold's label on the side of the different images.
            ax.annotate(f"threshold {fmt(t)}: accepts\n{RECALL:.0%} of copies and\n"
                        f"{share(fmr, len(different))} of different pairs",
                        (t, 0.97), xycoords=("data", "axes fraction"),
                        xytext=(6 if lower else -6, 0), textcoords="offset points",
                        ha="left" if lower else "right", va="top", color=c["ink"],
                        fontsize=8.5)
            # Headroom above the tallest bar for the threshold's label.
            ax.set_ylim(0, ax.get_ylim()[1] * 1.45)
            ax.set_title(f"{data['label']}\nd′ = {dprime:.2f}", color=c["ink"], fontsize=9.5)
        np.atleast_1d(axes)[0].set_ylabel("% of pairs", color=c["muted"], fontsize=9)
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out_dir, algo), "separability")


def share(fraction, n):
    """A share of n pairs, with the count when the percentage alone would round to 0."""
    k = round(fraction * n)
    return f"{fraction:.1%}" if fraction >= 0.001 else f"{k} of {n:,}"


def refused_note(datasets, algo, mod):
    """A sentence on the comparisons the library refused and the charts leave out, per
    corpus; empty when there are none."""
    parts = []
    for _, data in datasets:
        if not data["n"]:
            continue
        copies = sum(v is None for points in data["robust"][algo].values()
                     for _, values in points for v in values)
        pairs = sum(v is None for v in data["different"][algo])
        if copies or pairs:
            parts.append(f"{data['label']}: {copies} edited images and {pairs} pairs of "
                         "different images")
    if not parts:
        return ""
    return (f"Left out, because the comparison refuses {mod.REFUSED}: " + "; ".join(parts)
            + ".")


def corpus_tables(datasets, algo, mod, out_dir):
    """The numbers behind both corpus figures, as Markdown."""
    label, lower, convert, _, fmt = metric(mod)
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
                     f"{fmt(t)} | {share(fmr, len(different))} |")
    refused = refused_note(datasets, algo, mod)
    if refused:
        lines += ["", refused]
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
                cells.append(f"{fmt(q[1])} ({fmt(q[0])}–{fmt(q[2])})")
            lines.append(f"| {name} | {strength:g} | " + " | ".join(cells) + " |")
    write_text(os.path.join(out_dir, algo), "corpus-table.md", "\n".join(lines))


def _quartiles(values, convert):
    v = [convert(x) for x in values if x is not None]
    return np.percentile(v, [25, 50, 75]) if v else [np.nan] * 3


def _label(strength):
    return strength if isinstance(strength, str) else f"{strength:g}"


# How an edit that changes the picture reads at one strength, in a table or a title.
EDIT_NAMES = {
    "Local patch": "patch over {}% of the frame",
    "Hue rotation": "hue turned {}°",
    "Turn and mirror": "{}",
}


def _edit(name, strength):
    text = EDIT_NAMES[name].format(_label(strength))
    return {"90°": "turned 90°", "180°": "turned 180°", "mirror": "mirrored"}.get(text, text)


def edits_figure(datasets, algo, mod, out_dir):
    """One panel per edit that changes the picture: the median over each corpus with the
    band of its middle half, against the threshold that accepts RECALL of that corpus's
    copies; an edit below its corpus's line would be taken for a copy."""
    label, lower, convert, limits, _ = metric(mod)
    specs = content_edits()
    available = [(k, d) for k, d in datasets if d["n"]]
    thresholds = {k: separability(*copies_and_different(d, algo, convert), lower)[1]
                  for k, d in available}
    different = [convert(v) for _, d in available for v in d["different"][algo]
                 if v is not None]
    limits = close_range(limits, [_quartiles(values, convert)[2] for _, d in available
                                  for points in d["edits"][algo].values()
                                  for _, values in points]
                         + list(thresholds.values()) + [np.median(different)] * bool(different))

    def fig(c):
        colors = {"photos": c["accent"], "synthetic": c["accent2"]}
        figure, axes = plt.subplots(1, len(specs), figsize=(10, 3.9), sharey=True,
                                    gridspec_kw={"width_ratios": [len(s) + 1 for _, _, s in specs]})
        for ax, (name, xlabel, steps) in zip(axes, specs):
            xs = np.arange(len(steps))
            categorical = any(isinstance(st, str) for st, _ in steps)
            for n, (key, data) in enumerate(available):
                q = np.array([_quartiles(values, convert)
                              for _, values in data["edits"][algo][name]])
                # Side by side where the points have no order to connect.
                x = xs + (n - (len(available) - 1) / 2) * 0.18 if categorical else xs
                if categorical:
                    ax.vlines(x, q[:, 0], q[:, 2], color=colors[key], linewidth=5, alpha=0.3)
                    ax.plot(x, q[:, 1], linestyle="none", color=colors[key], marker="o",
                            markersize=5)
                else:
                    ax.fill_between(x, q[:, 0], q[:, 2], color=colors[key], alpha=0.18,
                                    linewidth=0)
                    ax.plot(x, q[:, 1], color=colors[key], linewidth=2, marker="o",
                            markersize=4, solid_capstyle="round")
                ax.axhline(thresholds[key], color=colors[key], linewidth=1.2,
                           linestyle=(0, (4, 3)))
            if different:
                ax.axhline(np.median(different), color=c["muted"], linewidth=1,
                           linestyle=(0, (1, 2)))
            ax.set_xticks(xs, [_label(st) for st, _ in steps])
            ax.set_xlim(-0.5, len(steps) - 0.5)
            ax.set_ylim(*limits)
            ax.set_title(name, color=c["ink"], fontsize=10)
            ax.set_xlabel(xlabel, color=c["muted"], fontsize=9)
            style_axes(ax, c)
        axes[0].set_ylabel(label, color=c["muted"], fontsize=9)
        figure.tight_layout(rect=(0, 0, 1, 0.86))
        handles = [plt.Line2D([], [], color=colors[k], linewidth=2, marker="o", markersize=4)
                   for k, _ in available]
        names = [d["label"] for _, d in available]
        handles += [plt.Line2D([], [], color=c["ink"], linewidth=1.2, linestyle=(0, (4, 3))),
                    plt.Line2D([], [], color=c["muted"], linewidth=1, linestyle=(0, (1, 2)))]
        names += [f"threshold accepting {RECALL:.0%} of copies, in the corpus's color",
                  "median for different images"]
        figure.legend(handles, names, loc="upper center", ncol=2, frameon=False,
                      labelcolor=c["ink"], fontsize=9, bbox_to_anchor=(0.5, 1.02))
        note = ("Line or dot: median over the corpus; band: the middle half of its images. "
                f"{'Below' if lower else 'Above'} its corpus's dashed line, an edited image "
                "would be taken for a copy.")
        if len(available) < len(datasets):
            note += " The photographs were not available to this build."
        figure.text(0.5, -0.02, note, ha="center", color=c["muted"], fontsize=9)
        return figure

    save(fig, os.path.join(out_dir, algo), "edits")


def edits_table(datasets, algo, mod, out_dir):
    """The numbers behind edits_figure(): per corpus, the median with its quartiles, and
    the share of edited images the copies' threshold accepts."""
    label, lower, convert, _, fmt = metric(mod)
    present = [(k, d) for k, d in datasets if d["n"]]
    short = {"photos": "Photographs", "synthetic": "Synthetic"}
    head = "| Edit | " + " | ".join(
        f"{short[k]}: median (25th–75th) | accepted as a copy" for k, _ in present) + " |"
    lines = [f"Median {label}, and the share of edited images within the threshold that "
             f"accepts {RECALL:.0%} of the copies; "
             + "; ".join(f"{short[k].lower()}: {d['label']}" for k, d in present) + ".", "",
             head, "|---|" + "---|---|" * len(present)]
    thresholds = {k: separability(*copies_and_different(d, algo, convert), lower)[1]
                  for k, d in present}
    for name, _, steps in content_edits():
        for i, (strength, _) in enumerate(steps):
            cells = []
            for key, data in present:
                values = [convert(x) for x in data["edits"][algo][name][i][1] if x is not None]
                q = np.percentile(values, [25, 50, 75])
                t = thresholds[key]
                accepted = np.mean([(v <= t) if lower else (v >= t) for v in values])
                cells += [f"{fmt(q[1])} ({fmt(q[0])}–{fmt(q[2])})", f"{accepted:.0%}"]
            lines.append(f"| {_edit(name, strength)} | " + " | ".join(cells) + " |")
    if not present:
        lines = [f"{LABEL_MISSING}."]
    write_text(os.path.join(out_dir, algo), "edits-table.md", "\n".join(lines))


def edits_examples(image, out_dir):
    """What the edits that change the picture do, on the example photograph: the
    strongest of each graded edit and every orientation."""
    base = Image.open(image).convert("RGB")
    shown = []
    for name, _, steps in content_edits():
        picks = steps if any(isinstance(s, str) for s, _ in steps) else steps[1:2] + steps[-1:]
        for strength, op in picks:
            shown.append((_edit(name, strength), op(base)[0]))

    def fig(c):
        figure, axes = plt.subplots(1, len(shown) + 1, figsize=(12, 1.9))
        for ax, (title, im) in zip(axes, [("Original", base)] + shown):
            ax.imshow(np.asarray(im))
            hide_axes(ax)
            # Two short lines: "Patch", "4% of the frame".
            title = title.replace("patch over ", "patch\n").replace("hue turned ", "hue\n")
            ax.set_title(title[0].upper() + title[1:], color=c["ink"], fontsize=8.5)
        return figure

    save(fig, os.path.join(out_dir, "edits"), "examples")


def _mean_bits(values):
    v = [bits_that_differ(x, 64) for x in values if x is not None]
    return float(np.mean(v)) if v else float("nan")


def two_hashes_by_edit(tool, out_dir, name, series, edits, short):
    """Two 64-bit hashes over both corpora, edit by edit, from the cached corpus
    measurement: the mean bits that differ from the original, as a figure of `edits` (one
    panel each, a row per corpus) and a table of every edit. `series` is ((algo, label),
    (algo, label)), drawn in the second accent and the accent; `short` names the corpora
    in the labels. Writes <name>.svg and <name>-table.md."""
    datasets = [(k, d) for k, d in ((k, corpus.measure_corpus(tool, k))
                                    for k in ("photos", "synthetic")) if d["n"]]
    steps = {n: s for n, _, s in transforms()}
    xlabels = {n: x for n, x, _ in transforms()}

    def fig(c):
        lines = [(algo, label, color)
                 for (algo, label), color in zip(series, (c["accent2"], c["accent"]))]
        figure, axes = plt.subplots(len(datasets), len(edits), sharey=True,
                                    figsize=(12, 2.9 * len(datasets) + 0.6), squeeze=False)
        for row, (key, data) in zip(axes, datasets):
            for ax, edit in zip(row, edits):
                xs = list(range(len(steps[edit])))
                for algo, _, color in lines:
                    ys = [_mean_bits(v) for _, v in data["robust"][algo][edit]]
                    ax.plot(xs, ys, color=color, linewidth=2, marker="o", markersize=4)
                ax.set_xticks(xs, [f"{v:g}" for v, _ in steps[edit]], fontsize=8)
                ax.set_title(edit, color=c["ink"], fontsize=10)
                ax.set_xlabel(xlabels[edit], color=c["muted"], fontsize=8.5)
                style_axes(ax, c)
            row[0].set_ylabel(f"{short[key]} ({data['n']})\nmean bits that differ",
                              color=c["muted"], fontsize=8.5)
        figure.tight_layout(rect=(0, 0, 1, 0.93))
        handles = [plt.Line2D([], [], color=col, linewidth=2, marker="o", markersize=4)
                   for _, _, col in lines]
        figure.legend(handles, [n for _, n, _ in lines], loc="upper center", ncol=2,
                      frameon=False, labelcolor=c["ink"], fontsize=9.5,
                      bbox_to_anchor=(0.5, 1.0))
        return figure

    save(fig, out_dir, name)

    head = "| Transform | Strength | " + " | ".join(
        f"{short[k]}, {label}" for k, _ in datasets for _, label in series) + " |"
    lines = [head, "|---|---|" + "---|" * (2 * len(datasets))]
    for edit, _, s in transforms():
        for k, (strength, _) in enumerate(s):
            cells = [f"{_mean_bits(d['robust'][a][edit][k][1]):.1f}"
                     for _, d in datasets for a, _ in series]
            lines.append(f"| {edit} | {strength:g} | " + " | ".join(cells) + " |")
    write_text(out_dir, f"{name}-table.md", "\n".join(lines))


def two_hashes_separability(tool, out_dir, name, series, short):
    """Two 64-bit hashes separating copies from different images over both corpora, from
    the cached corpus measurement: d′, the threshold that accepts 95 % of the copies and
    the different pairs it lets through. Writes <name>.md."""
    lines = ["| Corpus | Hash | d′ | Threshold | Different pairs within it |",
             "|---|---|---|---|---|"]
    for key in ("photos", "synthetic"):
        data = corpus.measure_corpus(tool, key)
        if not data["n"]:
            lines.append(f"| {data['label']}: not available to this build | | | | |")
            continue
        for algo, label in series:
            c, d = copies_and_different(data, algo, lambda v: round(bits_that_differ(v, 64)))
            dprime, t, fmr = separability(c, d, True)
            lines.append(f"| {short[key]} | {label} | {dprime:.2f} | {t:g} of 64 | "
                         f"{share(fmr, len(d))} |")
    write_text(out_dir, f"{name}.md", "\n".join(lines))
