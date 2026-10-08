"""The figures and tables of docs/theory/choosing.md: the nine algorithms side by side,
by what each edit does to them, how well they separate copies from different images,
what they cost, and how long their digests are.

Like the page on comparing, this one measures nothing of its own beyond the size of each
digest: the edits and the pairs are the corpus measurements every algorithm page shares
(measure_corpus(), cached), and the times are those of measure/timing.py.
"""
import os

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LinearSegmentedColormap

from draw.corpus_charts import LABEL_MISSING, _edit, share
from draw.markdown import write_text
from draw.style import THEMES, save, style_axes
from draw.timing_tables import megapixels, ms
from measure import corpus
from measure.metric import metric
from measure.separability import COPY_STRENGTHS, copies_and_different, separability
from measure.tool import run_lines
from pages import PAGES, TITLES

NAME = "choosing"
# The edits by a short name, for a column of the matrix.
EDIT_SHORT = {"JPEG quality": "JPEG 50", "Downscale": "Downscale ½", "Rotation": "Rotation 2°",
              "Brightness": "Brightness ×1.15", "Contrast": "Contrast ×1.15",
              "Gamma": "Gamma 1.2", "Gaussian blur": "Blur σ 1", "Noise": "Noise σ 5",
              "Crop": "Crop 5 %"}
# The sequential ramp of the matrix, one hue from near the surface to full strength: the
# blue of the site's accent (slot 1), stepped toward each theme's surface for 0.
RAMPS = {"light": ["#f4f8fe", "#cde2fb", "#6da7ec", "#256abf", "#0d366b"],
         "dark": ["#121a26", "#184f95", "#2a78d6", "#6da7ec", "#cde2fb"]}
# When to take each algorithm, as the summary table says it; every clause rests on a
# measurement of this page or of the algorithm's own.
WHEN = {
    "ahash": "The cheapest; finds copies well, but pHash finds them better at the same cost.",
    "dhash": "Costs more than aHash on a large image and separates better on photographs, "
             "worse on the synthetic images.",
    "phash": "Copies of photographs: the widest gap between copies and different images, "
             "at aHash's cost; among the narrowest on the synthetic images.",
    "whash": "aHash's 8×8 grid with a median threshold, at the same cost; little reason "
             "to prefer it over aHash.",
    "mhash": "Fine detail in 576 bits; a 2° rotation or a 5 % crop takes it most of the way "
             "to an unrelated image.",
    "bmh": "A 16×16 grid in 256 bits, at aHash's cost; the best separation on the synthetic "
           "images.",
    "radial": "Copies that are cropped or turned by half a turn; the most different pairs "
              "within its threshold on the synthetic images.",
    "color_hash": "When a change of color must count; alongside a layout hash, since it "
                  "ignores layout.",
    "color_moments": "Moves more with tone than with hue; the weakest separation of the "
                     "nine.",
}


def _cmap(theme):
    return LinearSegmentedColormap.from_list(f"seq-{theme}", RAMPS[theme])


# --8<-- [start:moved]
def moved(values, different, lower_is_closer):
    """How far an edit moves a hash toward an unrelated image: the median over the corpus
    of each copy's distance from identical, in units of the median distance between two
    different images. 0 is the original's own hash, 1 is as far as a typical pair of
    different images. Identical is 0 for a distance and 1 for a similarity (Radial's peak
    correlation, ColorHash's intersection)."""
    identical = 0.0 if lower_is_closer else 1.0
    typical = np.median(different) - identical
    return float(np.median((np.asarray(values, float) - identical) / typical))
# --8<-- [end:moved]


def matrix(data, group):
    """(columns, rows): the value of moved() for every algorithm and edit of `group`,
    "copies" (one moderate strength of each of the nine edits a copy goes through) or
    "edits" (every strength of the edits that change the picture)."""
    columns, rows = [], []
    for mod in PAGES:
        _, lower, convert, _, _ = metric(mod)
        different = [convert(v) for v in data["different"][mod.ALGO] if v is not None]
        row, cols = [], []
        source = data["robust"] if group == "copies" else data["edits"]
        for name, points in source[mod.ALGO].items():
            for strength, values in points:
                if group == "copies" and strength != COPY_STRENGTHS[name]:
                    continue
                v = [convert(x) for x in values if x is not None]
                row.append(moved(v, different, lower))
                cols.append(EDIT_SHORT[name] if group == "copies" else _edit(name, strength))
        rows.append(row)
        columns = cols
    return columns, np.array(rows)


def matrix_figure(datasets, group, out):
    for key, data in _photos_first(datasets):
        if not data["n"]:
            continue
        columns, values = matrix(data, group)

        def fig(c, theme, columns=columns, values=values, data=data):
            cmap = _cmap(theme)
            figure, ax = plt.subplots(figsize=(0.62 * len(columns) + 1.3, 4.3))
            ax.imshow(np.clip(values, 0, 1), cmap=cmap, vmin=0, vmax=1, aspect="auto")
            for i in range(values.shape[0]):
                for j in range(values.shape[1]):
                    rgba = cmap(float(np.clip(values[i, j], 0.0, 1.0)))
                    lum = 0.2126 * rgba[0] + 0.7152 * rgba[1] + 0.0722 * rgba[2]
                    color = "#0b0c0f" if lum > 0.5 else "#ffffff"
                    ax.text(j, i, f"{values[i, j]:.2f}".replace("-0.00", "0.00"),
                            ha="center", va="center", fontsize=8, color=color)
            ax.set_xticks(range(len(columns)))
            ax.set_xticklabels(columns, rotation=40, ha="right", rotation_mode="anchor",
                               color=c["ink"], fontsize=9)
            ax.set_yticks(range(len(PAGES)))
            ax.set_yticklabels([TITLES[m.ALGO] for m in PAGES], color=c["ink"], fontsize=9)
            ax.tick_params(length=0)
            for s in ax.spines.values():
                s.set_visible(False)
            ax.set_xticks(np.arange(-0.5, len(columns)), minor=True)
            ax.set_yticks(np.arange(-0.5, len(PAGES)), minor=True)
            ax.grid(which="minor", color=c["surface"], linewidth=2)
            ax.tick_params(which="minor", length=0)
            ax.set_title(f"{data['label'][0].upper()}{data['label'][1:]}", color=c["muted"],
                         fontsize=9, loc="left")
            figure.tight_layout()
            return figure

        save_themed(fig, out, f"{group}-{key}")


def save_themed(fig, out, name):
    """save() for a figure that needs its theme's name as well as its colors."""
    theme = {c["surface"]: name for name, c in THEMES.items()}
    save(lambda c: fig(c, theme[c["surface"]]), os.path.join(out, NAME), name)


def matrix_table(datasets, group, out):
    blocks = []
    for key, data in _photos_first(datasets):
        if not data["n"]:
            blocks.append(f"{LABEL_MISSING}.")
            continue
        columns, values = matrix(data, group)
        lines = [f"**{data['label'][0].upper()}{data['label'][1:]}**", "",
                 "| Algorithm | " + " | ".join(columns) + " |",
                 "|---|" + "---|" * len(columns)]
        for mod, row in zip(PAGES, values):
            lines.append(f"| {TITLES[mod.ALGO]} | " + " | ".join(
                f"{v:.2f}".replace("-0.00", "0.00") for v in row) + " |")
        blocks.append("\n".join(lines))
    write_text(os.path.join(out, NAME), f"{group}-table.md", "\n\n".join(blocks))


def _separation(datasets):
    """{corpus: [(d', share of different pairs within the 95 % threshold, n pairs)]} in the
    order of PAGES."""
    result = {}
    for key, data in datasets:
        if not data["n"]:
            continue
        rows = []
        for mod in PAGES:
            _, lower, convert, _, _ = metric(mod)
            copies, different = copies_and_different(data, mod.ALGO, convert)
            dprime, _, fmr = separability(copies, different, lower)
            rows.append((dprime, fmr, len(different)))
        result[key] = rows
    return result


# Where a share of 0 different pairs is drawn on the logarithmic axis.
NONE_ACCEPTED = 1e-5


def separation_figure(datasets, out):
    sep = _separation(datasets)
    # The photographs first, on top of each pair of bars, as in the legend.
    present = [(k, d) for k, d in _photos_first(datasets) if k in sep]

    def fig(c):
        colors = {"photos": c["accent"], "synthetic": c["accent2"]}
        figure, (left, right) = plt.subplots(1, 2, figsize=(10, 4.8), sharey=True)
        y = np.arange(len(PAGES))
        h = 0.38
        for i, (key, data) in enumerate(present):
            off = (i - (len(present) - 1) / 2) * (h + 0.04)
            d = [r[0] for r in sep[key]]
            f = [max(r[1], NONE_ACCEPTED) for r in sep[key]]
            left.barh(y + off, d, height=h, color=colors[key], label=data["label"])
            right.barh(y + off, f, height=h, color=colors[key], left=NONE_ACCEPTED / 1.6)
        for ax in (left, right):
            style_axes(ax, c)
            ax.grid(axis="y", visible=False)
        left.set_yticks(y)
        left.set_yticklabels([TITLES[m.ALGO] for m in PAGES], color=c["ink"], fontsize=9)
        left.invert_yaxis()
        left.set_xlabel("d′ (higher separates better)", color=c["muted"], fontsize=9)
        right.set_xscale("log")
        right.set_xlim(NONE_ACCEPTED / 1.6, 0.5)
        right.set_xticks([1e-5, 1e-4, 1e-3, 1e-2, 1e-1])
        right.set_xticklabels(["0", "0.01 %", "0.1 %", "1 %", "10 %"])
        right.xaxis.set_minor_locator(plt.NullLocator())
        right.set_xlabel("different pairs within the 95 % threshold\n(logarithmic; lower is "
                         "better)", color=c["muted"], fontsize=9)
        handles, labels = left.get_legend_handles_labels()
        if len(present) < len(datasets):
            handles.insert(0, plt.Line2D([], [], linestyle="none"))
            labels.insert(0, LABEL_MISSING)
        figure.legend(handles, labels, loc="lower center", ncol=1, frameon=False,
                      fontsize=9, labelcolor=c["ink"], bbox_to_anchor=(0.5, -0.02))
        figure.tight_layout(rect=(0, 0.1, 1, 1))
        return figure

    save(fig, os.path.join(out, NAME), "separation")


def _times(timing):
    small, large = timing["small"]["cases"], timing["large"]["cases"]
    return ([small[m.ALGO]["min_ms"] for m in PAGES], [large[m.ALGO]["min_ms"] for m in PAGES],
            small["decode"]["min_ms"], large["decode"]["min_ms"])


def cost_figure(timing, out):
    small, large, dec_s, dec_l = _times(timing)
    size = f"{timing['small']['width']}×{timing['small']['height']}"
    mpx = f"{megapixels(timing['large'])} Mpx"

    def fig(c):
        figure, axes = plt.subplots(1, 2, figsize=(10, 4.2), sharey=True)
        y = np.arange(len(PAGES))
        for ax, times, dec, title in ((axes[0], small, dec_s, f"a {size} image"),
                                      (axes[1], large, dec_l, f"a {mpx} image")):
            style_axes(ax, c)
            ax.grid(axis="y", visible=False)
            ax.barh(y, times, height=0.6, color=c["accent"])
            ax.set_xscale("log")
            ax.axvline(dec, color=c["muted"], linewidth=1, linestyle=(0, (4, 3)))
            ax.text(dec, -0.75, " decoding", color=c["muted"], fontsize=8.5, va="bottom",
                    ha="left")
            for yi, t in zip(y, times):
                ax.text(t * 1.08, yi, f"{ms(t)} ms", va="center", color=c["ink"], fontsize=8,
                        bbox={"facecolor": c["surface"], "edgecolor": "none", "pad": 0.5})
            ax.set_xlim(min(times) / 1.5, max(max(times), dec) * 4)
            ax.set_title(title, color=c["muted"], fontsize=9, loc="left", pad=14)
            ax.set_xlabel("milliseconds, logarithmic", color=c["muted"], fontsize=9)
            ax.xaxis.set_major_formatter(plt.FuncFormatter(lambda v, _: f"{v:g}"))
            ax.xaxis.set_minor_locator(plt.NullLocator())
        axes[0].set_yticks(y)
        axes[0].set_yticklabels([TITLES[m.ALGO] for m in PAGES], color=c["ink"], fontsize=9)
        axes[0].invert_yaxis()
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out, NAME), "cost")


def map_figure(datasets, timing, out):
    """d' on the photographs (the synthetic images when there are none) against the time
    on the large image: one labeled point per algorithm."""
    sep = _separation(datasets)
    key = "photos" if "photos" in sep else "synthetic"
    label = dict(datasets)[key]["label"]
    _, large, _, _ = _times(timing)
    mpx = f"{megapixels(timing['large'])} Mpx"
    def fig(c):
        figure, ax = plt.subplots(figsize=(8, 5))
        style_axes(ax, c)
        d = [r[0] for r in sep[key]]
        ax.scatter(large, d, s=46, color=c["accent"], edgecolor=c["surface"], linewidth=1.5,
                   zorder=3)
        ax.set_xscale("log")
        # Points that land on one another (the hashes that share the area grid cost the
        # same) take one label between them.
        groups = []
        for mod, x, yv in zip(PAGES, large, d):
            for g in groups:
                if abs(np.log10(g[1] / x)) < 0.03 and abs(g[2] - yv) < 0.3:
                    g[0].append(TITLES[mod.ALGO])
                    break
            else:
                groups.append([[TITLES[mod.ALGO]], x, yv])
        for names, x, yv in groups:
            ax.annotate(", ".join(names), (x, yv), xytext=(8, 0), textcoords="offset points",
                        va="center", ha="left", color=c["ink"], fontsize=9)
        ax.set_xlim(min(large) / 1.4, max(large) * 2.2)
        ax.set_ylim(0, max(d) * 1.12)
        ax.set_xlabel(f"time on a {mpx} image, after decoding (ms, logarithmic)",
                      color=c["muted"], fontsize=9)
        ax.set_ylabel("d′ (higher separates better)", color=c["muted"], fontsize=9)
        ax.xaxis.set_minor_locator(plt.NullLocator())
        ticks = [t for t in (1, 2, 5, 10, 20, 50, 100) if min(large) / 1.4 <= t <= max(large) * 2.2]
        ax.set_xticks(ticks)
        ax.set_xticklabels([f"{t:g}" for t in ticks])
        ax.set_title(f"d′ on the {label}", color=c["muted"], fontsize=9, loc="left")
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out, NAME), "map")


def _photos_first(datasets):
    """The corpora with the photographs first, as every chart and table here orders them."""
    return sorted(datasets, key=lambda p: p[0] != "photos")


def summary_table(tool, image, datasets, timing, out):
    """One row per algorithm: its digest, d' and the different pairs within the 95 %
    threshold on each corpus, its time on the large image, and when to take it."""
    sizes = {r["algorithm"]: r for r in run_lines(tool, "sizes", image)}
    sep = _separation(datasets)
    _, large, _, _ = _times(timing)
    mpx = f"{megapixels(timing['large'])} Mpx"
    keys = [k for k, _ in _photos_first(datasets)]
    head = ["Algorithm", "Digest", "d′", "Different pairs within", f"Time, {mpx}",
            "When to take it"]
    lines = ["| " + " | ".join(head) + " |", "|---" * len(head) + "|"]
    for i, mod in enumerate(PAGES):
        s = sizes[mod.ALGO]
        digest = f"{8 * s['size']} bits" if s["kind"] == "bits" else f"{s['size']} bytes"
        cells = [TITLES[mod.ALGO], digest,
                 " / ".join(f"{sep[k][i][0]:.2f}" if k in sep else "—" for k in keys),
                 " / ".join(share(sep[k][i][1], sep[k][i][2]) if k in sep else "—"
                            for k in keys),
                 f"{ms(large[i])} ms", WHEN[mod.ALGO]]
        lines.append("| " + " | ".join(cells) + " |")
    lines += ["", "d′ and the different pairs within the threshold that accepts 95 % of the "
              "copies are given as " + " / ".join(corpus.SHORT[k] for k in keys) + "."]
    write_text(os.path.join(out, NAME), "summary.md", "\n".join(lines))


def separation_table(datasets, out):
    sep = _separation(datasets)
    present = _photos_first(datasets)
    lines = ["| Algorithm | " + " | ".join(
                 f"{corpus.SHORT[k].capitalize()}: d′ | different pairs within" for k, _ in present)
             + " |", "|---|" + "---|---|" * len(present)]
    for i, mod in enumerate(PAGES):
        cells = []
        for k, _ in present:
            cells.append(f"{sep[k][i][0]:.2f} | {share(sep[k][i][1], sep[k][i][2])}"
                         if k in sep else f"{LABEL_MISSING} | —")
        lines.append(f"| {TITLES[mod.ALGO]} | " + " | ".join(cells) + " |")
    lines += ["", "; ".join(f"{d['label'][0].upper()}{d['label'][1:]}: "
                            f"{sep[k][0][2]:,} pairs of different images"
                            for k, d in present if k in sep) + "."]
    write_text(os.path.join(out, NAME), "separation-table.md", "\n".join(lines))


def figures(tool, image, out, timing):
    datasets = [(c, corpus.measure_corpus(tool, c)) for c in corpus.CORPORA]
    for group in ("copies", "edits"):
        matrix_figure(datasets, group, out)
        matrix_table(datasets, group, out)
    separation_figure(datasets, out)
    separation_table(datasets, out)
    cost_figure(timing, out)
    map_figure(datasets, timing, out)
    summary_table(tool, image, datasets, timing, out)
