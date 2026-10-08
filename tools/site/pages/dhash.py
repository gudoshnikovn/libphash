"""The figures of docs/theory/dhash.md, from `site_stages dhash` and
`site_stages dhash-variants`."""
import os

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

from draw.corpus_charts import share, two_hashes_by_edit, two_hashes_separability
from draw.markdown import write_text
from draw.style import (draw_bits, gray_panel, hash_footer, hide_axes, image_panel, save,
                        stage_strip)
from measure import corpus
from measure.corpus import SHORT
from measure.digests import digest_bits
from measure.separability import COPY_STRENGTHS, separability, variant_distances
from measure.tool import run_on_images, run_stages

ALGO = "dhash"
BITS = 64  # the robustness chart's scale: bits of the hash

# The grids `site_stages dhash-variants` compares, the library's first.
REDUCTIONS = (("mitchell", "Mitchell filter"), ("area", "exact area average"))
# The edits the comparison with aHash draws; every edit is in the table under it.
VS_AHASH = ("Rotation", "Crop", "Brightness", "Gamma", "Noise")


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
        fig = stage_strip(c, (10, 2.9), [
            ("Decoded image", image_panel(original)),
            ("Grayscale", gray_panel(gray)),
            (f"Reduced to {w}×{h}", gray_panel(grid, grid=True)),
            ("Bits", lambda ax: draw_bits(ax, bits, c, numbers=False))])
        hash_footer(fig, c, st["hash"])
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

    def reductions(c):
        # The 9×8 grid both ways, each comparison drawn between its two cells; a
        # comparison whose bit differs from the library's is outlined.
        area = np.array(st["grid_area"]).reshape(h, w)
        f, axes = plt.subplots(1, 2, figsize=(10.4, 4.2))
        for ax, (g, name) in zip(axes, ((grid, "Mitchell filter, the library"),
                                        (area, "exact area average"))):
            b = _bits(g)
            for (i, j), v in np.ndenumerate(g):
                _cell(ax, j * 1.4, i, v, c)
            for (i, j), x in np.ndenumerate(b):
                ax.text(j * 1.4 + 0.7, i, "<" if x else "≥", ha="center", va="center",
                        fontsize=10, fontweight="bold" if x else "normal",
                        color=c["accent"] if x else c["muted"],
                        bbox=dict(boxstyle="square,pad=0.15", facecolor="none",
                                  edgecolor=c["accent2"] if x != bits[i, j] else "none",
                                  linewidth=1.6))
            ax.set_xlim(-0.5, (w - 1) * 1.4 + 0.5)
            ax.set_ylim(h - 0.5, -0.5)
            ax.set_aspect("equal")
            hide_axes(ax)
            k = int((b != bits).sum())
            ax.set_title(name if g is grid else f"{name}: {k} bit{'' if k == 1 else 's'} "
                         "differ", color=c["ink"], fontsize=10)
        f.tight_layout(rect=(0, 0.05, 1, 1))
        f.text(0.5, 0.0, "blue “<”: the left cell is darker, bit set · orange outline: a bit "
               "that differs from the library's", ha="center", color=c["muted"], fontsize=9)
        return f

    save(reductions, out, "reductions")

    measured = corpus.measure_settings(tool, "dhash-variants")
    _write_reductions_tables(measured, out)
    _blind_spot(tool, measured, out)
    two_hashes_by_edit(tool, out, "vs-ahash", (("ahash", "aHash"), ("dhash", "dHash")),
                       VS_AHASH, SHORT)
    two_hashes_separability(tool, out, "vs-ahash-separability",
                            (("ahash", "aHash"), ("dhash", "dHash")), SHORT)


def _hash_bits(hexhash):
    return digest_bits(hexhash)


def _write_reductions_tables(measured, out):
    """Each grid over both corpora: d′, the threshold that accepts 95 % of the copies, the
    different pairs it lets through, the pairs with the same hash and the hashes with no
    bit set; and, folded away, the mean bits each edit moves."""
    lines = ["| Corpus | Grid | d′ | Threshold | Different pairs within it "
             "| Different pairs with the same hash | Hashes with no bit set |",
             "|---|---|---|---|---|---|---|"]
    means = {}
    present = [k for k in ("photos", "synthetic") if measured[k]["n"]]
    for key in ("photos", "synthetic"):
        data = measured[key]
        if not data["n"]:
            lines.append(f"| {data['label']}: not available to this build | | | | | | |")
            continue
        for variant, name in REDUCTIONS:
            c, d = variant_distances(data["images"], variant, _hash_bits)
            dprime, t, fmr = separability(c, d, True)
            b = np.array([[_hash_bits(row[variant]) for row in img]
                          for img in data["images"]], np.uint8)
            means[key, variant] = (b[:, 1:] != b[:, :1]).sum(axis=2).mean(axis=0)
            empty = int((b[:, 0].sum(axis=1) == 0).sum())
            name = f"**{name}**" if variant == "mitchell" else name
            lines.append(f"| {SHORT[key]} | {name} | {dprime:.2f} | {t:g} of 64 | "
                         f"{share(fmr, len(d))} | {int((d == 0).sum())} | {empty} |")
    write_text(out, "reductions.md", "\n".join(lines))

    lines = ["| Edit | " + " | ".join(f"{SHORT[k]}, {name}" for k in present
                                      for _, name in REDUCTIONS) + " |",
             "|---|" + "---|" * (len(present) * len(REDUCTIONS))]
    for e, (edit, strength) in enumerate(COPY_STRENGTHS.items()):
        lines.append(f"| {edit} {strength:g} | " + " | ".join(
            f"{means[k, v][e]:.1f}" for k in present for v, _ in REDUCTIONS) + " |")
    write_text(out, "reductions-edits.md", "\n".join(lines))


# --8<-- [start:blind-spot]
def _blind_spot_images(size=256):
    """Four images whose brightness changes in one direction only: stripes and a gradient
    running across the rows, and the same turned a quarter."""
    y = np.repeat(np.arange(size)[:, None], size, axis=1)
    stripes = np.where((y // 32) % 2 == 0, 60, 200).astype(np.uint8)
    sky = (220 - y * 140 // (size - 1)).astype(np.uint8)
    gray = [("horizontal stripes", stripes), ("darker from top to bottom", sky),
            ("vertical stripes", stripes.T.copy()),
            ("brighter from left to right", sky.T[:, ::-1].copy())]
    return [(name, Image.fromarray(g).convert("RGB")) for name, g in gray]
# --8<-- [end:blind-spot]


def _blind_spot(tool, measured, out):
    """The four images above with their hashes; and the bits set over both corpora."""
    made = _blind_spot_images()
    rows = run_on_images(tool, "dhash-variants", [im for _, im in made])
    panels = [(name, np.asarray(im), row["mitchell"]) for (name, im), row in zip(made, rows)]

    def fig(c):
        f, axes = plt.subplots(2, len(panels), figsize=(2.4 * len(panels), 5.8))
        for col, (name, px, hexhash) in enumerate(panels):
            axes[0, col].imshow(px, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
            hide_axes(axes[0, col])
            axes[0, col].set_title(name.replace(" from", "\nfrom"), color=c["ink"],
                                   fontsize=9)
            b = _hash_bits(hexhash).reshape(8, 8)
            draw_bits(axes[1, col], b, c, numbers=False)
            axes[1, col].set_title(f"{hexhash}\n{int(b.sum())} of 64 bits set",
                                   color=c["muted"], family="monospace", fontsize=8.5)
        f.tight_layout(h_pad=2.5)
        return f

    save(fig, out, "blind-spot")

    lines = ["| Corpus | Bits set: fewest–most; middle 80 % | Hashes with no bit set |",
             "|---|---|---|"]
    for key in ("photos", "synthetic"):
        data = measured[key]
        if not data["n"]:
            lines.append(f"| {data['label']}: not available to this build | | |")
            continue
        ones = np.array([int(_hash_bits(img[0]["mitchell"]).sum()) for img in data["images"]])
        lo, hi = np.percentile(ones, [10, 90])
        lines.append(f"| {SHORT[key]} ({data['n']}) | {ones.min()}–{ones.max()}; "
                     f"{lo:.0f}–{hi:.0f} | {int((ones == 0).sum())} |")
    write_text(out, "bits-set.md", "\n".join(lines))


def _bits(grid):
    """The bits of a 9×8 grid, as the library sets them: left < right."""
    return (grid[:, :-1] < grid[:, 1:]).astype(int)
