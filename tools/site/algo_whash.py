"""The figures of docs/theory/whash.md, from `site_stages whash` and `site_stages whash-modes`."""
import glob
import json
import os
import subprocess
import tempfile

import matplotlib.pyplot as plt
import numpy as np

import corpus
from common import draw_bits, hide_axes, run_stages, save, style_axes
from measure import bits_that_differ
from transforms import transforms

ALGO = "whash"
BITS = 64  # the robustness chart's scale: bits of the hash
# The Cost row's second pair of times: (timed case, what it is).
COST_VARIANTS = [("whash_full", "in the full mode")]

# The edits the comparison with aHash draws: those where the two differ, and contrast,
# where they do not; every edit is in the table under it.
VS_AHASH = ("Gamma", "Contrast", "Rotation", "Crop", "Noise")
SHORT = {"photos": "photographs", "synthetic": "synthetic"}

# The four bands of one Haar level, in the layout the library leaves them in: the
# horizontal pass puts the sums of pairs in the left half and their differences in the
# right, the vertical pass does the same top and bottom.
BANDS = (("LL: sums both ways", 0, 0), ("differences across", 0, 1),
         ("differences down", 1, 0), ("differences both ways", 1, 1))


def _bits_apart(a, b):
    return bin(int(a, 16) ^ int(b, 16)).count("1")


def _whash_modes(tool, paths):
    if not paths:
        return []
    out = subprocess.run([tool, "whash-modes", *paths], check=True, capture_output=True,
                         text=True).stdout
    return [json.loads(line) for line in out.splitlines()]


def _ties_synthetic(tool):
    """The image of the synthetic corpus whose FAST hash the removal moves the most, with
    its stages: (index, stages, pixels)."""
    best = None
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([tool, "corpus", tmp], check=True)
        for path in sorted(glob.glob(os.path.join(tmp, "*.ppm"))):
            st, px = run_stages(tool, ALGO, path, ("original.ppm",))
            moved = _bits_apart(st["fast"]["hash"], st["fast_removed"]["hash"])
            if best is None or moved > best[0]:
                best = (moved, int(os.path.basename(path)[:2]), st, px["original.ppm"])
    return best[1:]


def _ll_cells(ax, values, bits, c, fmt="{:.3f}", fontsize=7.5, changed=None):
    """The 8×8 LL band: each value in its cell, the cell in the accent where its bit is
    set; cells listed in `changed` outlined in the second accent."""
    for k, v in enumerate(values):
        i, j = divmod(k, 8)
        ax.add_patch(plt.Rectangle((j - 0.46, i - 0.46), 0.92, 0.92, linewidth=0,
                                   color=c["accent"] if bits[k] else c["off"]))
        ax.text(j, i, fmt.format(v), ha="center", va="center", fontsize=fontsize,
                color=c["on_text"] if bits[k] else c["ink"])
        if changed is not None and changed[k]:
            ax.add_patch(plt.Rectangle((j - 0.5, i - 0.5), 1, 1, fill=False, linewidth=2,
                                       edgecolor=c["accent2"]))
    ax.set_xlim(-0.5, 7.5)
    ax.set_ylim(7.5, -0.5)
    ax.set_aspect("equal")
    hide_axes(ax)


def _show_coef(ax, coef, size, levels):
    """A decomposition in Mallat's layout as an image: the LL corner on its own gray
    scale, and the detail bands of each level stretched so that zero is mid-gray, each
    level on its own scale (a coarser level's coefficients are larger by 2 per level)."""
    img = np.zeros_like(coef)
    ll = size >> levels
    corner = coef[:ll, :ll]
    img[:ll, :ll] = (corner - corner.min()) / max(np.ptp(corner), 1e-9)
    s = size
    for _ in range(levels):
        h = s // 2
        level = np.zeros_like(coef, bool)
        level[:s, :s] = True
        level[:h, :h] = False
        scale = np.percentile(np.abs(coef[level]), 99) or 1.0
        img[level] = np.clip(0.5 + 0.5 * coef[level] / scale, 0, 1)
        s = h
    ax.imshow(img, cmap="gray", vmin=0, vmax=1, interpolation="nearest")
    hide_axes(ax)
    return img


def figures(tool, image, out_dir):
    st, px = run_stages(tool, ALGO, image, ("original.ppm", "gray.pgm"))
    original, gray = px["original.ppm"], px["gray.pgm"]
    fast, full = st["fast"], st["full"]
    n = fast["size"]
    grid = np.array(fast["grid"]).reshape(n, n)
    coef = np.array(fast["coef"]).reshape(n, n)
    ll = np.array(fast["ll"])
    bits = np.array(fast["bits"])
    out = os.path.join(out_dir, ALGO)

    def pipeline(c):
        fig, axes = plt.subplots(1, 5, figsize=(12, 2.9))
        titles = ["Decoded image", "Grayscale", f"Reduced to {n}×{n}", "One Haar level",
                  "Bits of the 8×8 LL band"]
        axes[0].imshow(original)
        axes[1].imshow(gray, cmap="gray", vmin=0, vmax=255)
        axes[2].imshow(grid, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
        _show_coef(axes[3], coef, n, 1)
        axes[3].add_patch(plt.Rectangle((-0.5, -0.5), 8, 8, fill=False, linewidth=1.5,
                                        edgecolor=c["accent"]))
        draw_bits(axes[4], bits.reshape(8, 8), c, numbers=False)
        for ax, t in zip(axes, titles):
            hide_axes(ax)
            ax.set_title(t, color=c["ink"], fontsize=10)
        fig.text(0.5, -0.02, f"hash = {fast['hash']}", ha="center", color=c["ink"],
                 family="monospace", fontsize=11)
        return fig

    def haar_level(c):
        # The 16×16 grid, and the four 8×8 bands one level of the transform makes of it.
        fig = plt.figure(figsize=(9.4, 4.6))
        left = fig.add_axes([0.0, 0.08, 0.42, 0.8])
        left.imshow(grid, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
        hide_axes(left)
        left.set_title(f"the {n}×{n} grid", color=c["ink"], fontsize=10)
        for title, row, col in BANDS:
            ax = fig.add_axes([0.5 + col * 0.25, 0.48 - row * 0.44, 0.22, 0.36])
            band = coef[row * 8:(row + 1) * 8, col * 8:(col + 1) * 8]
            if row == col == 0:
                ax.imshow(band, cmap="gray", interpolation="nearest")
                for s in ax.spines.values():
                    s.set_visible(True)
                    s.set_color(c["accent"])
                    s.set_linewidth(2)
                ax.set_xticks([])
                ax.set_yticks([])
            else:
                scale = max(np.abs(coef[:8, 8:]).max(), np.abs(coef[8:, :]).max()) or 1.0
                ax.imshow(band, cmap="gray", vmin=-scale, vmax=scale, interpolation="nearest")
                hide_axes(ax)
            ax.set_title(title, color=c["accent"] if row == col == 0 else c["ink"],
                         fontsize=9)
        fig.text(0.725, 0.0, "differences: mid-gray is 0, the same scale for all three",
                 ha="center", color=c["muted"], fontsize=8.5)
        return fig

    def pyramid(c):
        size, levels = full["size"], full["levels"]
        fc = np.array(full["coef"]).reshape(size, size)
        zoom = 32  # the corner holding the last two levels and the LL band
        fig, axes = plt.subplots(1, 3, figsize=(12.6, 4.6))
        axes[0].imshow(np.array(full["grid"]).reshape(size, size), cmap="gray", vmin=0,
                       vmax=255, interpolation="nearest")
        axes[0].set_title(f"reduced to {size}×{size}", color=c["ink"], fontsize=10)
        hide_axes(axes[0])
        img = _show_coef(axes[1], fc, size, levels)
        axes[2].imshow(img[:zoom, :zoom], cmap="gray", vmin=0, vmax=1, interpolation="nearest")
        hide_axes(axes[2])
        for ax, limit in ((axes[1], size), (axes[2], zoom)):
            s = size
            for _ in range(levels):  # each level's quarter, outlined
                s //= 2
                if s < limit:
                    ax.add_patch(plt.Rectangle((-0.5, -0.5), s, s, fill=False, linewidth=0.8,
                                               edgecolor=c["muted"]))
            ax.add_patch(plt.Rectangle((-0.5, -0.5), 8, 8, fill=False, linewidth=2,
                                       edgecolor=c["accent"]))
        axes[1].add_patch(plt.Rectangle((-0.5, -0.5), zoom, zoom, fill=False, linewidth=1,
                                        edgecolor=c["accent2"]))
        axes[1].set_title(f"{levels} Haar levels", color=c["ink"], fontsize=10)
        axes[2].set_title(f"the orange corner, enlarged; the LL band in blue",
                          color=c["ink"], fontsize=10)
        return fig

    def ll_band(c):
        fig, ax = plt.subplots(figsize=(5.4, 5.0))
        _ll_cells(ax, ll, bits, c)
        ax.set_title(f"median = {fast['median']:.3f}  ·  blue: above it, bit set",
                     color=c["ink"], fontsize=10)
        return fig

    def bit_grid(c):
        fig, ax = plt.subplots(figsize=(4.6, 4.6))
        draw_bits(ax, bits.reshape(8, 8), c, numbers=True)
        ax.set_title("bit set where the LL value > median", color=c["ink"], fontsize=10)
        return fig

    def bit_order(c):
        fig, ax = plt.subplots(figsize=(4.6, 4.6))
        ax.set_xlim(-0.5, 7.5)
        ax.set_ylim(7.5, -0.5)
        for k in range(64):
            i, j = divmod(k, 8)
            ax.add_patch(plt.Rectangle((j - 0.46, i - 0.46), 0.92, 0.92, color=c["off"],
                                       linewidth=0))
            ax.text(j, i, str(k), ha="center", va="center", fontsize=9, color=c["ink"])
        hide_axes(ax)
        ax.set_aspect("equal")
        ax.set_title("bit number of each LL value (0 = least significant)",
                     color=c["ink"], fontsize=10)
        return fig

    def modes(c):
        # Both modes' LL bands as block means: each divided by 2 to the number of levels
        # and multiplied by 255, so the two read on the scale of the grayscale image.
        fig, axes = plt.subplots(1, 2, figsize=(9.6, 5.0))
        apart = _bits_apart(fast["hash"], full["hash"])
        for ax, s, name in ((axes[0], fast, "PH_WHASH_FAST"), (axes[1], full, "PH_WHASH_FULL")):
            means = np.array(s["ll"]) * 255 / 2 ** s["levels"]
            changed = np.array(fast["bits"]) != np.array(full["bits"])
            _ll_cells(ax, means, s["bits"], c, fmt="{:.1f}", changed=changed)
            ax.set_title(f"{name}: {s['size']}×{s['size']}, {s['levels']} "
                         f"level{'s' if s['levels'] > 1 else ''}", color=c["ink"], fontsize=10)
            ax.text(3.5, 7.8, s["hash"], ha="center", va="top", color=c["ink"],
                    family="monospace", fontsize=10)
        fig.suptitle(f"{apart} bit{'' if apart == 1 else 's'} apart", color=c["ink"],
                     fontsize=10, y=0.02)
        return fig

    syn_index, syn, syn_image = _ties_synthetic(tool)

    def removal(c):
        # The synthetic image with the most ties: its LL band without the removal, the
        # bits with it, and the bits that differ outlined.
        fig, axes = plt.subplots(1, 3, figsize=(12, 4.4),
                                 gridspec_kw={"width_ratios": [0.8, 1, 1]})
        axes[0].imshow(syn_image)
        hide_axes(axes[0])
        axes[0].set_title(f"Synthetic image {syn_index}", color=c["ink"], fontsize=10)
        off, on = syn["fast"], syn["fast_removed"]
        changed = np.array(off["bits"]) != np.array(on["bits"])
        ties = np.array(off["ll"], np.float32) == np.float32(off["median"])
        _ll_cells(axes[1], off["ll"], off["bits"], c, changed=changed)
        axes[1].set_title(f"as computed: {int(ties.sum())} values equal the median",
                          color=c["ink"], fontsize=10)
        _ll_cells(axes[2], on["ll"], on["bits"], c, changed=changed)
        axes[2].set_title("with remove_max_haar_ll: the mean subtracted",
                          color=c["ink"], fontsize=10)
        for ax, s in ((axes[1], off), (axes[2], on)):
            ax.text(3.5, 7.8, f"median {s['median']:.3f}   {s['hash']}", ha="center",
                    va="top", color=c["ink"], family="monospace", fontsize=9)
        fig.suptitle(f"orange: the {int(changed.sum())} bits the removal changes",
                     color=c["accent2"], fontsize=10, y=0.02)
        return fig

    save(pipeline, out, "pipeline")
    save(haar_level, out, "haar-level")
    save(pyramid, out, "pyramid")
    save(ll_band, out, "ll-band")
    save(bit_grid, out, "bits")
    save(bit_order, out, "bit-order")
    save(modes, out, "modes")
    save(removal, out, "removal")
    _write_modes_tables(tool, out)
    _vs_ahash(tool, out)
    _write_load_grayscale(st, out)


def _write_modes_tables(tool, out):
    """How the FAST hash relates to the median-thresholded grid and to the FULL hash, and
    each mode's hash to itself with remove_max_haar_ll, over both corpora: tables the page
    includes, so the text holds no number that moves with the code."""
    with tempfile.TemporaryDirectory() as tmp:
        sets = [("synthetic", corpus.images(tool, "synthetic", tmp)),
                ("photos", corpus.images(tool, "photos", tmp))]
        measured = [(name, _whash_modes(tool, paths)) for name, paths in sets]
    # table: (what a row is called, (mode, one hash, the other)) per row group
    tables = {"modes-grid": (None, (("", "fast", "grid_median"),)),
              "modes-full": (None, (("", "fast", "full"),)),
              "modes-removal": ("Mode", (("FAST", "fast", "fast_removed"),
                                         ("FULL", "full", "full_removed")))}
    for table, (head, groups) in tables.items():
        cols = ([head] if head else []) + ["Images", "Same hash", "One bit apart",
                                           "Most bits apart"]
        lines = ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
        for mode, a, b in groups:
            lead = f"| {mode} " if head else ""
            for name, rs in measured:
                label = corpus.LABELS[name].format(n=len(rs))
                if not rs:
                    lines.append(f"{lead}| {label}: not available to this build | | | |")
                    continue
                d = [_bits_apart(r[a], r[b]) for r in rs]
                lines.append(f"{lead}| {label} | {sum(x == 0 for x in d)} | "
                             f"{sum(x == 1 for x in d)} | {max(d)} |")
        with open(os.path.join(out, f"{table}.md"), "w") as f:
            f.write("\n".join(lines) + "\n")


def _mean_bits(values):
    v = [bits_that_differ(x, BITS) for x in values if x is not None]
    return float(np.mean(v)) if v else float("nan")


def _vs_ahash(tool, out):
    """wHash against aHash over both corpora, edit by edit: the mean bits that differ from
    the original, as a figure of the edits where they part and a table of all of them;
    and how many bits aHash sets, which the median fixes at half for wHash."""
    datasets = [(k, d) for k, d in ((k, corpus.measure_corpus(tool, k))
                                    for k in ("photos", "synthetic")) if d["n"]]
    steps = {name: s for name, _, s in transforms()}
    xlabels = {name: x for name, x, _ in transforms()}

    def fig(c):
        series = (("ahash", "aHash", c["accent2"]), ("whash", "wHash", c["accent"]))
        figure, axes = plt.subplots(len(datasets), len(VS_AHASH), sharey=True,
                                    figsize=(12, 2.9 * len(datasets) + 0.6), squeeze=False)
        for row, (key, data) in zip(axes, datasets):
            for ax, name in zip(row, VS_AHASH):
                xs = list(range(len(steps[name])))
                for algo, _, color in series:
                    ys = [_mean_bits(v) for _, v in data["robust"][algo][name]]
                    ax.plot(xs, ys, color=color, linewidth=2, marker="o", markersize=4)
                ax.set_xticks(xs, [f"{v:g}" for v, _ in steps[name]], fontsize=8)
                ax.set_title(name, color=c["ink"], fontsize=10)
                ax.set_xlabel(xlabels[name], color=c["muted"], fontsize=8.5)
                style_axes(ax, c)
            row[0].set_ylabel(f"{SHORT[key]} ({data['n']})\nmean bits that differ",
                              color=c["muted"], fontsize=8.5)
        figure.tight_layout(rect=(0, 0, 1, 0.93))
        handles = [plt.Line2D([], [], color=col, linewidth=2, marker="o", markersize=4)
                   for _, _, col in series]
        figure.legend(handles, [n for _, n, _ in series], loc="upper center", ncol=2,
                      frameon=False, labelcolor=c["ink"], fontsize=9.5,
                      bbox_to_anchor=(0.5, 1.0))
        return figure

    save(fig, out, "vs-ahash")

    head = "| Transform | Strength | " + " | ".join(
        f"{SHORT[k]}, {a}" for k, _ in datasets for a in ("aHash", "wHash")) + " |"
    lines = [head, "|---|---|" + "---|" * (2 * len(datasets))]
    for name, _, s in transforms():
        for k, (strength, _) in enumerate(s):
            cells = [f"{_mean_bits(d['robust'][a][name][k][1]):.1f}"
                     for _, d in datasets for a in ("ahash", "whash")]
            lines.append(f"| {name} | {strength:g} | " + " | ".join(cells) + " |")
    with open(os.path.join(out, "vs-ahash-table.md"), "w") as f:
        f.write("\n".join(lines) + "\n")

    with tempfile.TemporaryDirectory() as tmp:
        photos = _whash_modes(tool, corpus.images(tool, "photos", tmp))
    if photos:
        n = np.array([bin(int(r["ahash"], 16)).count("1") for r in photos])
        lo, hi = np.percentile(n, [10, 90])
        text = (f"Over the {len(n)} photographs, aHash sets between {n.min()} and {n.max()} "
                f"of its 64 bits, and on the middle 80 % of them between {lo:.0f} and "
                f"{hi:.0f}.")
    else:
        text = "The photographs were not available to this build."
    with open(os.path.join(out, "ahash-bits.md"), "w") as f:
        f.write(text + "\n")


def _write_load_grayscale(st, out):
    """One sentence on the example photograph hashed from the decoder's grayscale."""
    a, b = st["fast"]["hash"], st["hash_load_grayscale"]
    n = _bits_apart(a, b)
    text = (f"The example photograph hashes to `{a}` both ways." if n == 0 else
            f"The example photograph hashes to `{a}` with the library's grayscale and to "
            f"`{b}` with the decoder's, {n} bit{'' if n == 1 else 's'} apart.")
    with open(os.path.join(out, "load-grayscale.md"), "w") as f:
        f.write(text + "\n")
