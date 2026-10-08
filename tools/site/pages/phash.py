"""The figures of docs/theory/phash.md, from `site_stages phash` and
`site_stages phash-variants`."""
import os

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LogNorm

from draw.corpus_charts import share, two_hashes_by_edit, two_hashes_separability
from draw.markdown import load_grayscale_hash, write_text
from draw.style import (draw_bits, gray_panel, hash_footer, hide_axes, image_panel, save,
                        stage_strip, style_axes, value_cells)
from measure import corpus
from measure.corpus import SHORT
from measure.digests import bits_apart
from measure.separability import COPY_STRENGTHS, separability, variant_distances
from measure.tool import run_stages

ALGO = "phash"
BITS = 64  # the robustness chart's scale: bits of the hash

# What `site_stages phash-variants` hashes with: block sizes 2 to 8 at dct_size 32 (the
# library accepts 4 to 8), and dct_sizes at the default block.
REDUCTIONS = range(2, 9)
DCT_SIZES = (8, 16, 24, 32)
# The edits the comparison with aHash draws; every edit is in the table under it.
VS_AHASH = ("Rotation", "Crop", "Gamma", "Noise", "JPEG quality")
# The copies whose movement the dct_size table shows: the edits of fine detail, with
# their short column names.
FINE = (("JPEG quality", "JPEG"), ("Gaussian blur", "blur"), ("Noise", "noise"))


def _crowded_synthetic(tool):
    """The image of the synthetic corpus whose AC coefficients crowd its pHash median the
    most, with its stages: (index, stages, pixels)."""
    best = None
    for path in corpus.images(tool, "synthetic"):
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


def _block_cells(ax, values, bits, c, size=8):
    """An r×r block of coefficients: each value in its cell, the cell in the accent where
    its bit is set; DC, the top-left cell, outlined."""
    value_cells(ax, values, bits, c, size, fmt="{:.0f}")
    _outline_dc(ax, c)


def _outline_dc(ax, c):
    ax.add_patch(plt.Rectangle((-0.5, -0.5), 1, 1, fill=False, linewidth=2,
                               edgecolor=c["accent2"]))


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
        def coefficients(ax):
            ax.imshow(np.maximum(magnitude, norm.vmin), cmap="gray", norm=norm,
                      interpolation="nearest")
            outline_block(ax, c)

        fig = stage_strip(c, (12, 2.9), [
            ("Decoded image", image_panel(original)),
            ("Grayscale", gray_panel(gray)),
            (f"Reduced to {n}×{n}", gray_panel(grid, grid=True)),
            ("DCT, |coefficients|", coefficients),
            (f"Bits of the {r}×{r} block", lambda ax: draw_bits(ax, bits.reshape(r, r), c,
                                                                numbers=False))])
        hash_footer(fig, c, st["hash"])
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
        _outline_dc(ax, c)
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
        _outline_dc(ax, c)
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

    def dct_sizes(c):
        sizes = st["dct_sizes"]
        default = sizes[-1]["bits"]
        f, axes = plt.subplots(2, len(sizes), figsize=(2.6 * len(sizes), 6.2))
        for col, d in enumerate(sizes):
            n = d["size"]
            axes[0, col].imshow(np.array(d["grid"]).reshape(n, n), cmap="gray", vmin=0,
                                vmax=255, interpolation="nearest")
            hide_axes(axes[0, col])
            axes[0, col].set_title(f"dct_size {n}", color=c["ink"], fontsize=10,
                                   fontweight="bold" if col == len(sizes) - 1 else "normal")
            b = np.array(d["bits"])
            k = int((b != np.array(default)).sum())
            value_cells(axes[1, col], b, b, c, 8, fontsize=0, changed=b != np.array(default))
            note = ("the default" if col == len(sizes) - 1 else
                    "the same hash" if k == 0 else f"{k} bits differ")
            axes[1, col].set_title(f"{d['hash']}\n{note}",
                                   color=c["muted"], family="monospace", fontsize=8.5)
        f.tight_layout(rect=(0, 0.04, 1, 1), h_pad=2.5)
        f.text(0.5, 0.0, "orange outline: a bit that differs from dct_size 32", ha="center",
               color=c["muted"], fontsize=9)
        return f

    save(dct_sizes, out, "dct-sizes")

    measured = corpus.measure_settings(tool, "phash-variants")
    _reduction_corpus(measured, out)
    _write_dct_table(measured, out)
    _write_dc(measured, out)
    two_hashes_by_edit(tool, out, "vs-ahash", (("ahash", "aHash"), ("phash", "pHash")),
                       VS_AHASH, SHORT)
    two_hashes_separability(tool, out, "vs-ahash-separability",
                            (("ahash", "aHash"), ("phash", "pHash")), SHORT)
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
    write_text(out, "margin-table.md",
               "| Image | AC coefficients in the margin | Hash | Bits the margin clears |\n"
               "|---|---|---|---|\n" + "\n".join(rows))


def _write_load_grayscale(st, out):
    """One sentence on the example photograph hashed from the decoder's grayscale."""
    a, b = st["hash"], st["hash_load_grayscale"]
    write_text(out, "load-grayscale.md", load_grayscale_hash(a, b, bits_apart(a, b)))


def _hash_bits(n):
    """A hash's first n bits in coefficient order: bit i is coefficient i (LSB first)."""
    return lambda h: np.array([(int(h, 16) >> i) & 1 for i in range(n)], np.uint8)


def _variant_rows(data, variants):
    """[(variant, bits, d′, threshold, accepted share, pairs, distinct originals)]."""
    rows = []
    for v, n in variants:
        c, d = variant_distances(data["images"], v, _hash_bits(n))
        dprime, t, fmr = separability(c, d, True)
        rows.append((v, n, dprime, t, fmr, len(d), len({img[0][v] for img in data["images"]})))
    return rows


def _reduction_corpus(measured, out):
    """d′ and the share of different pairs within the threshold against reduction_size,
    both corpora; and their table, with the distinct hashes over the originals."""
    variants = [(f"r{r}", r * r) for r in REDUCTIONS]
    present = [(k, measured[k]) for k in ("photos", "synthetic") if measured[k]["n"]]
    rows = {k: _variant_rows(d, variants) for k, d in present}

    def fig(c):
        colors = {"photos": c["accent"], "synthetic": c["accent2"]}
        f, axes = plt.subplots(1, 2, figsize=(10, 3.9))
        for key, data in present:
            for ax, col, scale in ((axes[0], 2, 1), (axes[1], 4, 100)):
                ax.plot(list(REDUCTIONS), [r[col] * scale for r in rows[key]],
                        color=colors[key], linewidth=2, marker="o", markersize=4.5,
                        label=data["label"])
        for ax, ylabel in ((axes[0], "d′ (higher separates better)"),
                           (axes[1], "% of different pairs within the threshold")):
            ax.axvspan(1.6, 3.5, color=c["grid"], alpha=0.5, linewidth=0)
            ax.set_xticks(list(REDUCTIONS))
            ax.set_xlabel("reduction_size", color=c["muted"], fontsize=9)
            ax.set_ylabel(ylabel, color=c["muted"], fontsize=9)
            style_axes(ax, c)
        axes[1].set_yscale("symlog", linthresh=0.1)
        ticks = [0, 0.1, 0.3, 1, 3, 10, 30, 100]
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
        f.text(0.5, 0.0, "Shaded: sizes below the range the library accepts, computed the same "
               "way. The threshold accepts 95 % of the copies.", ha="center",
               color=c["muted"], fontsize=9)
        return f

    if present:
        save(fig, out, "reduction-corpus")
    lines = ["| Corpus | reduction_size | Bits | d′ | Threshold | Different pairs within it "
             "| Distinct hashes of the originals |", "|---|---|---|---|---|---|---|"]
    for key in ("photos", "synthetic"):
        data = measured[key]
        if not data["n"]:
            lines.append(f"| {data['label']}: not available to this build | | | | | | |")
            continue
        for v, n, dprime, t, fmr, npairs, distinct in rows[key]:
            r = int(v[1:])
            size = f"**{r}**" if r == 8 else (f"{r} (below the range)" if r < 4 else str(r))
            lines.append(f"| {SHORT[key]} | {size} | {n} | {dprime:.2f} | {t:g} of {n} | "
                         f"{share(fmr, npairs)} | {distinct} of {data['n']} |")
    write_text(out, "reduction-corpus-table.md", "\n".join(lines))


def _write_dct_table(measured, out):
    """Each dct_size over both corpora: d′, the threshold, the different pairs within it,
    and how far the copies of fine-detail edits move."""
    edits = list(COPY_STRENGTHS)
    lines = ["| Corpus | dct_size | d′ | Threshold | Different pairs within it | " +
             " | ".join(f"Copies, {name} {COPY_STRENGTHS[e]:g}" for e, name in FINE) + " |",
             "|---|---|---|---|---|" + "---|" * len(FINE)]
    for key in ("photos", "synthetic"):
        data = measured[key]
        if not data["n"]:
            lines.append(f"| {data['label']}: not available to this build | | | | |" +
                         " |" * len(FINE))
            continue
        for v, n, dprime, t, fmr, npairs, _ in _variant_rows(
                data, [(f"d{s}", 64) for s in DCT_SIZES]):
            b = np.array([[_hash_bits(64)(r[v]) for r in img] for img in data["images"]])
            moved = (b[:, 1:] != b[:, :1]).sum(axis=2).mean(axis=0)
            size = f"**{v[1:]}**" if v == "d32" else v[1:]
            lines.append(f"| {SHORT[key]} | {size} | {dprime:.2f} | {t:g} of 64 | "
                         f"{share(fmr, npairs)} | " +
                         " | ".join(f"{moved[edits.index(e)]:.1f}" for e, _ in FINE) + " |")
    write_text(out, "dct-corpus-table.md", "\n".join(lines))


def _write_dc(measured, out):
    """How many hashed files, originals and copies of both corpora, have the DC bit clear."""
    files = [row for k in ("photos", "synthetic") for img in measured[k]["images"]
             for row in img]
    if not files:
        write_text(out, "dc.md", "The corpora were not available to this build.")
        return
    clear = sum(1 for row in files if not int(row["r8"], 16) & 1)
    text = (f"Over the {len(files):,} images hashed for the charts of this page, both "
            f"corpora's originals and their copies, the DC bit is set in "
            f"{'every one' if clear == 0 else f'all but {clear}'}.")
    write_text(out, "dc.md", text)
