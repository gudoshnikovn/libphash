"""The figures of docs/theory/ahash.md, from `site_stages ahash`."""
import os

import matplotlib.pyplot as plt
import numpy as np

from draw.style import (draw_bits, gray_panel, hash_footer, hide_axes, image_panel, save,
                        stage_strip)
from measure.tool import run_stages

ALGO = "ahash"
BITS = 64  # the robustness chart's scale: bits of the hash


def figures(tool, image, out_dir):
    st, px = run_stages(tool, ALGO, image, ("original.ppm", "gray.pgm"))
    original, gray = px["original.ppm"], px["gray.pgm"]
    n = st["grid_size"]
    grid = np.array(st["grid"]).reshape(n, n)
    bits = np.array(st["bits"]).reshape(n, n)
    mean = st["mean"]
    out = os.path.join(out_dir, ALGO)

    def pipeline(c):
        fig = stage_strip(c, (10, 2.9), [
            ("Decoded image", image_panel(original)),
            ("Grayscale", gray_panel(gray)),
            (f"Reduced to {n}×{n}", gray_panel(grid, grid=True)),
            ("Bits", lambda ax: draw_bits(ax, bits, c, numbers=False))])
        hash_footer(fig, c, st["hash"])
        return fig

    def grid_values(c):
        fig, ax = plt.subplots(figsize=(4.6, 4.6))
        ax.imshow(grid, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
        for (i, j), v in np.ndenumerate(grid):
            above = v >= mean
            ax.text(j, i, str(v), ha="center", va="center", fontsize=9,
                    color="#000000" if v > 140 else "#ffffff",
                    fontweight="bold" if above else "normal")
        hide_axes(ax)
        ax.set_title(f"mean = {mean:.2f}  ·  bold: at or above the mean", color=c["ink"],
                     fontsize=10)
        return fig

    def bit_grid(c):
        fig, ax = plt.subplots(figsize=(4.6, 4.6))
        draw_bits(ax, bits, c, numbers=True)
        ax.set_title("bit set where pixel ≥ mean", color=c["ink"], fontsize=10)
        return fig

    def bit_order(c):
        fig, ax = plt.subplots(figsize=(4.6, 4.6))
        ax.set_xlim(-0.5, n - 0.5)
        ax.set_ylim(n - 0.5, -0.5)
        for i in range(n):
            for j in range(n):
                k = i * n + j
                ax.add_patch(plt.Rectangle((j - 0.46, i - 0.46), 0.92, 0.92,
                                           color=c["off"], linewidth=0))
                ax.text(j, i, str(63 - k), ha="center", va="center", fontsize=9,
                        color=c["ink"])
        hide_axes(ax)
        ax.set_title("bit number of each cell (63 = most significant)", color=c["ink"],
                     fontsize=10)
        return fig

    save(pipeline, out, "pipeline")
    save(grid_values, out, "grid")
    save(bit_grid, out, "bits")
    save(bit_order, out, "bit-order")
