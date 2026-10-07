"""The figures of docs/theory/color-hash.md, from `site_stages color_hash` and
`site_stages measure`."""
import concurrent.futures
import hashlib
import json
import math
import os
import subprocess
import tempfile

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

import corpus
from common import hide_axes, run_stages, save, style_axes
from corpus_charts import copies_and_different, separability, share
from transforms import content_edits

ALGO = "color_hash"
BITS = None  # not a bit vector: the charts show the library's own score
METRIC = "histogram intersection"
LOWER_IS_CLOSER = False
METRIC_RANGE = (0.0, 1.0)
FORMAT = "{:.3f}"
REFUSED = "a grayscale image"

SHORT = {"photos": "photographs", "synthetic": "synthetic images"}

# The opponent axes, as src/hashes/hashes.h defines them: (name, offset, distinct values).
AXES = {"rg": ("R − G", 255, 2 * 255 + 1), "by": ("2B − R − G", 510, 2 * 510 + 1),
        "wb": ("R + G + B", 0, 3 * 255 + 1)}

# --8<-- [start:tints]
# A shift of a few levels in one channel or in all three: (name, added to R, G, B).
TINTS = [("red +1", (1, 0, 0)), ("red +2", (2, 0, 0)), ("blue +2", (0, 0, 2)),
         ("all +3", (3, 3, 3))]
# --8<-- [end:tints]


def _digest(hexstr):
    return np.frombuffer(bytes.fromhex(hexstr), np.uint8)


# --8<-- [start:intersection]
def intersection(a, b):
    """ph_histogram_intersection() on two digests: each normalized by its own sum, the
    sum over bins of the smaller of the two shares."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    return float(np.minimum(a / a.sum(), b / b.sum()).sum())
# --8<-- [end:intersection]


def edges(axis, bins):
    """The lowest axis value of each bin after the first: bin k starts where the shifted
    value s reaches k · values / bins (the library's k = s · bins / values, truncated)."""
    _, offset, values = AXES[axis]
    return [math.ceil(k * values / bins) - offset for k in range(1, bins)]


def _measure(tool, ref, variants):
    """`site_stages measure`: every algorithm's score of each variant against `ref`."""
    out = subprocess.run([tool, "measure", ref, *variants], check=True, capture_output=True,
                         text=True).stdout
    return [json.loads(line) for line in out.splitlines()]


def figures(tool, image, out_dir):
    st, px = run_stages(tool, ALGO, image, ("original.ppm", "bins.pgm"))
    out = os.path.join(out_dir, ALGO)
    nrg, nby, nwb = st["bins_rg"], st["bins_by"], st["bins_wb"]
    digest = _digest(st["digest"])
    counts = np.array(st["counts"])
    volume = np.array(st["volume"])
    color = np.array(st["bin_color"]) / 255.0
    original = px["original.ppm"]
    binned = (color[px["bins.pgm"]] * 255).round().astype(np.uint8)

    def digest_text(fig, c, y):
        h = st["digest"]
        pad = chr(0xA0) * len("digest = ")
        rows = [h[i:i + 72] for i in range(0, len(h), 72)]
        fig.text(0.5, y, "digest = " + ("\n" + pad).join(rows), ha="center",
                 multialignment="left", color=c["ink"], family="monospace", fontsize=9,
                 va="top")

    def bars(ax, c, values, ylabel):
        k = np.arange(len(values))
        ax.bar(k, values, width=0.85, color=color, edgecolor=c["muted"], linewidth=0.3)
        ax.set_xlim(-1, len(values))
        ax.set_xticks(range(0, len(values) + 1, 18))
        ax.set_xlabel("bin", color=c["muted"], fontsize=9)
        ax.set_ylabel(ylabel, color=c["muted"], fontsize=9)
        style_axes(ax, c)

    def pipeline(c):
        fig = plt.figure(figsize=(10.4, 3.6))
        a0 = fig.add_axes([0.0, 0.12, 0.27, 0.78])
        a1 = fig.add_axes([0.29, 0.12, 0.27, 0.78])
        a2 = fig.add_axes([0.63, 0.2, 0.36, 0.66])
        a0.imshow(original)
        a1.imshow(binned)
        for ax, t in ((a0, "Decoded image"), (a1, "Each pixel in its bin's color")):
            hide_axes(ax)
            ax.set_title(t, color=c["ink"], fontsize=10)
        bars(a2, c, digest, "byte")
        a2.set_ylim(0, 255)
        a2.set_title("108 bins, scaled to the largest", color=c["ink"], fontsize=10)
        digest_text(fig, c, 0.02)
        return fig

    # The pixels of the example on the two chroma axes, a sample of them, in their own
    # colors; one panel per intensity third, with the bin edges and the bins no 8-bit
    # color reaches.
    rgb = original.reshape(-1, 3).astype(int)
    rng = np.random.default_rng(1)
    sample = rgb[rng.choice(len(rgb), size=min(len(rgb), 6000), replace=False)]
    rg = sample[:, 0] - sample[:, 1]
    by = 2 * sample[:, 2] - sample[:, 0] - sample[:, 1]
    third = np.minimum(sample.sum(axis=1) * nwb // AXES["wb"][2], nwb - 1)
    e_rg, e_by = edges("rg", nrg), edges("by", nby)
    lim_rg = [-AXES["rg"][1] - 0.5] + [e - 0.5 for e in e_rg] + [AXES["rg"][1] + 0.5]
    lim_by = [-AXES["by"][1] - 0.5] + [e - 0.5 for e in e_by] + [AXES["by"][1] + 0.5]
    wb_edges = [0] + edges("wb", nwb) + [AXES["wb"][2]]

    def plane(c):
        fig, axes = plt.subplots(1, nwb, figsize=(10.4, 4.0), sharey=True)
        for w, ax in enumerate(axes):
            for a in range(nrg):
                for b in range(nby):
                    if volume[(a * nby + b) * nwb + w] == 0:
                        ax.add_patch(plt.Rectangle((lim_rg[a], lim_by[b]),
                                                   lim_rg[a + 1] - lim_rg[a],
                                                   lim_by[b + 1] - lim_by[b],
                                                   color=c["off"], linewidth=0, zorder=0))
            for e in e_rg:
                ax.axvline(e - 0.5, color=c["muted"], linewidth=0.8)
            for e in e_by:
                ax.axhline(e - 0.5, color=c["muted"], linewidth=0.8)
            m = third == w
            ax.scatter(rg[m], by[m], s=5, c=sample[m] / 255.0, linewidths=0.2,
                       edgecolors=c["muted"], zorder=3)
            ax.plot([0], [0], marker="o", markersize=9, markerfacecolor="none",
                    markeredgecolor=c["accent2"], markeredgewidth=1.6, zorder=4)
            ax.set_xlim(lim_rg[0], lim_rg[-1])
            ax.set_ylim(lim_by[0], lim_by[-1])
            ax.set_xticks([-255, 0, 255])
            ax.set_yticks([-510, 0, 510])
            ax.set_xlabel("R − G  (red →)", color=c["muted"], fontsize=9)
            ax.set_title(f"R + G + B from {wb_edges[w]} to {wb_edges[w + 1] - 1}\n"
                         f"{int(m.sum() * 100 / len(m))} % of the pixels", color=c["ink"],
                         fontsize=9.5)
            ax.tick_params(colors=c["muted"], labelsize=8.5)
            for s in ax.spines.values():
                s.set_color(c["grid"])
        axes[0].set_ylabel("2B − R − G  (blue ↑)", color=c["muted"], fontsize=9)
        fig.text(0.5, -0.04, "Lines: the bin edges. Shaded: bins no 8-bit color falls into. "
                 "Circled: neutral gray, R = G = B.", ha="center", color=c["muted"],
                 fontsize=9)
        fig.tight_layout()
        return fig

    def layout(c):
        fig, ax = plt.subplots(figsize=(10.4, 3.7))
        for a in range(nrg):
            for b in range(nby):
                for w in range(nwb):
                    i = (a * nby + b) * nwb + w
                    x, y = b * nwb + w + b * 0.35, a
                    if volume[i]:
                        ink = "#000000" if color[i] @ [0.299, 0.587, 0.114] > 0.5 else "#ffffff"
                        ax.add_patch(plt.Rectangle((x - 0.46, y - 0.46), 0.92, 0.92,
                                                   color=color[i], linewidth=0))
                        ax.text(x, y - 0.12, f"{digest[i]:02x}", ha="center", va="center",
                                fontsize=8, family="monospace", color=ink)
                        ax.text(x, y + 0.24, str(i), ha="center", va="center", fontsize=6,
                                color=ink, alpha=0.8)
                    else:
                        ax.add_patch(plt.Rectangle((x - 0.46, y - 0.46), 0.92, 0.92,
                                                   facecolor="none", edgecolor=c["grid"],
                                                   linewidth=0.8))
                        ax.text(x, y, str(i), ha="center", va="center", fontsize=6,
                                color=c["muted"])
        ax.set_xlim(-0.6, nby * nwb + (nby - 1) * 0.35 - 0.4)
        ax.set_ylim(nrg + 0.3, -0.9)
        ends = {0: " (yellow)", nby - 1: " (blue)"}
        for b in range(nby):
            ax.text(b * nwb + b * 0.35 + 1, -0.75, f"2B − R − G bin {b}{ends.get(b, '')}",
                    ha="center", va="center", color=c["ink"], fontsize=8.5)
        ends = {0: " (green)", nrg - 1: " (red)"}
        for a in range(nrg):
            ax.text(-0.75, a, f"R − G bin {a}{ends.get(a, '')}", ha="right", va="center",
                    color=c["ink"], fontsize=8.5)
        ax.text((nby * nwb + (nby - 1) * 0.35) / 2 - 0.5, nrg + 0.05,
                "In each group of three, R + G + B from dark to light. In each cell, the byte "
                "in hexadecimal and the bin number;\nempty cells are the bins no 8-bit color "
                "reaches.", ha="center", va="top", color=c["muted"], fontsize=9)
        hide_axes(ax)
        ax.set_aspect("equal")
        return fig

    save(pipeline, out, "pipeline")
    save(plane, out, "plane")
    save(layout, out, "bit-order")
    _write_stage_numbers(st, out)
    with open(os.path.join(out, "load-grayscale.md"), "w") as f:
        f.write("With `ph_context_set_load_grayscale()` enabled, the example photograph is "
                "decoded to one channel, and "
                f"`ph_compute_color_hash()` refuses it: \"{st['load_grayscale']}\".\n")

    base = Image.open(image).convert("RGB")
    _intersection_figure(tool, base, digest, out)
    _blind_spots(tool, base, out)
    _tint_figure(tool, out)


def _write_stage_numbers(st, out):
    counts = np.array(st["counts"])
    volume = np.array(st["volume"])
    digest = _digest(st["digest"])
    total = counts.sum()
    top = np.sort(counts)[::-1]
    with open(os.path.join(out, "stage-numbers.md"), "w") as f:
        f.write(f"Of the 108 bins, {int((volume > 0).sum())} hold some 8-bit color; the "
                f"other {int((volume == 0).sum())} are combinations of the three axes no "
                f"color can take, and stay zero in every digest. The example photograph's "
                f"{total:,} pixels fall into {int((counts > 0).sum())} bins, "
                f"{int((digest > 0).sum())} of which get a nonzero byte; the largest holds "
                f"{top[0] / total:.0%} of the pixels and the five largest "
                f"{top[:5].sum() / total:.0%}.\n")


def _save_ppm(im, path):
    im.save(path)
    return path


def _intersection_figure(tool, base, digest, out):
    """The example's histogram against that of the example with its hue turned 30°: the
    two shares per bin and their minimum; the score computed here checked against the
    library's."""
    turned, _ = dict(content_edits()[1][2])[30](base)
    with tempfile.TemporaryDirectory() as tmp:
        ref = _save_ppm(base, os.path.join(tmp, "a.ppm"))
        var = _save_ppm(turned, os.path.join(tmp, "b.ppm"))
        st, _ = run_stages(tool, ALGO, var)
        lib = _measure(tool, ref, [var])[0][ALGO]
    other = _digest(st["digest"])
    mine = intersection(digest, other)
    if abs(mine - lib) > 1e-6:
        raise SystemExit(f"render: color_hash: intersection {mine:.6f} here, "
                         f"{lib:.6f} from the library")
    color = np.array(st["bin_color"]) / 255.0
    pa, pb = digest / digest.sum(), other / other.sum()
    used = np.flatnonzero((pa > 0) | (pb > 0))

    def fig(c):
        f = plt.figure(figsize=(10.4, 3.9))
        for n, (im, t) in enumerate(((base, "the example"), (turned, "its hue turned 30°"))):
            ax = f.add_axes([0.0, 0.52 - n * 0.48, 0.17, 0.4])
            ax.imshow(np.asarray(im))
            hide_axes(ax)
            ax.set_title(t, color=c["ink"], fontsize=9)
        ax = f.add_axes([0.25, 0.17, 0.74, 0.7])
        x = np.arange(len(used))
        ax.bar(x - 0.2, pa[used] * 100, width=0.4, color=color[used], edgecolor=c["ink"],
               linewidth=0.4, )
        ax.bar(x + 0.2, pb[used] * 100, width=0.4, color=color[used], edgecolor=c["ink"],
               linewidth=0.4, hatch="////")
        ax.step(np.r_[x - 0.5, x[-1] + 0.5], np.r_[np.minimum(pa, pb)[used], 0] * 100,
                where="post", color=c["accent2"], linewidth=1.6)
        ax.set_xticks(x, [str(i) for i in used], fontsize=7.5, rotation=90)
        ax.set_xlim(-0.6, len(used) - 0.4)
        ax.set_xlabel("bin (only those either image uses)", color=c["muted"], fontsize=9)
        ax.set_ylabel("% of the digest's sum", color=c["muted"], fontsize=9)
        handles = [plt.Rectangle((0, 0), 1, 1, facecolor=c["off"], edgecolor=c["ink"],
                                 linewidth=0.4),
                   plt.Rectangle((0, 0), 1, 1, facecolor=c["off"], edgecolor=c["ink"],
                                 linewidth=0.4, hatch="////"),
                   plt.Line2D([], [], color=c["accent2"], linewidth=1.6)]
        ax.legend(handles, ["the example", "hue turned 30°", "the smaller of the two"],
                  frameon=False, labelcolor=c["ink"], fontsize=9, ncol=3, loc="lower center",
                  bbox_to_anchor=(0.5, 1.0))
        style_axes(ax, c)
        ax.text(0.99, 0.95, f"sum of the smaller: {FORMAT.format(lib)}", ha="right", va="top",
                transform=ax.transAxes, color=c["ink"], fontsize=9.5,
                bbox={"facecolor": c["surface"], "edgecolor": "none", "pad": 1})
        return f

    save(fig, out, "intersection")


def _blind_spots(tool, base, out):
    """What ColorHash cannot see: the example with its pixels shuffled and turned a
    quarter, and pairs of flat colors; each scored by the library, with pHash beside it."""
    a = np.asarray(base)
    shuffled = Image.fromarray(a.reshape(-1, 3)[np.random.default_rng(1).permutation(
        a.shape[0] * a.shape[1])].reshape(a.shape))
    turned = base.transpose(Image.Transpose.ROTATE_90)
    flats = [("black", (8, 8, 8), "dark gray", (64, 64, 64)),
             ("light gray", (176, 176, 176), "white", (248, 248, 248)),
             ("black", (8, 8, 8), "white", (248, 248, 248)),
             ("gray", (128, 128, 128), "gray, red +1", (129, 128, 128))]
    with tempfile.TemporaryDirectory() as tmp:
        ref = _save_ppm(base, os.path.join(tmp, "ref.ppm"))
        rows = _measure(tool, ref, [_save_ppm(shuffled, os.path.join(tmp, "s.ppm")),
                                    _save_ppm(turned, os.path.join(tmp, "t.ppm"))])
        flat_scores = []
        for n, (_, c1, _, c2) in enumerate(flats):
            p1 = _save_ppm(Image.new("RGB", (64, 64), c1), os.path.join(tmp, f"f{n}a.ppm"))
            p2 = _save_ppm(Image.new("RGB", (64, 64), c2), os.path.join(tmp, f"f{n}b.ppm"))
            flat_scores.append(_measure(tool, p1, [p2])[0][ALGO])

    def fig(c):
        f, axes = plt.subplots(1, 3 + len(flats), figsize=(12, 2.9),
                               gridspec_kw={"width_ratios": [1, 1, 1] + [0.8] * len(flats)})
        panels = [(base, "the example", None), (shuffled, "pixels shuffled", rows[0]),
                  (turned, "turned 90°", rows[1])]
        for ax, (im, title, row) in zip(axes, panels):
            ax.imshow(np.asarray(im))
            hide_axes(ax)
            ax.set_title(title, color=c["ink"], fontsize=9.5)
            if row:
                bits = round((1 - row["phash"]) * 64)
                ax.set_xlabel(f"ColorHash {FORMAT.format(row[ALGO])}\npHash {bits} of 64 bits "
                              "differ", color=c["ink"], fontsize=8.5)
        for ax, (n1, c1, n2, c2), score in zip(axes[3:], flats, flat_scores):
            ax.imshow(np.array([[c1, c2]], np.uint8), aspect="auto")
            ax.axvline(0.5, color=c["grid"], linewidth=0.8)
            hide_axes(ax)
            ax.set_title(f"{n1}\nagainst {n2}", color=c["ink"], fontsize=8.5)
            ax.set_xlabel(f"ColorHash {FORMAT.format(score)}", color=c["ink"], fontsize=8.5)
        f.tight_layout()
        return f

    save(fig, out, "blind-spots")
    lines = ["| Pair | R, G, B | ColorHash |", "|---|---|---|"]
    lines += [f"| example and its pixels shuffled | — | {FORMAT.format(rows[0][ALGO])} |",
              f"| example and the example turned 90° | — | {FORMAT.format(rows[1][ALGO])} |"]
    for (n1, c1, n2, c2), score in zip(flats, flat_scores):
        lines.append(f"| {n1} and {n2} | {c1} and {c2} | {FORMAT.format(score)} |")
    with open(os.path.join(out, "blind-spots.md"), "w") as f:
        f.write("\n".join(lines) + "\n")


# --8<-- [start:tint]
def _tint_one(args):
    """One original and its tinted copies through `site_stages measure`: ColorHash's
    score of each copy against the original."""
    tool, path, tmp, stem = args
    base = Image.open(path).convert("RGB")
    a = np.asarray(base).astype(int)
    ref = os.path.join(tmp, f"{stem}.ppm")
    base.save(ref)
    files = []
    for n, (_, shift) in enumerate(TINTS):
        files.append(os.path.join(tmp, f"{stem}-{n}.ppm"))
        Image.fromarray(np.clip(a + np.array(shift), 0, 255).astype(np.uint8)).save(files[-1])
    rows = _measure(tool, ref, files)
    for f in [ref] + files:
        os.remove(f)
    return [row[ALGO] for row in rows]


def measure_tints(tool):
    """{corpus: [[score per tint] per image]}, cached in build/site-cache/ like the
    corpora, under their key and the list of tints."""
    result = {}
    for key in ("synthetic", "photos"):
        paths = corpus.images(tool, key, os.path.join(corpus.CACHE, "work"))
        if not paths:
            result[key] = []
            continue
        ck = hashlib.sha256((corpus.cache_key(paths) + repr(TINTS)).encode()).hexdigest()
        cache = os.path.join(corpus.CACHE, f"color-tints-{key}.json")
        if os.path.exists(cache):
            with open(cache) as f:
                saved = json.load(f)
            if saved.get("key") == ck:
                result[key] = saved["scores"]
                continue
        print(f"render: color_hash: tinting {len(paths)} images")
        with tempfile.TemporaryDirectory() as tmp, \
                concurrent.futures.ProcessPoolExecutor() as pool:
            scores = list(pool.map(_tint_one, [(tool, p, tmp, f"{i:04d}")
                                               for i, p in enumerate(paths)]))
        with open(cache, "w") as f:
            json.dump({"key": ck, "scores": scores}, f)
        result[key] = scores
    return result
# --8<-- [end:tint]


def _tint_figure(tool, out):
    """Per corpus, how far each tint moves the histogram, against the threshold that
    accepts 95 % of that corpus's copies."""
    tints = measure_tints(tool)
    present = [k for k in ("photos", "synthetic") if tints[k]]
    data = {k: corpus.measure_corpus(tool, k) for k in present}
    thresholds = {k: separability(*copies_and_different(data[k], ALGO, lambda v: v),
                                  False)[1] for k in present}
    names = [n for n, _ in TINTS]

    def fig(c):
        colors = {"photos": c["accent"], "synthetic": c["accent2"]}
        f, ax = plt.subplots(figsize=(10, 3.6))
        xs = np.arange(len(names))
        for n, key in enumerate(present):
            v = np.array(tints[key])
            x = xs + (n - (len(present) - 1) / 2) * 0.22
            q = np.percentile(v, [10, 25, 50, 75], axis=0)
            ax.vlines(x, q[0], q[3], color=colors[key], linewidth=1.2, alpha=0.6)
            ax.vlines(x, q[1], q[3], color=colors[key], linewidth=6, alpha=0.35)
            ax.plot(x, q[2], linestyle="none", marker="o", markersize=5, color=colors[key],
                    label=data[key]["label"])
            ax.axhline(thresholds[key], color=colors[key], linewidth=1.1,
                       linestyle=(0, (4, 3)))
        ax.set_xticks(xs, names)
        ax.set_xlim(-0.5, len(names) - 0.5)
        ax.set_ylim(0, 1.02)
        ax.set_ylabel(METRIC, color=c["muted"], fontsize=9)
        ax.set_xlabel("levels added to the channel, of 255", color=c["muted"], fontsize=9)
        ax.legend(frameon=False, labelcolor=c["ink"], fontsize=9, loc="lower left")
        style_axes(ax, c)
        f.text(0.5, -0.04, "Dot: median; thick bar: the middle half; thin bar: from the 10th "
               "percentile. Dashed: the threshold that accepts 95 % of the corpus's copies, "
               "in its color.", ha="center", color=c["muted"], fontsize=9)
        f.tight_layout()
        return f

    save(fig, out, "tints")
    lines = ["| Corpus | Shift | Median | 10th percentile | Lowest | Below the copies' "
             "threshold |", "|---|---|---|---|---|---|"]
    for key in present:
        v = np.array(tints[key])
        for i, name in enumerate(names):
            col = v[:, i]
            lines.append(f"| {SHORT[key]} | {name} | {FORMAT.format(np.median(col))} | "
                         f"{FORMAT.format(np.percentile(col, 10))} | "
                         f"{FORMAT.format(col.min())} | "
                         f"{share(float(np.mean(col < thresholds[key])), len(col))} |")
    with open(os.path.join(out, "tints-table.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
