#!/usr/bin/env python3
"""Draws the documentation site's figures from what tools/site/stages.c measures.

Every figure is drawn twice, for the light and the dark theme (`name.light.svg`,
`name.dark.svg`; a page shows them with `#only-light` / `#only-dark`), and every measured
chart also gets a Markdown table of the same numbers, which a page includes under the
figure so the values are readable without the picture.

Usage: tools/site/render.py --tool build/release/site_stages --image tests/data/photo.jpeg
                            --out docs/assets/generated
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image, ImageEnhance, ImageFilter  # noqa: E402

# One accent for measured data, and ink colors for everything else, per theme. The accent
# is slot 1 of the site's chart palette (validated against both surfaces); the inks are
# text colors, not series.
THEMES = {
    "light": {"accent": "#2a78d6", "ink": "#1f1f1e", "muted": "#6b6a66",
              "grid": "#e4e3df", "off": "#eceae5", "on_text": "#ffffff"},
    "dark": {"accent": "#3987e5", "ink": "#f2f2f0", "muted": "#a9a89f",
             "grid": "#3a3a37", "off": "#2c2c2a", "on_text": "#ffffff"},
}

plt.rcParams.update({
    "font.family": "sans-serif",
    "font.size": 10,
    "svg.fonttype": "none",       # text stays text: it takes the page's font and scales
    "svg.hashsalt": "libphash",   # stable element ids, so a rebuild changes no bytes
    "axes.spines.top": False,
    "axes.spines.right": False,
})


def save(fig, out_dir, name):
    """Writes name.light.svg and name.dark.svg; `fig` is a function of the theme."""
    os.makedirs(out_dir, exist_ok=True)
    for theme, colors in THEMES.items():
        figure = fig(colors)
        figure.savefig(os.path.join(out_dir, f"{name}.{theme}.svg"), transparent=True,
                       bbox_inches="tight", metadata={"Date": None})
        plt.close(figure)


def style_axes(ax, c):
    for side in ("left", "bottom"):
        ax.spines[side].set_color(c["grid"])
    ax.tick_params(colors=c["muted"], labelsize=9)
    ax.grid(True, color=c["grid"], linewidth=0.6)
    ax.set_axisbelow(True)


def read_pnm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        w, h = map(int, f.readline().split())
        f.readline()
        data = np.frombuffer(f.read(), dtype=np.uint8)
    return data.reshape(h, w, 3) if magic == b"P6" else data.reshape(h, w)


# --- aHash -------------------------------------------------------------------------------

def ahash_figures(tool, image, out_dir):
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([tool, "ahash", image, tmp], check=True)
        with open(os.path.join(tmp, "ahash.json")) as f:
            st = json.load(f)
        original = read_pnm(os.path.join(tmp, "original.ppm"))
        gray = read_pnm(os.path.join(tmp, "gray.pgm"))
    n = st["grid_size"]
    grid = np.array(st["grid"]).reshape(n, n)
    bits = np.array(st["bits"]).reshape(n, n)
    mean = st["mean"]
    out = os.path.join(out_dir, "ahash")

    def pipeline(c):
        fig, axes = plt.subplots(1, 4, figsize=(10, 2.9))
        titles = ["Decoded image", "Grayscale", f"Reduced to {n}×{n}", "Bits"]
        axes[0].imshow(original)
        axes[1].imshow(gray, cmap="gray", vmin=0, vmax=255)
        axes[2].imshow(grid, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
        draw_bits(axes[3], bits, c, numbers=False)
        for ax, t in zip(axes, titles):
            ax.set_xticks([])
            ax.set_yticks([])
            for s in ax.spines.values():
                s.set_visible(False)
            ax.set_title(t, color=c["ink"], fontsize=10)
        fig.text(0.5, -0.02, f"hash = {st['hash']}", ha="center", color=c["ink"],
                 family="monospace", fontsize=11)
        return fig

    def grid_values(c):
        fig, ax = plt.subplots(figsize=(4.6, 4.6))
        ax.imshow(grid, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
        for (i, j), v in np.ndenumerate(grid):
            above = v >= mean
            ax.text(j, i, str(v), ha="center", va="center", fontsize=9,
                    color="#000000" if v > 140 else "#ffffff",
                    fontweight="bold" if above else "normal")
        ax.set_xticks([])
        ax.set_yticks([])
        for s in ax.spines.values():
            s.set_visible(False)
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
        ax.set_xticks([])
        ax.set_yticks([])
        for s in ax.spines.values():
            s.set_visible(False)
        ax.set_title("bit number of each cell (63 = most significant)", color=c["ink"],
                     fontsize=10)
        return fig

    save(pipeline, out, "pipeline")
    save(grid_values, out, "grid")
    save(bit_grid, out, "bits")
    save(bit_order, out, "bit-order")
    return st


def draw_bits(ax, bits, c, numbers):
    n = bits.shape[0]
    ax.set_xlim(-0.5, n - 0.5)
    ax.set_ylim(n - 0.5, -0.5)
    for (i, j), b in np.ndenumerate(bits):
        ax.add_patch(plt.Rectangle((j - 0.46, i - 0.46), 0.92, 0.92,
                                   color=c["accent"] if b else c["off"], linewidth=0))
        if numbers:
            ax.text(j, i, str(b), ha="center", va="center", fontsize=9,
                    color=c["on_text"] if b else c["muted"])
    ax.set_xticks([])
    ax.set_yticks([])
    for s in ax.spines.values():
        s.set_visible(False)
    ax.set_aspect("equal")


# --- Robustness ----------------------------------------------------------------------------

def transforms():
    """(name, x-axis label, [(strength, PIL image -> (image, file extension))])."""
    def jpeg(q):
        return lambda im: (im, ("jpg", q))

    def scale(s):
        return lambda im: (im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))),
                                     Image.LANCZOS), ("png", None))

    def rotate(deg):
        def f(im):
            fill = tuple(int(v) for v in np.asarray(im).reshape(-1, 3).mean(axis=0))
            return im.rotate(deg, resample=Image.BICUBIC, fillcolor=fill), ("png", None)
        return f

    def brightness(k):
        return lambda im: (ImageEnhance.Brightness(im).enhance(k), ("png", None))

    def contrast(k):
        return lambda im: (ImageEnhance.Contrast(im).enhance(k), ("png", None))

    def gamma(g):
        def f(im):
            a = (np.asarray(im).astype(np.float64) / 255.0) ** g
            return Image.fromarray((a * 255.0).round().astype(np.uint8)), ("png", None)
        return f

    def blur(r):
        return lambda im: (im.filter(ImageFilter.GaussianBlur(r)), ("png", None))

    def noise(sigma):
        def f(im):
            rng = np.random.default_rng(1)
            a = np.asarray(im).astype(np.float64) + rng.normal(0, sigma, (im.height, im.width, 3))
            return Image.fromarray(np.clip(a, 0, 255).round().astype(np.uint8)), ("png", None)
        return f

    def crop(p):
        def f(im):
            dx, dy = round(im.width * p / 2), round(im.height * p / 2)
            return im.crop((dx, dy, im.width - dx, im.height - dy)), ("png", None)
        return f

    return [
        ("JPEG quality", "quality", [(q, jpeg(q)) for q in (95, 75, 50, 30, 15, 5)]),
        ("Downscale", "scale", [(s, scale(s)) for s in (0.75, 0.5, 0.25, 0.125)]),
        ("Rotation", "degrees", [(d, rotate(d)) for d in (1, 2, 5, 10, 20, 45)]),
        ("Brightness", "factor", [(k, brightness(k)) for k in (0.5, 0.7, 0.85, 1.15, 1.3, 1.5)]),
        ("Contrast", "factor", [(k, contrast(k)) for k in (0.5, 0.7, 0.85, 1.15, 1.3, 1.5)]),
        ("Gamma", "exponent", [(g, gamma(g)) for g in (0.5, 0.7, 0.85, 1.2, 1.5, 2.0)]),
        ("Gaussian blur", "radius, px", [(r, blur(r)) for r in (0.5, 1, 2, 4, 8)]),
        ("Noise", "σ, gray levels", [(s, noise(s)) for s in (2, 5, 10, 20, 40)]),
        ("Crop", "border removed, %", [(p, crop(p / 100)) for p in (5, 10, 20, 30)]),
    ]


def measure_robustness(tool, image):
    """{algorithm: {transform: [(strength, value)]}} for one image, by stages.c."""
    base = Image.open(image).convert("RGB")
    with tempfile.TemporaryDirectory() as tmp:
        ref = os.path.join(tmp, "reference.png")
        base.save(ref)
        files, keys = [], []
        for name, _, steps in transforms():
            for strength, op in steps:
                im, (ext, q) = op(base)
                path = os.path.join(tmp, f"{len(files)}.{ext}")
                if ext == "jpg":
                    im.save(path, quality=q)
                else:
                    im.save(path)
                files.append(path)
                keys.append((name, strength))
        out = subprocess.run([tool, "measure", ref, *files], check=True, capture_output=True,
                             text=True).stdout
    rows = [json.loads(line) for line in out.splitlines()]
    result = {}
    for (name, strength), row in zip(keys, rows):
        for algo, value in row.items():
            if algo != "file":
                result.setdefault(algo, {}).setdefault(name, []).append((strength, value))
    return result


def robustness_figure(data, algo, out_dir, bits_total, caption):
    """Small multiples, one panel per transform: bits that differ from the original."""
    specs = transforms()

    def fig(c):
        figure, axes = plt.subplots(3, 3, figsize=(10, 7.2), sharey=True)
        for ax, (name, xlabel, _) in zip(axes.flat, specs):
            pts = [(s, (1.0 - v) * bits_total) for s, v in data[algo][name] if v is not None]
            xs = list(range(len(pts)))
            ax.plot(xs, [p[1] for p in pts], color=c["accent"], linewidth=2, marker="o",
                    markersize=4.5, solid_capstyle="round")
            ax.set_xticks(xs, [f"{p[0]:g}" for p in pts])
            ax.set_ylim(0, bits_total / 2)
            ax.set_title(name, color=c["ink"], fontsize=10)
            ax.set_xlabel(xlabel, color=c["muted"], fontsize=9)
            style_axes(ax, c)
        for ax in axes[:, 0]:
            ax.set_ylabel("bits that differ", color=c["muted"], fontsize=9)
        figure.text(0.5, -0.03, caption, ha="center", color=c["muted"], fontsize=9)
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out_dir, algo), "robustness")


def robustness_table(data, algo, out_dir, bits_total):
    lines = ["| Transform | Strength | Bits that differ |", "|---|---|---|"]
    for name, _, _ in transforms():
        for strength, value in data[algo][name]:
            cell = "—" if value is None else f"{round((1.0 - value) * bits_total)}"
            lines.append(f"| {name} | {strength:g} | {cell} |")
    path = os.path.join(out_dir, algo, "robustness-table.md")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--tool", required=True)
    p.add_argument("--image", required=True)
    p.add_argument("--out", required=True)
    args = p.parse_args()

    ahash_figures(args.tool, args.image, args.out)
    data = measure_robustness(args.tool, args.image)
    caption = "One image, the example above; each transform applied alone to the original."
    robustness_figure(data, "ahash", args.out, 64, caption)
    robustness_table(data, "ahash", args.out, 64)
    print(f"render: figures in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
