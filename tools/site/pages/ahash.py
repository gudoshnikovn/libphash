"""The figures of docs/theory/ahash.md, from `site_stages ahash` and
`site_stages ahash-variants`."""
import os
import tempfile

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

from draw.corpus_charts import share
from draw.markdown import load_grayscale_hash, write_text
from draw.style import (draw_bits, gray_panel, hash_footer, hide_axes, image_panel, save,
                        stage_strip, value_cells)
from measure import corpus
from measure.corpus import SHORT
from measure.digests import bits_apart, digest_bits
from measure.separability import COPY_STRENGTHS, separability, variant_distances
from measure.tool import run_stages

ALGO = "ahash"
BITS = 64  # the robustness chart's scale: bits of the hash

# The reductions `site_stages ahash` and `ahash-variants` compare, the library's first.
REDUCTIONS = (("area", "exact area average"), ("mitchell", "Mitchell filter"),
              ("nearest", "nearest pixel"))


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
        ax.set_title("bit set where cell ≥ mean", color=c["ink"], fontsize=10)
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
    _reductions_figure(st, out)
    write_text(out, "load-grayscale.md", load_grayscale_hash(
        st["hash"], st["hash_load_grayscale"], bits_apart(st["hash"], st["hash_load_grayscale"])))

    measured = corpus.measure_settings(tool, "ahash-variants")
    _write_reductions_table(measured, out)
    _write_ties_table(measured, out)
    _ties_figure(tool, measured, out)


def _bits(hexhash):
    return digest_bits(hexhash)


def _reductions_figure(st, out):
    """The example photograph's 8×8 grid by each reduction, with its bits; the cells whose
    bit differs from the library's outlined."""
    red = st["reductions"]
    ref = np.array(red["area"]["bits"])

    def fig(c):
        f, axes = plt.subplots(1, len(REDUCTIONS), figsize=(10, 3.7))
        for ax, (key, name) in zip(axes, REDUCTIONS):
            b = np.array(red[key]["bits"])
            changed = b != ref
            value_cells(ax, red[key]["grid"], b, c, 8, fontsize=7.5, changed=changed)
            note = ("the library" if key == "area" else
                    f"{int(changed.sum())} bit{'' if changed.sum() == 1 else 's'} differ")
            ax.set_title(f"{name}\n{note}", color=c["ink"], fontsize=10,
                         fontweight="bold" if key == "area" else "normal")
        f.tight_layout(rect=(0, 0.05, 1, 1))
        f.text(0.5, 0.0, "blue: at or above the grid's mean, bit set · orange outline: a bit "
               "that differs from the exact area average", ha="center", color=c["muted"],
               fontsize=9)
        return f

    save(fig, out, "reductions")


def _write_reductions_table(measured, out):
    """Each reduction over both corpora: d′, the threshold that accepts 95 % of the copies,
    the different pairs it lets through, and the different pairs with identical hashes."""
    lines = ["| Corpus | Reduction | d′ | Threshold | Different pairs within it "
             "| Different pairs with the same hash |", "|---|---|---|---|---|---|"]
    for key in ("photos", "synthetic"):
        data = measured[key]
        if not data["n"]:
            lines.append(f"| {data['label']}: not available to this build | | | | | |")
            continue
        for variant, name in REDUCTIONS:
            c, d = variant_distances(data["images"], variant, _bits)
            dprime, t, fmr = separability(c, d, True)
            name = f"**{name}**" if variant == "area" else name
            lines.append(f"| {SHORT[key]} | {name} | {dprime:.2f} | {t:g} of 64 | "
                         f"{share(fmr, len(d))} | {int((d == 0).sum())} |")
    write_text(out, "reductions.md", "\n".join(lines))

    # The copies one edit at a time: rows are [original, copy…] in COPY_STRENGTHS order.
    present = [k for k in ("photos", "synthetic") if measured[k]["n"]]
    lines = ["| Edit | " + " | ".join(f"{SHORT[k]}, {name}" for k in present
                                      for _, name in REDUCTIONS) + " |",
             "|---|" + "---|" * (len(present) * len(REDUCTIONS))]
    means = {}
    for k in present:
        for variant, _ in REDUCTIONS:
            b = np.array([[_bits(row[variant]) for row in img]
                          for img in measured[k]["images"]], np.uint8)
            means[k, variant] = (b[:, 1:] != b[:, :1]).sum(axis=2).mean(axis=0)
    for e, (edit, strength) in enumerate(COPY_STRENGTHS.items()):
        lines.append(f"| {edit} {strength:g} | " + " | ".join(
            f"{means[k, v][e]:.1f}" for k in present for v, _ in REDUCTIONS) + " |")
    write_text(out, "reductions-edits.md", "\n".join(lines))


def _write_ties_table(measured, out):
    """Over both corpora: how many bits the hash sets, how many hashes have all 64, and
    what thresholding at the mean rounded down would change."""
    lines = ["| Corpus | Bits set: range; middle 80 % | Hashes with all 64 "
             "| Hashes a rounded-down mean changes; most bits "
             "| Different pairs within the threshold: exact mean | rounded-down mean |",
             "|---|---|---|---|---|---|"]
    for key in ("photos", "synthetic"):
        data = measured[key]
        if not data["n"]:
            lines.append(f"| {data['label']}: not available to this build | | | | | |")
            continue
        ones = np.array([int(_bits(img[0]["area"]).sum()) for img in data["images"]])
        lo, hi = np.percentile(ones, [10, 90])
        moved = np.array([bits_apart(img[0]["area"], img[0]["floor_mean"])
                          for img in data["images"]])
        within = []
        for variant in ("area", "floor_mean"):
            c, d = variant_distances(data["images"], variant, _bits)
            within.append(share(separability(c, d, True)[2], len(d)))
        lines.append(f"| {SHORT[key]} ({data['n']}) | {ones.min()}–{ones.max()}; "
                     f"{lo:.0f}–{hi:.0f} | "
                     f"{int((ones == 64).sum())} | {int((moved > 0).sum())}; {moved.max()} | "
                     f"{within[0]} | {within[1]} |")
    write_text(out, "ties.md", "\n".join(lines))


def _ties_figure(tool, measured, out):
    """The corpus image whose hash a mean rounded down changes the most: the image, its
    grid against the exact mean, and against the rounded one, with the cells that differ
    outlined."""
    candidates = [(bits_apart(img[0]["area"], img[0]["floor_mean"]), key, i)
                  for key in ("synthetic", "photos") for i, img in
                  enumerate(measured[key]["images"])]
    if not candidates:
        return
    _, key, pick = max(candidates)
    with tempfile.TemporaryDirectory() as tmp:
        ppm = os.path.join(tmp, "image.ppm")
        Image.open(corpus.images(tool, key)[pick]).convert("RGB").save(ppm)
        st, px = run_stages(tool, ALGO, ppm, ("original.ppm",))
    grid = np.array(st["grid"])
    exact = np.array(st["bits"])
    floor = (grid >= int(grid.sum()) // grid.size).astype(int)
    if "".join(map(str, floor)) != format(int(measured[key]["images"][pick][0]["floor_mean"],
                                              16), "064b"):
        raise SystemExit("render: ahash: the rounded-down mean disagrees with site_stages")
    what = (f"Photograph {pick + 1} of the corpus" if key == "photos"
            else f"Synthetic image {pick}")

    def fig(c):
        f, axes = plt.subplots(1, 3, figsize=(10, 3.7))
        axes[0].imshow(px["original.ppm"], interpolation="nearest")
        hide_axes(axes[0])
        axes[0].set_title(what, color=c["ink"], fontsize=10)
        for ax, b, title in ((axes[1], exact, f"mean {st['mean']:g}, compared exactly"),
                             (axes[2], floor, f"mean rounded down, {int(grid.sum()) // 64}")):
            value_cells(ax, grid, b, c, 8, fontsize=7.5, changed=b != exact)
            ax.set_title(f"{title}\n{int(b.sum())} of 64 bits set", color=c["ink"],
                         fontsize=10)
        f.tight_layout(rect=(0, 0.05, 1, 1))
        f.text(0.5, 0.0, "blue: bit set · orange outline: a bit the rounded mean sets and the "
               "exact one does not", ha="center", color=c["muted"], fontsize=9)
        return f

    save(fig, out, "ties")
