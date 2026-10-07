"""The figures of docs/theory/phash.md, from `site_stages phash`."""
import glob
import os
import subprocess
import tempfile

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LogNorm

from common import draw_bits, hide_axes, run_stages, save

ALGO = "phash"
BITS = 64  # the robustness chart's scale: bits of the hash


def _crowded_synthetic(tool):
    """The image of the synthetic corpus whose AC coefficients crowd its pHash median the
    most, with its stages: (index, stages, pixels)."""
    best = None
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([tool, "corpus", tmp], check=True)
        for path in sorted(glob.glob(os.path.join(tmp, "*.ppm"))):
            st, px = run_stages(tool, ALGO, path, ("original.ppm",))
            near = _within_margin(st).sum()
            if best is None or near > best[0]:
                best = (near, int(os.path.basename(path)[:2]), st, px["original.ppm"])
    return best[1:]


def _ac(st):
    """The 63 AC coefficients of the 8×8 block, as the library holds them (float32)."""
    return np.array(st["block"], np.float32)[1:]


def _median(ac):
    return np.sort(ac)[len(ac) // 2]  # 63 values: the middle one


def _within_margin(st):
    """Which AC coefficients lie within the margin of the median, on either side."""
    ac = _ac(st)
    return np.abs(ac - _median(ac)) <= np.float32(st["margin"]) * (ac.max() - ac.min())


def _bare_bits(st):
    """The bits at the bare median, without the margin: pHash's ph_dct_imagehash()."""
    block = np.array(st["block"], np.float32)
    return (block > _median(block[1:])).astype(int)


def _block_cells(ax, values, bits, c, size=8, fmt="{:.0f}", fontsize=8):
    """An r×r block of coefficients: each value in its cell, the cell in the accent where
    its bit is set; DC, the top-left cell, outlined."""
    for k, v in enumerate(values):
        i, j = divmod(k, size)
        ax.add_patch(plt.Rectangle((j - 0.46, i - 0.46), 0.92, 0.92, linewidth=0,
                                   color=c["accent"] if bits[k] else c["off"]))
        ax.text(j, i, fmt.format(v), ha="center", va="center", fontsize=fontsize,
                color=c["on_text"] if bits[k] else c["ink"])
    ax.add_patch(plt.Rectangle((-0.5, -0.5), 1, 1, fill=False, linewidth=2,
                               edgecolor=c["accent2"]))
    ax.set_xlim(-0.5, size - 0.5)
    ax.set_ylim(size - 0.5, -0.5)
    ax.set_aspect("equal")
    hide_axes(ax)


def figures(tool, image, out_dir):
    st, px = run_stages(tool, ALGO, image, ("original.ppm", "gray.pgm"))
    original, gray = px["original.ppm"], px["gray.pgm"]
    n, r = st["dct_size"], st["block_size"]
    grid = np.array(st["grid"]).reshape(n, n)
    dct = np.array(st["dct"]).reshape(n, n)
    mat = np.array(st["matrix"]).reshape(r, n)
    block = np.array(st["block"])
    bits = np.array(st["bits"])
    reductions = {o["size"]: o for o in st["reductions"]}
    out = os.path.join(out_dir, ALGO)
    magnitude = np.abs(dct)
    norm = LogNorm(vmin=max(magnitude.min(), 1e-2), vmax=magnitude.max())

    def outline_block(ax, c, lw=1.5):
        ax.add_patch(plt.Rectangle((-0.5, -0.5), r, r, fill=False, linewidth=lw,
                                   edgecolor=c["accent"]))

    def pipeline(c):
        fig, axes = plt.subplots(1, 5, figsize=(12, 2.9))
        titles = ["Decoded image", "Grayscale", f"Reduced to {n}×{n}", "DCT, |coefficients|",
                  f"Bits of the {r}×{r} block"]
        axes[0].imshow(original)
        axes[1].imshow(gray, cmap="gray", vmin=0, vmax=255)
        axes[2].imshow(grid, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
        axes[3].imshow(np.maximum(magnitude, norm.vmin), cmap="gray", norm=norm,
                       interpolation="nearest")
        outline_block(axes[3], c)
        draw_bits(axes[4], bits.reshape(r, r), c, numbers=False)
        for ax, t in zip(axes, titles):
            hide_axes(ax)
            ax.set_title(t, color=c["ink"], fontsize=10)
        fig.text(0.5, -0.02, f"hash = {st['hash']}", ha="center", color=c["ink"],
                 family="monospace", fontsize=11)
        return fig

    def dct_map(c):
        fig, ax = plt.subplots(figsize=(5.4, 4.6))
        im = ax.imshow(np.maximum(magnitude, norm.vmin), cmap="gray", norm=norm,
                       interpolation="nearest")
        outline_block(ax, c, lw=2)
        ax.text(r - 0.2, r - 0.2, f"the {r}×{r} block kept", color=c["accent"], fontsize=9,
                va="top", bbox=dict(boxstyle="round,pad=0.2", facecolor=c["surface"],
                                    edgecolor="none"))
        ax.set_xlabel("horizontal frequency →", color=c["muted"], fontsize=9)
        ax.set_ylabel("← vertical frequency", color=c["muted"], fontsize=9)
        ax.set_xticks([])
        ax.set_yticks([])
        for s in ax.spines.values():
            s.set_visible(False)
        bar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
        bar.ax.tick_params(colors=c["muted"], labelsize=8)
        bar.outline.set_visible(False)
        bar.set_label("|coefficient|, log scale", color=c["muted"], fontsize=9)
        ax.set_title("all 32×32 coefficients: brighter is larger", color=c["ink"], fontsize=10)
        return fig

    def basis(c):
        # The 64 cosine patterns the 8×8 block measures: pattern (u, v) is row u of the
        # DCT matrix down the image times row v across it.
        fig, axes = plt.subplots(r, r, figsize=(5.6, 5.6))
        for u in range(r):
            for v in range(r):
                ax = axes[u, v]
                ax.imshow(np.outer(mat[u], mat[v]), cmap="gray", interpolation="nearest")
                hide_axes(ax)
                if u == 0:
                    ax.set_title(str(v), color=c["muted"], fontsize=8, pad=2)
                if v == 0:
                    ax.set_ylabel(str(u), color=c["muted"], fontsize=8, rotation=0,
                                  labelpad=8, va="center")
        fig.supxlabel("horizontal frequency", color=c["muted"], fontsize=9, y=0.06)
        fig.supylabel("vertical frequency", color=c["muted"], fontsize=9, x=0.06)
        fig.subplots_adjust(wspace=0.08, hspace=0.08)
        return fig

    def block_values(c):
        fig, ax = plt.subplots(figsize=(5.4, 5.0))
        _block_cells(ax, block, bits, c)
        ax.set_title(f"threshold = {st['threshold']:.2f}  ·  blue: above it, bit set",
                     color=c["ink"], fontsize=10)
        ax.text(0, -0.75, "DC", ha="center", va="bottom", color=c["accent2"], fontsize=9)
        return fig

    syn_index, syn, syn_image = _crowded_synthetic(tool)

    def margin(c):
        # The 63 AC coefficients in ascending order, as distances from their median in
        # units of their range; the band is the margin. Symmetric-log scale, linear inside
        # ±0.1 %, so the band is visible.
        fig, axes = plt.subplots(1, 2, figsize=(10, 3.6), sharey=True)
        cases = ((st, original, "Example photograph"),
                 (syn, syn_image, f"Synthetic image {syn_index}"))
        for ax, (s, img, title) in zip(axes, cases):
            ac = np.sort(_ac(s))
            med, rng = _median(ac), ac.max() - ac.min()
            y = (ac - med) / rng
            near = _within_margin(s)
            ax.axhspan(0, s["margin"], color=c["accent"], alpha=0.18, linewidth=0)
            ax.axhline(0, color=c["muted"], linewidth=0.8)
            ax.scatter(np.arange(1, len(ac) + 1), y, s=12, zorder=3,
                       color=[c["accent2"] if k else c["ink"] for k in near[np.argsort(_ac(s))]])
            ax.set_yscale("symlog", linthresh=s["margin"], linscale=1.2)
            ax.set_xlim(0, 64)
            ax.set_xlabel("AC coefficients, smallest to largest", color=c["muted"], fontsize=9)
            for side in ("left", "bottom"):
                ax.spines[side].set_color(c["grid"])
            ax.tick_params(colors=c["muted"], labelsize=8)
            ax.set_title(f"{title}: {int(near.sum())} of 63 in the margin",
                         color=c["ink"], fontsize=10)
            inset = ax.inset_axes([0.02, 0.62, 0.3, 0.36])
            inset.imshow(img)
            hide_axes(inset)
        axes[0].set_ylabel("(value − median) / range", color=c["muted"], fontsize=9)
        axes[0].set_yticks([-1, -0.1, -0.01, -0.001, 0, 0.001, 0.01, 0.1, 1])
        axes[0].set_yticklabels(["−1", "−0.1", "−0.01", "−0.001", "0", "0.001", "0.01",
                                 "0.1", "1"])
        return fig

    def bit_grid(c):
        fig, ax = plt.subplots(figsize=(4.6, 4.6))
        draw_bits(ax, bits.reshape(r, r), c, numbers=True)
        ax.add_patch(plt.Rectangle((-0.5, -0.5), 1, 1, fill=False, linewidth=2,
                                   edgecolor=c["accent2"]))
        ax.set_title("bit set where coefficient > threshold", color=c["ink"], fontsize=10)
        return fig

    def bit_order(c):
        fig, ax = plt.subplots(figsize=(4.6, 4.6))
        ax.set_xlim(-0.5, r - 0.5)
        ax.set_ylim(r - 0.5, -0.5)
        for k in range(r * r):
            i, j = divmod(k, r)
            ax.add_patch(plt.Rectangle((j - 0.46, i - 0.46), 0.92, 0.92, color=c["off"],
                                       linewidth=0))
            ax.text(j, i, str(k), ha="center", va="center", fontsize=9, color=c["ink"])
        ax.add_patch(plt.Rectangle((-0.5, -0.5), 1, 1, fill=False, linewidth=2,
                                   edgecolor=c["accent2"]))
        hide_axes(ax)
        ax.set_aspect("equal")
        ax.set_title("bit number of each coefficient (0 = least significant)",
                     color=c["ink"], fontsize=10)
        return fig

    def reduction(c):
        sizes = (min(reductions), max(reductions))
        fig, axes = plt.subplots(1, 2, figsize=(8.4, 4.6),
                                 gridspec_kw={"width_ratios": [s for s in sizes]})
        for ax, s in zip(axes, sizes):
            o = reductions[s]
            _block_cells(ax, o["block"], o["bits"], c, size=s)
            ax.set_title(f"reduction_size {s}: {s * s} bits", color=c["ink"], fontsize=10)
            ax.text((s - 1) / 2, s - 0.2, o["hash"], ha="center", va="top", color=c["ink"],
                    family="monospace", fontsize=10)
        return fig

    save(pipeline, out, "pipeline")
    save(dct_map, out, "dct-map")
    save(basis, out, "basis")
    save(block_values, out, "block")
    save(margin, out, "margin")
    save(bit_grid, out, "bits")
    save(bit_order, out, "bit-order")
    save(reduction, out, "reduction")
    _write_margin_facts(st, syn_index, syn, out)
    _write_load_grayscale(st, out)


def _write_margin_facts(st, syn_index, syn, out):
    """The numbers the margin paragraph quotes, as a table the page includes, so the text
    holds no number that moves with the code."""
    rows = []
    for name, s in (("The example photograph", st),
                    (f"Synthetic image {syn_index} of the tests' corpus", syn)):
        bare = _bare_bits(s)
        flipped = int((bare != np.array(s["bits"])).sum())
        rows.append(f"| {name} | {int(_within_margin(s).sum())} of 63 | "
                    f"`{s['hash']}` | {flipped} |")
    with open(os.path.join(out, "margin-table.md"), "w") as f:
        f.write("| Image | AC coefficients in the margin | Hash | Bits the margin clears |\n"
                "|---|---|---|---|\n" + "\n".join(rows) + "\n")


def _write_load_grayscale(st, out):
    """One sentence on the example photograph hashed from the decoder's grayscale."""
    a, b = st["hash"], st["hash_load_grayscale"]
    n = bin(int(a, 16) ^ int(b, 16)).count("1")
    text = (f"The example photograph hashes to `{a}` both ways." if n == 0 else
            f"The example photograph hashes to `{a}` with the library's grayscale and to "
            f"`{b}` with the decoder's, {n} bit{'' if n == 1 else 's'} apart.")
    with open(os.path.join(out, "load-grayscale.md"), "w") as f:
        f.write(text + "\n")
