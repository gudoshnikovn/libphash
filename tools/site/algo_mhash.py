"""The figures of docs/theory/mhash.md, from `site_stages mhash`."""
import concurrent.futures
import json
import os
import subprocess
import tempfile

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LinearSegmentedColormap, TwoSlopeNorm

import corpus
from common import hide_axes, read_pnm, save, style_axes
from corpus_charts import COPY_STRENGTHS
from timing import measure_timing, ms

ALGO = "mhash"
BITS = 576  # the robustness chart's scale: bits of the digest


def _stages(tool, image):
    """`site_stages mhash` on `image`: (json, {name: pixels}, response as an n×n array)."""
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([tool, ALGO, image, tmp], check=True)
        with open(os.path.join(tmp, "mhash.json")) as f:
            st = json.load(f)
        px = {n: read_pnm(os.path.join(tmp, n)) for n in
              ("original.ppm", "gray.pgm", "blurred.pgm", "resized.pgm", "equalized.pgm")}
        n = st["default"]["size"]
        response = np.fromfile(os.path.join(tmp, "response.f32"), dtype=np.float32)
    return st, px, response.reshape(n, n)


def _bits(digest_hex):
    """The digest's 576 bits in order, MSB of byte 0 first, as the library packs them."""
    return np.unpackbits(np.frombuffer(bytes.fromhex(digest_hex), np.uint8))


def _apart(a, b):
    return int((_bits(a) != _bits(b)).sum())


def _diverging(c):
    """Negative in the second accent, zero on the page's surface, positive in the accent."""
    return LinearSegmentedColormap.from_list("signed", [c["accent2"], c["surface"], c["accent"]])


def _signed(ax, values, c, limit=None):
    limit = limit or float(np.percentile(np.abs(values), 99.5)) or 1.0
    return ax.imshow(values, cmap=_diverging(c), norm=TwoSlopeNorm(0, -limit, limit),
                     interpolation="nearest")


def _bit_mosaic(bits, grid=8, window=3):
    """The bits laid out as their windows: 8×8 windows of 3×3, one cell apart."""
    side = grid * (window + 1) - 1
    out = np.full((side, side), np.nan)
    for w in range(grid * grid):
        wy, wx = divmod(w, grid)
        cells = bits[w * window * window:(w + 1) * window * window].reshape(window, window)
        out[wy * (window + 1):wy * (window + 1) + window,
            wx * (window + 1):wx * (window + 1) + window] = cells
    return out


def _draw_mosaic(ax, bits, c):
    m = _bit_mosaic(bits)
    for (i, j), b in np.ndenumerate(m):
        if not np.isnan(b):
            ax.add_patch(plt.Rectangle((j - 0.45, i - 0.45), 0.9, 0.9, linewidth=0,
                                       color=c["accent"] if b else c["off"]))
    ax.set_xlim(-0.5, m.shape[1] - 0.5)
    ax.set_ylim(m.shape[0] - 0.5, -0.5)
    ax.set_aspect("equal")
    hide_axes(ax)


def _windows(ax, st, c, labels=False):
    g, w, s = st["grid"], st["window"], st["stride"]
    for k in range(64):
        wy, wx = divmod(k, 8)
        ax.add_patch(plt.Rectangle((wx * s - 0.5, wy * s - 0.5), w, w, fill=False,
                                   linewidth=1, edgecolor=c["ink"]))
        if labels:
            ax.text(wx * s + 1, wy * s + 1, str(k), ha="center", va="center", fontsize=7,
                    color=c["ink"], bbox={"facecolor": c["surface"], "edgecolor": "none",
                                          "pad": 0.5, "alpha": 0.8})
    ax.set_xlim(-0.5, g - 0.5)
    ax.set_ylim(g - 0.5, -0.5)


def figures(tool, image, out_dir):
    st, px, response = _stages(tool, image)
    d = st["default"]
    g = st["grid"]
    n, block, side = d["size"], d["block"], d["side"]
    blocks = np.array(d["blocks"]).reshape(g, g)
    kernel = np.array(d["kernel"]).reshape(side, side)
    bits = _bits(d["digest"])
    out = os.path.join(out_dir, ALGO)

    def pipeline(c):
        fig, axes = plt.subplots(1, 6, figsize=(13, 2.7))
        titles = ["Decoded image", f"Blurred, {n}×{n}", "Equalized", "Kernel response",
                  f"{g}×{g} block sums", "576 bits"]
        axes[0].imshow(px["original.ppm"])
        axes[1].imshow(px["resized.pgm"], cmap="gray", vmin=0, vmax=255)
        axes[2].imshow(px["equalized.pgm"], cmap="gray", vmin=0, vmax=255)
        _signed(axes[3], response, c)
        _signed(axes[4], blocks, c)
        _draw_mosaic(axes[5], bits, c)
        for ax, t in zip(axes, titles):
            hide_axes(ax)
            ax.set_title(t, color=c["ink"], fontsize=10)
        return fig

    def kernel_fig(c):
        fig, (a, b) = plt.subplots(1, 2, figsize=(9, 3.6), gridspec_kw={"width_ratios": [1, 1.6]})
        _signed(a, kernel, c, limit=float(np.abs(kernel).max()))
        hide_axes(a)
        a.set_title(f"{side}×{side} kernel", color=c["ink"], fontsize=10)
        half = side // 2
        scale = d["alpha"] ** -d["level"]
        xs = np.arange(-half, half + 1)
        fine = np.linspace(-half, half, 400)
        A = (scale * fine) ** 2
        b.plot(fine, (2 - A) * np.exp(-A / 2), color=c["muted"], linewidth=1)
        b.plot(xs, kernel[half], color=c["accent"], linestyle="none", marker="o", markersize=4.5)
        b.axhline(0, color=c["grid"], linewidth=0.8)
        b.set_xlabel("samples from the center", color=c["muted"], fontsize=9)
        b.set_ylabel("weight", color=c["muted"], fontsize=9)
        b.set_title("the middle row: (2 − A)·exp(−A/2)", color=c["ink"], fontsize=10)
        style_axes(b, c)
        return fig

    def equalize(c):
        fig, axes = plt.subplots(2, 2, figsize=(8, 6.2), gridspec_kw={"height_ratios": [3, 1.2]})
        for col, (name, title) in enumerate((("resized.pgm", "before"),
                                             ("equalized.pgm", "after equalizing"))):
            im = px[name]
            axes[0, col].imshow(im, cmap="gray", vmin=0, vmax=255)
            hide_axes(axes[0, col])
            axes[0, col].set_title(title, color=c["ink"], fontsize=10)
            h = np.bincount(im.ravel(), minlength=256)
            ax = axes[1, col]
            ax.bar(np.arange(256), h / h.sum() * 100, width=1, color=c["muted"], linewidth=0)
            ax2 = ax.twinx()
            ax2.plot(np.arange(256), np.cumsum(h) / h.sum() * 100, color=c["accent"],
                     linewidth=1.8)
            ax2.set_ylim(0, 100)
            ax2.set_yticks([])
            for sp in ax2.spines.values():
                sp.set_visible(False)
            ax.set_xlim(0, 255)
            ax.set_yticks([])
            ax.set_xlabel("gray level", color=c["muted"], fontsize=9)
            style_axes(ax, c)
        axes[1, 0].set_ylabel("share", color=c["muted"], fontsize=9)
        fig.text(0.5, -0.01, "Bars: histogram of gray levels. Blue line: their cumulative share, "
                 "which equalizing makes a straight line.", ha="center", color=c["muted"],
                 fontsize=9)
        fig.tight_layout()
        return fig

    def response_grid(c):
        fig, ax = plt.subplots(figsize=(5.6, 5.6))
        _signed(ax, response, c)
        for k in range(g + 1):
            ax.axhline(k * block - 0.5, color=c["ink"], linewidth=0.3, alpha=0.6)
            ax.axvline(k * block - 0.5, color=c["ink"], linewidth=0.3, alpha=0.6)
        covered = g * block
        ax.axvspan(covered - 0.5, n - 0.5, color=c["muted"], alpha=0.5, linewidth=0)
        ax.axhspan(covered - 0.5, n - 0.5, color=c["muted"], alpha=0.5, linewidth=0)
        hide_axes(ax)
        ax.set_title(f"response, {g}×{g} blocks of {block}×{block}; shaded: outside every block",
                     color=c["ink"], fontsize=9.5)
        return fig

    def windows(c):
        fig, ax = plt.subplots(figsize=(5.6, 5.6))
        _signed(ax, blocks, c)
        _windows(ax, st, c, labels=True)
        hide_axes(ax)
        ax.set_title("block sums; outlined: the 64 windows, numbered", color=c["ink"],
                     fontsize=10)
        return fig

    # The window shown in detail: the first whose bits are neither all set nor all clear,
    # scanning from the middle of the image outwards.
    order = sorted(range(64), key=lambda k: abs(k // 8 - 3.5) + abs(k % 8 - 3.5))
    wsel = next(k for k in order if 0 < bits[9 * k:9 * k + 9].sum() < 9)

    def window(c):
        wy, wx = divmod(wsel, 8)
        s = st["stride"]
        vals = blocks[wy * s:wy * s + 3, wx * s:wx * s + 3]
        mean = np.float32(vals.astype(np.float32).sum()) / np.float32(9)
        wb = bits[9 * wsel:9 * wsel + 9].reshape(3, 3)
        fig, ax = plt.subplots(figsize=(4.6, 4.4))
        for (i, j), v in np.ndenumerate(vals):
            on = wb[i, j]
            ax.add_patch(plt.Rectangle((j - 0.47, i - 0.47), 0.94, 0.94, linewidth=0,
                                       color=c["accent"] if on else c["off"]))
            ax.text(j, i - 0.1, f"{v:.0f}", ha="center", va="center", fontsize=11,
                    color=c["on_text"] if on else c["ink"])
            ax.text(j, i + 0.25, f"bit {3 * i + j}", ha="center", va="center", fontsize=8,
                    color=c["on_text"] if on else c["muted"])
        ax.set_xlim(-0.5, 2.5)
        ax.set_ylim(2.5, -0.5)
        ax.set_aspect("equal")
        hide_axes(ax)
        ax.set_title(f"window {wsel}: mean {mean:.0f}; blue: above it, bit set",
                     color=c["ink"], fontsize=10)
        return fig

    def bit_order(c):
        fig = plt.figure(figsize=(9, 3.2))
        ax = fig.add_axes([0.0, 0.08, 1.0, 0.8])
        cells = 18
        for i in range(cells):
            w, k = divmod(i, 9)
            ax.add_patch(plt.Rectangle((i + 0.04, 0.04), 0.92, 0.92, linewidth=0,
                                       color=c["accent"] if w == 0 else c["off"]))
            ax.text(i + 0.5, 0.62, str(i), ha="center", va="center", fontsize=9,
                    color=c["on_text"] if w == 0 else c["ink"])
            ax.text(i + 0.5, 0.3, f"w{w}·{k}", ha="center", va="center", fontsize=7.5,
                    color=c["on_text"] if w == 0 else c["muted"])
        for byte in range(3):
            x0, x1 = byte * 8, min(byte * 8 + 8, cells)
            ax.annotate("", (x0 + 0.05, -0.15), (x1 - 0.05, -0.15),
                        arrowprops={"arrowstyle": "|-|", "color": c["muted"], "linewidth": 1})
            ax.text((x0 + x1) / 2, -0.42, f"byte {byte}" + (", bit 7 first" if byte == 0 else ""),
                    ha="center", va="center", fontsize=9, color=c["ink"])
        ax.text(cells + 0.3, 0.5, "…  576 bits, 72 bytes", va="center", fontsize=9,
                color=c["muted"])
        ax.set_xlim(-0.2, cells + 4)
        ax.set_ylim(-0.7, 1.1)
        hide_axes(ax)
        ax.set_title("bit number; below it, window·cell (cell = 3 × row + column)",
                     color=c["ink"], fontsize=10)
        return fig

    levels = [r for r in st["levels"]]
    sizes = sorted(st["sizes"], key=lambda r: r["size"])
    sizes = [r for i, r in enumerate(sizes) if i == 0 or r["size"] != sizes[i - 1]["size"]]
    levels = sorted({r["level"]: r for r in levels}.values(), key=lambda r: r["level"])

    def scales(c):
        fig, axes = plt.subplots(2, len(levels), figsize=(9.5, 6.4))
        big = max(r["side"] for r in levels)
        for col, r in enumerate(levels):
            k = np.array(r["kernel"]).reshape(r["side"], r["side"])
            pad = (big - r["side"]) // 2
            canvas = np.zeros((big, big))
            canvas[pad:pad + r["side"], pad:pad + r["side"]] = k
            _signed(axes[0, col], canvas, c, limit=2.0)
            axes[0, col].add_patch(plt.Rectangle((pad - 0.5, pad - 0.5), r["side"], r["side"],
                                                 fill=False, edgecolor=c["muted"], linewidth=0.8))
            hide_axes(axes[0, col])
            apart = _apart(r["digest"], d["digest"])
            axes[0, col].set_title(
                f"level {r['level']:g}: {r['side']}×{r['side']}"
                + (" (default)" if r["level"] == d["level"] else f", {apart} bits apart"),
                color=c["ink"], fontsize=10)
            _signed(axes[1, col], np.array(r["blocks"]).reshape(g, g), c)
            hide_axes(axes[1, col])
        axes[0, 0].set_ylabel("kernel, to scale", color=c["muted"], fontsize=9)
        axes[1, 0].set_ylabel("block sums", color=c["muted"], fontsize=9)
        fig.tight_layout()
        return fig

    def sizes_fig(c):
        fig, axes = plt.subplots(1, len(sizes), figsize=(9.5, 3.6))
        for ax, r in zip(axes, sizes):
            _signed(ax, np.array(r["blocks"]).reshape(g, g), c)
            hide_axes(ax)
            note = (" (default)" if r["size"] == n else
                    f", {_apart(r['digest'], d['digest'])} bits apart")
            ax.set_title(f"size {r['size']}: blocks of {r['block']}{note}", color=c["ink"],
                         fontsize=9.5)
        return fig

    save(pipeline, out, "pipeline")
    save(kernel_fig, out, "kernel")
    save(equalize, out, "equalize")
    save(response_grid, out, "response")
    save(windows, out, "windows")
    save(window, out, "window")
    save(bit_order, out, "bit-order")
    save(scales, out, "levels")
    save(sizes_fig, out, "sizes")
    _cost_figure(tool, out)
    _patch_figure(tool, out)
    _write_direct(tool, st, out)
    _write_facts(st, kernel, out)


def _write_facts(st, kernel, out):
    """The numbers the text quotes, as sentences the page includes."""
    d = st["default"]
    with open(os.path.join(out, "load-grayscale.md"), "w") as f:
        k = _apart(d["digest"], st["digest_load_grayscale"])
        f.write("The example photograph gives the same digest both ways.\n" if k == 0 else
                f"On the example photograph the two digests are {k} of 576 bits apart.\n")
    with open(os.path.join(out, "kernel-sum.md"), "w") as f:
        f.write(f"The {d['side']}×{d['side']} kernel at the defaults sums to "
                f"{kernel.sum():.3f}, against a weight of {kernel.max():g} at its center.\n")


def _cost_figure(tool, out):
    """Time against `size`, on both timed images, from the build machine's timing."""
    data = measure_timing(tool)
    sizes = sorted(int(k.rsplit("_", 1)[1]) for k in data["small"]["cases"]
                   if k.startswith("mhash_size_"))
    series = [(data[key], name) for key, name in (("small", "accent"), ("large", "accent2"))]

    def fig(c):
        f, ax = plt.subplots(figsize=(7, 3.8))
        for entry, color in series:
            t = [entry["cases"][f"mhash_size_{s}"]["min_ms"] for s in sizes]
            ax.plot(sizes, t, color=c[color], linewidth=2, marker="o", markersize=4.5)
            # At the left end, where the two lines are furthest apart.
            ax.annotate(f"{entry['width']}×{entry['height']} image", (sizes[0], t[0]),
                        xytext=(0, 14), textcoords="offset points", ha="left", fontsize=9,
                        color=c["ink"], bbox={"facecolor": c["surface"], "edgecolor": "none",
                                              "pad": 1})
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xticks(sizes, [str(s) for s in sizes])
        ticks = [v for v in (0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500)
                 if ax.get_ylim()[0] <= v <= ax.get_ylim()[1]]
        ax.set_yticks(ticks, [f"{v:g}" for v in ticks])
        ax.minorticks_off()
        ax.set_xlabel("size", color=c["muted"], fontsize=9)
        ax.set_ylabel("ms, logarithmic scale", color=c["muted"], fontsize=9)
        style_axes(ax, c)
        return f

    save(fig, out, "cost-size")
    rows = ["| size | " + " | ".join(f"{e['width']}×{e['height']}" for e, _ in series) + " |",
            "|---|---|---|"]
    for s in sizes:
        rows.append(f"| {s} | " + " | ".join(f"{ms(e['cases'][f'mhash_size_{s}']['min_ms'])} ms"
                                             for e, _ in series) + " |")
    with open(os.path.join(out, "cost-size.md"), "w") as f:
        f.write("\n".join(rows) + "\n")


def _patch_figure(tool, out):
    """mHash's median distance after each copy edit and after each patch, per corpus: the
    patches against the edits a copy goes through."""
    datasets = [(k, corpus.measure_corpus(tool, k)) for k in ("synthetic", "photos")]
    datasets = [(k, d) for k, d in datasets if d["n"]]

    def rows(data):
        r = []
        for name, points in data["robust"][ALGO].items():
            for strength, values in points:
                if strength == COPY_STRENGTHS[name]:
                    r.append((f"{name} {strength:g}", "copy", values))
        for strength, values in data["edits"][ALGO]["Local patch"]:
            r.append((f"patch, {strength:g}% of the frame", "patch", values))
        return [(lab, kind, np.median([(1 - v) * BITS for v in vals if v is not None]))
                for lab, kind, vals in r]

    def fig(c):
        f, axes = plt.subplots(1, len(datasets), figsize=(10, 4.6), sharex=True)
        for ax, (key, data) in zip(np.atleast_1d(axes), datasets):
            rs = sorted(rows(data), key=lambda r: r[2])
            y = np.arange(len(rs))
            ax.barh(y, [r[2] for r in rs], height=0.62, linewidth=0,
                    color=[c["accent2"] if r[1] == "patch" else c["accent"] for r in rs])
            ax.set_yticks(y, [r[0] for r in rs], fontsize=8.5)
            ax.set_xlabel("median bits that differ, of 576", color=c["muted"], fontsize=9)
            ax.set_title(data["label"], color=c["ink"], fontsize=9.5)
            style_axes(ax, c)
            ax.tick_params(axis="y", colors=c["ink"])
        f.text(0.5, -0.02, "Blue: the edits a copy goes through, at the strength that makes a "
               "copy. Orange: a patch.", ha="center", color=c["muted"], fontsize=9)
        f.tight_layout()
        return f

    save(fig, out, "patch-vs-copies")


def _direct_one(args):
    tool, paths = args
    res = subprocess.run([tool, "mhash-direct", *paths], check=True, capture_output=True,
                         text=True).stdout
    return [json.loads(line) for line in res.splitlines()]


def _write_direct(tool, st, out):
    """How often evaluating the definition pixel by pixel, in double and in float, gives
    another digest than the folded block sums, over both corpora; cached like them."""
    lines = []
    for key in ("synthetic", "photos"):
        work = os.path.join(corpus.CACHE, "work")
        paths = corpus.images(tool, key, work)
        if not paths:
            continue
        cache = os.path.join(corpus.CACHE, f"mhash-direct-{key}.json")
        ck = corpus.cache_key(paths)
        rows = None
        if os.path.exists(cache):
            with open(cache) as f:
                saved = json.load(f)
            rows = saved["rows"] if saved.get("key") == ck else None
        if rows is None:
            chunks = [(tool, paths[i:i + 8]) for i in range(0, len(paths), 8)]
            with concurrent.futures.ProcessPoolExecutor() as pool:
                rows = [r for part in pool.map(_direct_one, chunks) for r in part]
            with open(cache, "w") as f:
                json.dump({"key": ck, "rows": rows}, f)
        label = corpus.LABELS[key].format(n=len(paths))
        for kind in ("double", "float"):
            diff = [r[kind] for r in rows if r[kind]]
            text = ("the same digest on every image" if not diff else
                    f"another digest on {len(diff)} of {len(rows)} images, "
                    f"{min(diff)}–{max(diff)} bits apart" if len(diff) > 1 else
                    f"another digest on 1 of {len(rows)} images, {diff[0]} "
                    f"bit{'' if diff[0] == 1 else 's'} apart")
            lines.append(f"| {label} | {kind} | {text} |")
    d = st["default"]
    example = [f"| the example photograph | {kind} | " +
               ("the same digest" if _apart(d["digest"], st[f"digest_direct_{kind}"]) == 0 else
                f"{_apart(d['digest'], st[f'digest_direct_{kind}'])} bits apart") + " |"
               for kind in ("double", "float")]
    with open(os.path.join(out, "direct.md"), "w") as f:
        f.write("| Images | Response computed in | Against the folded block sums |\n"
                "|---|---|---|\n" + "\n".join(example + lines) + "\n")
