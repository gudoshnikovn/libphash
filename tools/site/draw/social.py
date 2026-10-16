"""The preview image of a page: the 1200×630 PNG that a link to the page shows in a chat,
a forum or a search result (its og:image and twitter:image, overrides/main.html).

A card is the page's title above one of the page's own figures, drawn by the function that
draws it on the page, in the light theme: an algorithm page shows its steps ("pipeline"),
the front page its figure of copies and their hashes. The title is read from the page's
front matter, so the card and the search result say the same thing. Every other page
shows the front page's card (`SITE`).
"""
import io
import os

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

from draw.style import THEMES

# The card every page without one of its own shows, under docs/assets/generated/social/.
SITE = "site"
WIDTH, HEIGHT, DPI = 1200, 630, 100
# The title's top and the bottom of its two lines, in pixels from the card's top edge.
TITLE_TOP, TITLE_BOTTOM = 80, 180
DOCS = os.path.join(os.path.dirname(__file__), "..", "..", "..", "docs")


def front_matter(page):
    """The `key: value` lines of a page's front matter (docs/<page>), as a dict."""
    with open(os.path.join(DOCS, page), encoding="utf-8") as f:
        lines = f.read().split("\n")
    if lines[0] != "---":
        raise SystemExit(f"social: docs/{page} has no front matter")
    meta = {}
    for line in lines[1:lines.index("---", 1)]:
        key, sep, value = line.partition(":")
        if sep and value.strip():
            meta[key.strip()] = value.strip().strip("\"'")
    return meta


def _figure_pixels(fig, c, width):
    """A figure of the page drawn on the card's background, as RGB pixels about `width`
    wide."""
    figure = fig(c)
    for ax in figure.axes:
        ax.set_facecolor(c["surface"])
    buf = io.BytesIO()
    w_in = figure.get_size_inches()[0]
    figure.savefig(buf, format="png", dpi=width / w_in, bbox_inches="tight",
                   facecolor=c["surface"], metadata={"Software": None})
    plt.close(figure)
    buf.seek(0)
    return np.asarray(Image.open(buf).convert("RGB"))


def _title(card_fig, title, c):
    """The title under the site's name: on one line when it fits the card's width, else on
    two lines of about equal width, broken between words, and smaller only when even those
    do not fit."""
    words = title.split()
    renderer = card_fig.canvas.get_renderer()
    for size in (30, 27, 24, 21):
        text = card_fig.text(48 / WIDTH, 1 - TITLE_TOP / HEIGHT, title, color=c["ink"],
                             fontsize=size, fontweight="bold", va="top", linespacing=1.15)
        if text.get_window_extent(renderer).width <= WIDTH - 96:
            return
        best = None
        for k in range(1, len(words)):
            lines = [" ".join(words[:k]), " ".join(words[k:])]
            widths = []
            for line in lines:
                text.set_text(line)
                widths.append(text.get_window_extent(renderer).width)
            if max(widths) <= WIDTH - 96 and (best is None or max(widths) < best[0]):
                best = (max(widths), "\n".join(lines))
        if best:
            text.set_text(best[1])
            return
        text.remove()
    raise SystemExit(f"social: the title does not fit a card: {title}")


def card(fig, out_dir, name, page):
    """Writes out_dir/social/<name>.png: the title of docs/<page> above the figure `fig`
    (a function of the theme, as save() takes it)."""
    c = THEMES["light"]
    title = front_matter(page).get("title")
    if not title:
        raise SystemExit(f"social: docs/{page} has no title in its front matter")
    pixels = _figure_pixels(fig, c, WIDTH - 96)

    card_fig = plt.figure(figsize=(WIDTH / DPI, HEIGHT / DPI), dpi=DPI)
    card_fig.patch.set_facecolor(c["surface"])
    # The band along the top edge, in the accent, and the site's name under it, unless
    # the title starts with it.
    card_fig.add_artist(plt.Rectangle((0, 1 - 12 / HEIGHT), 1, 12 / HEIGHT,
                                      transform=card_fig.transFigure, color=c["accent"]))
    if not title.startswith("libphash"):
        card_fig.text(48 / WIDTH, 1 - 58 / HEIGHT, "libphash", color=c["accent"],
                      fontsize=20, fontweight="bold", va="baseline")
    _title(card_fig, title, c)
    card_fig.text(48 / WIDTH, 34 / HEIGHT, "Perceptual image hashing library in C",
                  color=c["muted"], fontsize=15, va="baseline")
    card_fig.text(1 - 48 / WIDTH, 34 / HEIGHT, "gudoshnikovn.github.io/libphash",
                  color=c["muted"], fontsize=15, va="baseline", ha="right")
    # The figure fills the space between the title and the footer, centered.
    top, bottom = HEIGHT - TITLE_BOTTOM - 16, 70
    box_w, box_h = WIDTH - 96, top - bottom
    h, w = pixels.shape[:2]
    scale = min(box_w / w, box_h / h)
    fw, fh = w * scale, h * scale
    ax = card_fig.add_axes(((WIDTH - fw) / 2 / WIDTH, (bottom + (box_h - fh) / 2) / HEIGHT,
                            fw / WIDTH, fh / HEIGHT))
    ax.imshow(pixels, interpolation="lanczos")
    ax.set_axis_off()

    out = os.path.join(out_dir, "social")
    os.makedirs(out, exist_ok=True)
    card_fig.savefig(os.path.join(out, f"{name}.png"), dpi=DPI,
                     facecolor=c["surface"], metadata={"Software": None})
    plt.close(card_fig)
