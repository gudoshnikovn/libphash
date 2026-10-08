"""The figures and tables of docs/theory/comparing.md: how a threshold on each algorithm's
own comparison trades the copies it accepts against the different images it lets
through, on both corpora. Everything here is drawn from the corpus measurements every
algorithm page shares (measure_corpus(), cached), with the copies and the threshold of
measure/separability.py; this page measures nothing of its own.

Unlike an algorithm page, this one has no hash of its own: render.py calls figures() and
nothing else.
"""
import os

import matplotlib.pyplot as plt
import numpy as np

from draw.corpus_charts import LABEL_MISSING, refused_note, share
from draw.markdown import write_text
from draw.style import save, style_axes
from measure import corpus
from measure.metric import metric
from measure.separability import (COPY_STRENGTHS, RECALL, copies_and_different,
                                  separability)
from pages import TITLES, PAGES, radial

NAME = "comparing"
SHORT = {"photos": "Photographs", "synthetic": "Synthetic images"}
# The shares of copies the table under the chart gives a threshold for.
RECALLS = (0.90, RECALL, 0.99)
# The edits by a short name, for a table's header.
EDIT_SHORT = {"JPEG quality": "JPEG", "Gaussian blur": "Blur"}
# Where a share of 0 different pairs is drawn on the logarithmic axis.
NONE_ACCEPTED = 1e-5


# --8<-- [start:curve]
def curve(copies, different, lower_is_closer):
    """Every threshold the values allow, as (share of copies accepted, share of different
    pairs accepted): a pair is accepted when its value is at the threshold or closer. The
    first point is a threshold stricter than any value, which accepts nothing."""
    c, d = np.asarray(copies, float), np.asarray(different, float)
    sign = 1.0 if lower_is_closer else -1.0
    ts = np.unique(sign * np.concatenate([c, d]))
    accepted_c = np.searchsorted(np.sort(sign * c), ts, side="right") / len(c)
    accepted_d = np.searchsorted(np.sort(sign * d), ts, side="right") / len(d)
    return np.r_[0.0, accepted_c], np.r_[0.0, accepted_d]
# --8<-- [end:curve]


def _values(data, mod):
    """One algorithm's copies and different pairs as its page charts them."""
    _, lower, convert, _, fmt = metric(mod)
    copies, different = copies_and_different(data, mod.ALGO, convert)
    return copies, different, lower, fmt


def _threshold(mod, t, similarity=True):
    """A threshold as a reader applies it: which side is accepted, and for a bit hash
    the similarity it is."""
    _, lower, _, _, fmt = metric(mod)
    if mod.BITS and not similarity:
        return f"≤ {fmt(t)} bits"
    if mod.BITS:
        return f"≤ {fmt(t)} of {mod.BITS} bits (similarity ≥ {1 - t / mod.BITS:.3f})"
    return f"{'≤' if lower else '≥'} {fmt(t)}"


def curves_figure(datasets, out):
    """Small multiples, one panel per algorithm: the share of copies a threshold accepts
    against the share of different pairs it lets through, per corpus, with the threshold
    that accepts RECALL of the copies marked."""
    present = [(k, d) for k, d in datasets if d["n"]]

    def fig(c):
        colors = {"photos": c["accent"], "synthetic": c["accent2"]}
        figure, axes = plt.subplots(3, 3, figsize=(10, 9.2), sharex=True, sharey=True)
        for ax, mod in zip(axes.ravel(), PAGES):
            style_axes(ax, c)
            ax.set_xscale("log")
            ax.set_xlim(NONE_ACCEPTED / 1.6, 1.3)
            ax.set_ylim(0.5, 1.005)
            ax.axhline(RECALL, color=c["muted"], linewidth=0.8, linestyle=(0, (4, 3)))
            for key, data in present:
                copies, different, lower, _ = _values(data, mod)
                acc_c, acc_d = curve(copies, different, lower)
                ax.plot(np.maximum(acc_d, NONE_ACCEPTED), acc_c, color=colors[key],
                        linewidth=1.5, drawstyle="steps-post")
                _, t, fmr = separability(copies, different, lower)
                ax.plot(max(fmr, NONE_ACCEPTED), np.mean(
                    np.asarray(copies) <= t if lower else np.asarray(copies) >= t),
                    "o", color=colors[key], markersize=5, markeredgecolor=c["surface"],
                    markeredgewidth=1)
            ax.set_title(TITLES[mod.ALGO], color=c["ink"], fontsize=10)
        for ax in axes[-1]:
            ax.set_xticks([1e-5, 1e-4, 1e-3, 1e-2, 1e-1, 1])
            ax.set_xticklabels(["0", "0.01 %", "0.1 %", "1 %", "10 %", "100 %"])
            ax.set_xlabel("different pairs accepted", color=c["muted"], fontsize=9)
            ax.xaxis.set_minor_locator(plt.NullLocator())
        for ax in axes[:, 0]:
            ax.set_yticks([0.5, 0.6, 0.7, 0.8, 0.9, 0.95, 1.0])
            ax.set_yticklabels(["50 %", "60 %", "70 %", "80 %", "90 %", "", "100 %"])
            ax.set_ylabel("copies accepted", color=c["muted"], fontsize=9)
        handles = [plt.Line2D([], [], color=colors[k], linewidth=1.5) for k, _ in present]
        handles.append(plt.Line2D([], [], color=c["ink"], marker="o", linestyle="none",
                                  markersize=5))
        labels = [d["label"] for _, d in present]
        labels.append(f"the threshold that accepts {RECALL:.0%} of the copies (the dashed line)")
        if len(present) < len(datasets):
            handles.insert(0, plt.Line2D([], [], linestyle="none"))
            labels.insert(0, LABEL_MISSING)
        figure.legend(handles, labels, loc="lower center", ncol=1, frameon=False,
                      fontsize=9, labelcolor=c["ink"], bbox_to_anchor=(0.5, -0.01))
        figure.tight_layout(rect=(0, 0.08, 1, 1))
        return figure

    save(fig, os.path.join(out, NAME), "curves")


def curves_table(datasets, out):
    """The numbers behind the curves: for each algorithm and corpus, the threshold that
    accepts each share of RECALLS of the copies, and the different pairs it lets through."""
    lines = ["| Algorithm | Corpus | " + " | ".join(
                 f"{r:.0%} of copies" for r in RECALLS) + " |",
             "|---|---|" + "---|" * len(RECALLS)]
    for mod in PAGES:
        for key, data in datasets:
            if not data["n"]:
                lines.append(f"| {TITLES[mod.ALGO]} | {LABEL_MISSING} |"
                             + " — |" * len(RECALLS))
                continue
            copies, different, lower, _ = _values(data, mod)
            cells = []
            for r in RECALLS:
                _, t, fmr = separability(copies, different, lower, r)
                cells.append(f"{_threshold(mod, t, False)}: {share(fmr, len(different))}")
            lines.append(f"| {TITLES[mod.ALGO]} | {SHORT[key]} | " + " | ".join(cells) + " |")
    lines += ["", "Each cell is the threshold that accepts that share of the copies, and the "
              "share of different pairs it lets through as well. " + "; ".join(
                  f"{d['label']}: {len(copies_and_different(d, 'ahash', float)[0]):,} copies "
                  f"and {len(d['different']['ahash']):,} different pairs"
                  for _, d in datasets if d["n"]) + "."]
    for mod in PAGES:
        note = refused_note(datasets, mod.ALGO, mod)
        if note:
            lines += ["", f"For {TITLES[mod.ALGO]}: {note[0].lower()}{note[1:]}"]
    write_text(os.path.join(out, NAME), "curves-table.md", "\n".join(lines))


def thresholds_table(datasets, out):
    """Per algorithm, the threshold that accepts RECALL of the copies on each corpus."""
    present = [(k, d) for k, d in datasets]
    lines = ["| Algorithm | " + " | ".join(
                 f"{SHORT[k]}: threshold | different pairs within" for k, _ in present) + " |",
             "|---|" + "---|---|" * len(present)]
    for mod in PAGES:
        cells = []
        for key, data in present:
            if not data["n"]:
                cells.append(f"{LABEL_MISSING} | —")
                continue
            copies, different, lower, _ = _values(data, mod)
            _, t, fmr = separability(copies, different, lower)
            cells.append(f"{_threshold(mod, t)} | {share(fmr, len(different))}")
        lines.append(f"| {TITLES[mod.ALGO]} | " + " | ".join(cells) + " |")
    write_text(os.path.join(out, NAME), "thresholds.md", "\n".join(lines))


def by_edit_table(datasets, out):
    """Per corpus, the share of each kind of copy that the threshold accepting RECALL of
    all copies accepts: which edits the threshold is spent on."""
    names = list(COPY_STRENGTHS)
    blocks = []
    for key, data in datasets:
        if not data["n"]:
            blocks.append(f"{LABEL_MISSING}.")
            continue
        lines = [f"**{data['label']}**", "",
                 "| Algorithm | " + " | ".join(EDIT_SHORT.get(n, n) for n in names) + " |",
                 "|---|" + "---|" * len(names)]
        for mod in PAGES:
            copies, different, lower, fmt = _values(data, mod)
            _, t, _ = separability(copies, different, lower)
            convert = metric(mod)[2]
            cells = []
            for name in names:
                for strength, values in data["robust"][mod.ALGO][name]:
                    if strength == COPY_STRENGTHS[name]:
                        v = np.array([convert(x) for x in values if x is not None])
                        cells.append(f"{np.mean(v <= t if lower else v >= t):.0%}")
            lines.append(f"| {TITLES[mod.ALGO]} | " + " | ".join(cells) + " |")
        blocks.append("\n".join(lines))
    write_text(os.path.join(out, NAME), "by-edit.md", "\n\n".join(blocks))


def radial_reference(datasets, out):
    """What Radial's reference threshold accepts on each corpus, beside the threshold
    that accepts RECALL of the copies."""
    value, name = radial.REFERENCE
    lines = [f"| Corpus | Copies at `{name}` ({value:g}) or above | Different pairs at "
             f"{value:g} or above | Threshold for {RECALL:.0%} of copies |",
             "|---|---|---|---|"]
    for key, data in datasets:
        if not data["n"]:
            lines.append(f"| {LABEL_MISSING} | — | — | — |")
            continue
        copies, different, lower, fmt = _values(data, radial)
        c, d = np.asarray(copies), np.asarray(different)
        _, t, _ = separability(copies, different, lower)
        lines.append(f"| {data['label']} | {np.mean(c >= value):.1%} | "
                     f"{share(np.mean(d >= value), len(d))} | {fmt(t)} |")
    write_text(os.path.join(out, NAME), "radial-reference.md", "\n".join(lines))


def figures(tool, image, out, timing):
    datasets = [(c, corpus.measure_corpus(tool, c)) for c in corpus.CORPORA]
    curves_figure(datasets, out)
    curves_table(datasets, out)
    thresholds_table(datasets, out)
    by_edit_table(datasets, out)
    radial_reference(datasets, out)
