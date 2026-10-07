"""What every figure of the site shares: the two themes, the saving, the axes, the readers."""
import json
import os
import subprocess
import tempfile

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

# One accent for measured data, and ink colors for everything else, per theme. The accent
# is slot 1 of the site's chart palette, and accent2, for a second series beside it, is
# slot 2; the pair is validated against both of the site's surfaces (#ffffff and the
# dark theme's #0b0c0f, `surface`, which a label set over a line takes as its
# background). The inks are text colors, not series.
THEMES = {
    "light": {"accent": "#2a78d6", "accent2": "#eb6834", "ink": "#1f1f1e", "muted": "#6b6a66",
              "grid": "#e4e3df", "surface": "#ffffff", "off": "#eceae5", "on_text": "#ffffff"},
    "dark": {"accent": "#3987e5", "accent2": "#d95926", "ink": "#f2f2f0", "muted": "#a9a89f",
             "grid": "#3a3a37", "surface": "#0b0c0f", "off": "#2c2c2a", "on_text": "#ffffff"},
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


def hide_axes(ax):
    """An image panel: no ticks, no frame."""
    ax.set_xticks([])
    ax.set_yticks([])
    for s in ax.spines.values():
        s.set_visible(False)


def read_pnm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        w, h = map(int, f.readline().split())
        f.readline()
        data = np.frombuffer(f.read(), dtype=np.uint8)
    return data.reshape(h, w, 3) if magic == b"P6" else data.reshape(h, w)


def run_stages(tool, mode, image, images=()):
    """Runs `site_stages <mode> <image> <dir>` and returns ({json}, {image name: pixels})
    for the `<mode>.json` and the named PNM files it wrote."""
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([tool, mode, image, tmp], check=True)
        with open(os.path.join(tmp, f"{mode}.json")) as f:
            stages = json.load(f)
        pixels = {name: read_pnm(os.path.join(tmp, name)) for name in images}
    return stages, pixels


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
    hide_axes(ax)
    ax.set_aspect("equal")
