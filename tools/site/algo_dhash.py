"""The figures of docs/theory/dhash.md, from `site_stages dhash`."""
import os

import matplotlib.pyplot as plt
import numpy as np

from common import draw_bits, hide_axes, run_stages, save

ALGO = "dhash"
BITS = 64  # the robustness chart's scale: bits of the hash


def _cell(ax, x, y, value, c, edge=None):
    """A gray cell holding its value, as on the grid figure."""
    ax.add_patch(plt.Rectangle((x - 0.46, y - 0.46), 0.92, 0.92, linewidth=2 if edge else 0,
                               facecolor=str(value / 255), edgecolor=edge or "none"))
    ax.text(x, y, str(value), ha="center", va="center", fontsize=9,
            color="#000000" if value > 140 else "#ffffff")


def _bit(ax, x, y, b, c, label=None):
    ax.add_patch(plt.Rectangle((x - 0.36, y - 0.36), 0.72, 0.72, linewidth=0,
                               color=c["accent"] if b else c["off"]))
    ax.text(x, y, str(b) if label is None else label, ha="center", va="center", fontsize=9,
            color=c["on_text"] if b else c["muted"])


def figures(tool, image, out_dir):
    st, px = run_stages(tool, ALGO, image, ("original.ppm", "gray.pgm"))
    original, gray = px["original.ppm"], px["gray.pgm"]
    w, h = st["grid_width"], st["grid_height"]
    grid = np.array(st["grid"]).reshape(h, w)
    dec = np.array(st["grid_load_grayscale"]).reshape(h, w)
    bits = np.array(st["bits"]).reshape(h, w - 1)
    out = os.path.join(out_dir, ALGO)

    def pipeline(c):
        fig, axes = plt.subplots(1, 4, figsize=(10, 2.9))
        titles = ["Decoded image", "Grayscale", f"Reduced to {w}×{h}", "Bits"]
        axes[0].imshow(original)
        axes[1].imshow(gray, cmap="gray", vmin=0, vmax=255)
        axes[2].imshow(grid, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
        draw_bits(axes[3], bits, c, numbers=False)
        for ax, t in zip(axes, titles):
            hide_axes(ax)
            ax.set_title(t, color=c["ink"], fontsize=10)
        fig.text(0.5, -0.02, f"hash = {st['hash']}", ha="center", color=c["ink"],
                 family="monospace", fontsize=11)
        return fig

    def grid_values(c):
        # Each cell with its value, and between two neighbors the comparison that makes
        # the bit: "<" (bit 1) when the left one is darker, "≥" (bit 0) otherwise.
        fig, ax = plt.subplots(figsize=(5.4, 4.6))
        for (i, j), v in np.ndenumerate(grid):
            _cell(ax, j * 1.4, i, v, c)
        for (i, j), b in np.ndenumerate(bits):
            ax.text(j * 1.4 + 0.7, i, "<" if b else "≥", ha="center", va="center",
                    fontsize=10, fontweight="bold" if b else "normal",
                    color=c["accent"] if b else c["muted"])
        ax.set_xlim(-0.5, (w - 1) * 1.4 + 0.5)
        ax.set_ylim(h - 0.5, -0.5)
        ax.set_aspect("equal")
        hide_axes(ax)
        ax.set_title("blue “<”: the left cell is darker, and the bit is set",
                     color=c["ink"], fontsize=10)
        return fig

    def bit_grid(c):
        fig, ax = plt.subplots(figsize=(4.6, 4.6))
        draw_bits(ax, bits, c, numbers=True)
        ax.set_title("bit set where left < right", color=c["ink"], fontsize=10)
        return fig

    def bit_order(c):
        # The 9×8 cells, and on each boundary between two neighbors the number of the bit
        # their comparison sets.
        fig, ax = plt.subplots(figsize=(5.4, 4.6))
        for i in range(h):
            for j in range(w):
                ax.add_patch(plt.Rectangle((j * 1.4 - 0.46, i - 0.46), 0.92, 0.92,
                                           color=c["off"], linewidth=0))
            for j in range(w - 1):
                ax.text(j * 1.4 + 0.7, i, str(63 - (i * (w - 1) + j)), ha="center",
                        va="center", fontsize=8.5, color=c["ink"],
                        bbox=dict(boxstyle="round,pad=0.15", facecolor=c["surface"],
                                  edgecolor="none"))
        ax.set_xlim(-0.5, (w - 1) * 1.4 + 0.5)
        ax.set_ylim(h - 0.5, -0.5)
        ax.set_aspect("equal")
        hide_axes(ax)
        ax.set_title("bit set by each pair of neighbors (63 = most significant)",
                     color=c["ink"], fontsize=10)
        return fig

    def one_row(c):
        # Why 9 columns: the top row of the grid, and the 8 bits its 8 neighbor pairs give.
        fig, ax = plt.subplots(figsize=(8, 2.2))
        for j in range(w):
            _cell(ax, j, 1, grid[0, j], c)
        for j in range(w - 1):
            ax.annotate("", xy=(j + 0.5, 0.36), xytext=(j + 0.12, 0.54),
                        arrowprops=dict(arrowstyle="-", color=c["muted"], linewidth=0.8))
            ax.annotate("", xy=(j + 0.5, 0.36), xytext=(j + 0.88, 0.54),
                        arrowprops=dict(arrowstyle="-", color=c["muted"], linewidth=0.8))
            _bit(ax, j + 0.5, 0, bits[0, j], c)
        ax.text(-0.75, 1, "9 cells", ha="right", va="center", color=c["muted"], fontsize=9)
        ax.text(-0.25, 0, "8 bits", ha="right", va="center", color=c["muted"], fontsize=9)
        ax.set_xlim(-1.9, w - 0.5)
        ax.set_ylim(-0.5, 1.5)
        ax.set_aspect("equal")
        hide_axes(ax)
        return fig

    def load_grayscale(c):
        # The rows where the decoder's grayscale gives a different bit, both ways; the two
        # cells of each such pair are outlined.
        differ = np.argwhere(bits != _bits(dec))
        rows = sorted({int(i) for i, _ in differ}) or [0]
        fig, axes = plt.subplots(len(rows), 1, figsize=(8, 1.7 * len(rows)), squeeze=False)
        for ax, r in zip(axes[:, 0], rows):
            pairs = {int(j) for i, j in differ if i == r}
            for y, (g, name) in enumerate(((grid, "library gray"), (dec, "decoder gray"))):
                b = _bits(g)
                for j in range(w):
                    edge = c["accent2"] if j in pairs or j - 1 in pairs else None
                    _cell(ax, j * 1.4, y, g[r, j], c, edge=edge)
                for j in range(w - 1):
                    ax.text(j * 1.4 + 0.7, y, "<" if b[r, j] else "≥", ha="center",
                            va="center", fontsize=10, color=c["accent"] if b[r, j] else
                            c["muted"], fontweight="bold" if b[r, j] else "normal")
                ax.text(-0.75, y, name, ha="right", va="center", color=c["muted"], fontsize=9)
            ax.set_xlim(-3.2, (w - 1) * 1.4 + 0.5)
            ax.set_ylim(1.5, -0.5)
            ax.set_aspect("equal")
            hide_axes(ax)
            ax.set_title(f"row {r + 1} of the grid", color=c["ink"], fontsize=10)
        n = len(differ)
        fig.text(0.5, 0.0, f"{n} bit{' differs' if n == 1 else 's differ'}:  "
                 f"{st['hash']}  vs  {st['hash_load_grayscale']}", ha="center",
                 color=c["ink"], family="monospace", fontsize=10)
        return fig

    save(pipeline, out, "pipeline")
    save(grid_values, out, "grid")
    save(bit_grid, out, "bits")
    save(bit_order, out, "bit-order")
    save(one_row, out, "nine-to-eight")
    save(load_grayscale, out, "load-grayscale")


def _bits(grid):
    """The bits of a 9×8 grid, as the library sets them: left < right."""
    return (grid[:, :-1] < grid[:, 1:]).astype(int)
