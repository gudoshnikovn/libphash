"""The figures of docs/theory/bmh.md, from `site_stages bmh` and `site_stages bmh-variants`."""
import os
import tempfile

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

from draw.corpus_charts import share
from draw.markdown import write_text
from draw.style import (draw_bits, gray_panel, hide_axes, image_panel, save, stage_strip,
                        style_axes, value_cells)
from measure import corpus
from measure.corpus import SHORT
from measure.digests import digest_bits
from measure.metric import bits_that_differ
from measure.separability import copies_and_different, separability
from measure.tool import run_stages

ALGO = "bmh"
BITS = 256  # the robustness chart's scale: bits of the digest at the default block size

def _bits(digest_hex, size):
    """The digest's bits in block order: bit i is bit i % 8 of byte i / 8 (LSB first)."""
    return digest_bits(digest_hex, size * size, lsb_first=True)


def _cells(ax, grid, bits, c, fontsize, changed=None):
    """A grid of block means: each value in its cell, the cell in the accent where its bit
    is set; cells listed in `changed` outlined in the second accent."""
    value_cells(ax, grid.ravel(), bits, c, grid.shape[0], fontsize=fontsize, changed=changed,
                changed_width=1.6)


def figures(tool, image, out_dir):
    st, px = run_stages(tool, ALGO, image, ("original.ppm", "gray.pgm"))
    original, gray = px["original.ppm"], px["gray.pgm"]
    sizes = {s["size"]: s for s in st["sizes"]}
    d = sizes[st["default"]]
    n = d["size"]
    grid = np.array(d["grid"]).reshape(n, n)
    bits = _bits(d["digest"], n)
    out = os.path.join(out_dir, ALGO)

    def pipeline(c):
        fig = stage_strip(c, (10.4, 3.1), [
            ("Decoded image", image_panel(original)),
            ("Grayscale", gray_panel(gray)),
            (f"Block means, {n}×{n}", gray_panel(grid, grid=True)),
            (f"{n * n} bits: block ≥ median",
             lambda ax: draw_bits(ax, bits.reshape(n, n), c, numbers=False))])
        half = len(d["digest"]) // 2
        pad = chr(0xA0) * len("digest = ")  # a no-break space survives in the SVG
        fig.text(0.5, -0.02, f"digest = {d['digest'][:half]}\n{pad}{d['digest'][half:]}",
                 ha="center", multialignment="left", color=c["ink"], family="monospace",
                 fontsize=10)
        return fig

    def median_grid(c):
        fig, ax = plt.subplots(figsize=(7.2, 7.4))
        _cells(ax, grid, bits, c, fontsize=6.2)
        ax.set_title(f"median = {d['median']}  ·  blue: at or above it, bit set "
                     f"({int(bits.sum())} of {n * n})", color=c["ink"], fontsize=10)
        return fig

    def bit_order(c):
        fig, ax = plt.subplots(figsize=(7.2, 7.4))
        for k in range(n * n):
            i, j = divmod(k, n)
            byte = k // 8
            ax.add_patch(plt.Rectangle((j - 0.46, i - 0.46), 0.92, 0.92, linewidth=0,
                                       color=c["off"] if byte % 2 == 0 else c["grid"]))
            ax.text(j, i, str(k), ha="center", va="center", fontsize=5.6, color=c["ink"])
        ax.set_xlim(-0.5, n - 0.5)
        ax.set_ylim(n - 0.5, -0.5)
        ax.set_aspect("equal")
        hide_axes(ax)
        ax.set_title("bit number of each block; bit k is bit k mod 8 of byte k / 8,\n"
                     "the shading alternates by byte", color=c["ink"], fontsize=10)
        return fig

    def size_fig(c):
        shown = sorted(sizes)
        fig, axes = plt.subplots(2, len(shown), figsize=(2.75 * len(shown), 5.9))
        for col, s in enumerate(shown):
            g = np.array(sizes[s]["grid"]).reshape(s, s)
            b = _bits(sizes[s]["digest"], s)
            axes[0, col].imshow(g, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
            hide_axes(axes[0, col])
            axes[0, col].set_title(f"block_size {s}", color=c["ink"], fontsize=10,
                                   fontweight="bold" if s == n else "normal")
            draw_bits(axes[1, col], b.reshape(s, s), c, numbers=False)
            axes[1, col].set_title(f"{s * s} bits, {s * s // 8 or 1} "
                                   f"byte{'s' if s * s > 8 else ''}", color=c["muted"],
                                   fontsize=9)
        return fig

    save(pipeline, out, "pipeline")
    save(median_grid, out, "grid")
    save(bit_order, out, "bit-order")
    save(size_fig, out, "sizes")
    _write_load_grayscale(st, out)

    measured = corpus.measure_settings(tool, "bmh-variants")
    _threshold_figure(tool, measured, out)
    _write_threshold_table(measured, out)
    _sizes_figure_and_table(tool, measured, out)


def _write_load_grayscale(st, out):
    d = next(s for s in st["sizes"] if s["size"] == st["default"])
    k = int((_bits(d["digest"], d["size"]) != _bits(st["digest_load_grayscale"],
                                                      d["size"])).sum())
    text = ("The example photograph gives the same digest both ways." if k == 0 else
            f"On the example photograph the two digests are {k} of {d['size'] ** 2} bits "
            "apart.")
    write_text(out, "load-grayscale.md", text)


# --8<-- [start:variants]
def _variant_distances(images, variant, size):
    """(copies, different): bits that differ between each original and its copies, and
    between every pair of distinct originals, for one variant ("median_16", …)."""
    b = np.array([[_bits(row[variant], size) for row in img] for img in images], np.uint8)
    copies = (b[:, 1:] != b[:, :1]).sum(axis=2).ravel()
    orig = b[:, 0].astype(np.int32)
    ones = orig.sum(axis=1)
    # Hamming distance of every pair i < j, from dot products of 0/1 vectors.
    same_ones = orig @ orig.T
    dist = ones[:, None] + ones[None, :] - 2 * same_ones
    iu = np.triu_indices(len(images), 1)
    return copies, dist[iu]
# --8<-- [end:variants]


def _sizes(measured):
    first = next((d["images"][0][0] for d in measured.values() if d["images"]), {})
    return sorted(int(k.split("_")[1]) for k in first if k.startswith("median_"))


def _rows(tool, measured):
    """Per corpus: [(what, bits, d′, threshold, accepted share, number of pairs)] for each
    block size at the median, the default size at the mean, and aHash for reference."""
    out = {}
    for key, data in measured.items():
        if not data["n"]:
            out[key] = []
            continue
        rows = []
        for s in _sizes(measured):
            c, d = _variant_distances(data["images"], f"median_{s}", s)
            rows.append((f"block_size {s}", s * s, *separability(c, d, True), len(d)))
        s = 16
        c, d = _variant_distances(data["images"], f"mean_{s}", s)
        rows.append((f"block_size {s}, mean threshold", s * s, *separability(c, d, True),
                     len(d)))
        # The library's default, measured here, must be what the corpus pages show.
        cached = corpus.measure_corpus(tool, key)
        cc, cd = copies_and_different(cached, ALGO, lambda v: round(bits_that_differ(v, BITS)))
        mine = next(r for r in rows if r[0] == "block_size 16")
        theirs = separability(cc, cd, True)
        if abs(mine[2] - theirs[0]) > 1e-9:
            raise SystemExit(f"render: bmh: block_size 16 over the {key} corpus gives d′ "
                             f"{mine[2]:.6f} here and {theirs[0]:.6f} in the corpus charts")
        ac, ad = copies_and_different(cached, "ahash", lambda v: bits_that_differ(v, 64))
        rows.append(("aHash", 64, *separability(ac, ad, True), len(ad)))
        out[key] = rows
    return out


def _sizes_figure_and_table(tool, measured, out):
    rows = _rows(tool, measured)
    present = [(k, measured[k]) for k in ("photos", "synthetic") if rows[k]]
    sizes = _sizes(measured)

    def fig(c):
        colors = {"photos": c["accent"], "synthetic": c["accent2"]}
        f, axes = plt.subplots(1, 2, figsize=(10, 3.9))
        for key, data in present:
            by = {r[0]: r for r in rows[key]}
            med = [by[f"block_size {s}"] for s in sizes]
            for ax, col, scale in ((axes[0], 2, 1), (axes[1], 4, 100)):
                ys = [r[col] * scale for r in med]
                ax.plot(sizes, ys, color=colors[key], linewidth=2, marker="o", markersize=4.5,
                        label=data["label"])
                ax.axhline(by["aHash"][col] * scale, color=colors[key], linewidth=1,
                           linestyle=(0, (4, 3)))
        for ax, ylabel in ((axes[0], "d′ (higher separates better)"),
                           (axes[1], "% of different pairs within the threshold")):
            ax.set_xscale("log", base=2)
            ax.set_xticks(sizes, [str(s) for s in sizes])
            ax.minorticks_off()
            ax.set_xlabel("block_size", color=c["muted"], fontsize=9)
            ax.set_ylabel(ylabel, color=c["muted"], fontsize=9)
            style_axes(ax, c)
        axes[1].set_yscale("symlog", linthresh=0.1)
        ticks = [0, 0.1, 0.3, 1, 3, 10, 30]
        top = axes[1].get_ylim()[1]
        axes[1].set_yticks([t for t in ticks if t <= top * 1.1],
                           [f"{t:g}" for t in ticks if t <= top * 1.1])
        axes[1].set_ylim(0, None)
        f.tight_layout(rect=(0, 0.06, 1, 0.9))
        handles = [plt.Line2D([], [], color=colors[k], linewidth=2, marker="o", markersize=4)
                   for k, _ in present]
        f.legend(handles, [d["label"] for _, d in present], loc="upper center",
                 ncol=len(present), frameon=False, labelcolor=c["ink"], fontsize=9.5,
                 bbox_to_anchor=(0.5, 1.0))
        f.text(0.5, 0.0, "Line: BMH at each block size. Dashed: aHash, 64 bits, in the "
               "corpus's color. The threshold accepts 95 % of the copies.", ha="center",
               color=c["muted"], fontsize=9)
        return f

    if present:
        save(fig, out, "sizes-corpus")
    lines = ["| Corpus | Hash | Bits | d′ | Threshold | Different pairs within it |",
             "|---|---|---|---|---|---|"]
    for key in ("photos", "synthetic"):
        if not rows[key]:
            lines.append(f"| {measured[key]['label']}: not available to this build "
                         "| | | | | |")
            continue
        for what, nbits, dprime, t, fmr, npairs in rows[key]:
            lines.append(f"| {SHORT[key]} | {what} | {nbits} | {dprime:.2f} | "
                         f"{t:g} of {nbits} ({t / nbits:.0%}) | {share(fmr, npairs)} |")
    write_text(out, "sizes-corpus-table.md", "\n".join(lines))


def _balance(images, variant, size):
    """The number of bits set in each original's digest."""
    return np.array([_bits(img[0][variant], size).sum() for img in images])


def _write_threshold_table(measured, out):
    """Median against mean at the default size, over both corpora: how often the two
    digests agree, and how many bits each sets."""
    s = 16
    lines = ["| Images | Same digest | Bits apart: median, most | Bits set, median threshold "
             "| Bits set, mean threshold |", "|---|---|---|---|---|"]
    for key in ("photos", "synthetic"):
        data = measured[key]
        if not data["n"]:
            lines.append(f"| {data['label']}: not available to this build | | | | |")
            continue
        apart = np.array([int((_bits(img[0][f"median_{s}"], s) !=
                               _bits(img[0][f"mean_{s}"], s)).sum())
                          for img in data["images"]])

        def spread(v):
            lo, hi = np.percentile(v, [10, 90])
            return f"{v.min()}–{v.max()}; middle 80 %: {lo:.0f}–{hi:.0f}"

        lines.append(f"| {data['label']} | {int((apart == 0).sum())} | "
                     f"{np.median(apart):g}, {apart.max()} | "
                     f"{spread(_balance(data['images'], f'median_{s}', s))} | "
                     f"{spread(_balance(data['images'], f'mean_{s}', s))} |")
    write_text(out, "thresholds.md", "\n".join(lines))


def _threshold_figure(tool, measured, out):
    """The photograph (or, without photographs, the synthetic image) whose mean threshold
    sets the fewest or most bits: its block means, the two thresholds on their
    distribution, and both digests with the blocks where they differ outlined."""
    key = "photos" if measured["photos"]["n"] else "synthetic"
    images = measured[key]["images"]
    s = 16
    ones = _balance(images, f"mean_{s}", s)
    pick = int(np.argmax(np.abs(ones - s * s / 2)))
    with tempfile.TemporaryDirectory() as tmp:
        path = corpus.images(tool, key)[pick]
        ppm = os.path.join(tmp, "image.ppm")
        Image.open(path).convert("RGB").save(ppm)
        st, px = run_stages(tool, ALGO, ppm, ("original.ppm",))
    d = next(x for x in st["sizes"] if x["size"] == s)
    grid = np.array(d["grid"]).reshape(s, s)
    med, mean = _bits(d["digest"], s), _bits(d["digest_mean"], s)
    changed = med != mean
    what = (f"Photograph {pick + 1} of the corpus" if key == "photos"
            else f"Synthetic image {pick}")

    def fig(c):
        f = plt.figure(figsize=(12, 7.4))
        ax_im = f.add_axes([0.0, 0.53, 0.3, 0.4])
        ax_im.imshow(px["original.ppm"])
        hide_axes(ax_im)
        ax_im.set_title(what, color=c["ink"], fontsize=10)
        ax_h = f.add_axes([0.38, 0.6, 0.6, 0.32])
        ax_h.hist(grid.ravel(), bins=np.arange(0, 257, 4), color=c["muted"], alpha=0.5,
                  linewidth=0)
        for v, name, color, dy in ((d["median"], "median", c["accent"], 0.95),
                                   (d["mean"], "mean", c["accent2"], 0.78)):
            ax_h.axvline(v, color=color, linewidth=1.6, linestyle=(0, (4, 3)))
            ax_h.annotate(f"{name} {v:.1f}" if name == "mean" else f"{name} {v}",
                          (v, dy), xycoords=("data", "axes fraction"), xytext=(5, 0),
                          textcoords="offset points", va="top", color=c["ink"], fontsize=9,
                          bbox={"facecolor": c["surface"], "edgecolor": "none", "pad": 1})
        ax_h.set_xlim(0, 255)
        ax_h.set_xlabel(f"block mean, the {s * s} blocks", color=c["muted"], fontsize=9)
        ax_h.set_ylabel("blocks", color=c["muted"], fontsize=9)
        style_axes(ax_h, c)
        for x, bits, title in ((0.08, med, "threshold at the median"),
                               (0.55, mean, "threshold at the mean")):
            ax = f.add_axes([x, 0.0, 0.38, 0.45])
            _cells(ax, grid, bits, c, fontsize=0, changed=changed)
            ax.set_title(f"{title}: {int(bits.sum())} of {s * s} bits set", color=c["ink"],
                         fontsize=10)
        f.text(0.5, -0.04, f"orange: the {int(changed.sum())} blocks whose bit the two "
               "thresholds set differently", ha="center", color=c["accent2"], fontsize=10)
        return f

    save(fig, out, "median-mean")
