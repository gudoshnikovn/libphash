"""What every figure of the site shares: the two themes, the saving, the axes, and the
panels several pages draw (a strip of stages, a grid of values, a grid of bits)."""
import os

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
    """Writes name.light.svg and name.dark.svg; `fig` is a function of the theme.

    With SITE_PREVIEW set to a directory, every figure is also written there as
    <algo>/<name>.<theme>.png on its theme's page background, to look at before the
    page: an SVG is transparent, and an image viewer would show it on the wrong one.
    """
    os.makedirs(out_dir, exist_ok=True)
    preview = os.environ.get("SITE_PREVIEW")
    for theme, colors in THEMES.items():
        figure = fig(colors)
        figure.savefig(os.path.join(out_dir, f"{name}.{theme}.svg"), transparent=True,
                       bbox_inches="tight", metadata={"Date": None})
        if preview:
            d = os.path.join(preview, os.path.basename(os.path.normpath(out_dir)))
            os.makedirs(d, exist_ok=True)
            for ax in figure.axes:  # transparent in the SVG; matplotlib's white otherwise
                ax.set_facecolor(colors["surface"])
            figure.savefig(os.path.join(d, f"{name}.{theme}.png"), dpi=110,
                           bbox_inches="tight", facecolor=colors["surface"])
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


def draw_bits(ax, bits, c, numbers):
    """A square grid of bits: the accent where a bit is set; with `numbers`, 0 or 1 in
    each cell."""
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


def value_cells(ax, values, bits, c, size, fmt="{}", fontsize=8, changed=None,
                changed_width=2):
    """A size×size grid of values, row by row: each value in its cell (none when
    `fontsize` is 0), the cell in the accent where its bit is set, and the cells listed in
    `changed` outlined in the second accent."""
    for k, v in enumerate(values):
        i, j = divmod(k, size)
        ax.add_patch(plt.Rectangle((j - 0.46, i - 0.46), 0.92, 0.92, linewidth=0,
                                   color=c["accent"] if bits[k] else c["off"]))
        if fontsize:
            ax.text(j, i, fmt.format(v), ha="center", va="center", fontsize=fontsize,
                    color=c["on_text"] if bits[k] else c["ink"])
        if changed is not None and changed[k]:
            ax.add_patch(plt.Rectangle((j - 0.5, i - 0.5), 1, 1, fill=False,
                                       linewidth=changed_width, edgecolor=c["accent2"]))
    ax.set_xlim(-0.5, size - 0.5)
    ax.set_ylim(size - 0.5, -0.5)
    ax.set_aspect("equal")
    hide_axes(ax)


def stage_strip(c, figsize, panels):
    """The strip at the top of "The steps": one panel per stage, `panels` being
    [(title, draw(ax))], each drawn and then titled. Returns the figure."""
    fig, axes = plt.subplots(1, len(panels), figsize=figsize)
    for ax, (_, draw) in zip(axes, panels):
        draw(ax)
    for ax, (title, _) in zip(axes, panels):
        hide_axes(ax)
        ax.set_title(title, color=c["ink"], fontsize=10)
    return fig


def image_panel(image):
    """A stage_strip() panel showing a decoded (RGB) image."""
    return lambda ax: ax.imshow(image)


def gray_panel(image, grid=False):
    """A stage_strip() panel showing a grayscale image on the full 0–255 scale; a `grid`
    (a reduction to a few cells) is drawn with sharp cells."""
    if grid:
        return lambda ax: ax.imshow(image, cmap="gray", vmin=0, vmax=255,
                                    interpolation="nearest")
    return lambda ax: ax.imshow(image, cmap="gray", vmin=0, vmax=255)


def hash_footer(fig, c, hexhash):
    """The hash under a strip of stages, as ph_compute_*() returned it."""
    fig.text(0.5, -0.02, f"hash = {hexhash}", ha="center", color=c["ink"], family="monospace",
             fontsize=11)
