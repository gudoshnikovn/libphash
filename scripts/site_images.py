#!/usr/bin/env python3
"""Gives every figure of the built site its size, and every figure but a page's first a
lazy load.

A figure is an SVG drawn twice, once for each color scheme, and the theme hides the one
the other scheme shows (`display: none` on `#only-light` or `#only-dark`). Loaded eagerly,
a browser fetches both copies of every figure on a page as it opens, down to the last one,
and on a page of twenty figures those are most of what it transfers. With
`loading="lazy"`, a figure is fetched when it scrolls near the screen, and the hidden copy
never is. A page's first figure, which may be on the screen when it opens, keeps the eager
load, as a lazy one would start only after the layout.

`width` and `height` are the SVG's own size, the size the browser draws it at without them
(the theme scales it down to the column with `max-width: 100%; height: auto`), or the width
a page gives a small figure and the height in the SVG's proportion; with them the browser
reserves the figure's place before the file arrives, so the text below does not move.

Only figures inside a page's <article> are changed; the header's logo is the theme's.
Fails on a figure whose SVG is missing or has no size, so every figure gets both.

Usage: scripts/site_images.py <built site dir>     (build/site, from scripts/site.sh)
"""
import os
import re
import sys
from urllib.parse import unquote

ARTICLE = re.compile(r"<article\b.*?</article>", re.S)
IMG = re.compile(r"<img\b[^>]*>")
SRC = re.compile(r'\bsrc="([^"#]+)(#[^"]*)?"')
WIDTH = re.compile(r'\swidth="(\d+)"')
# The size an SVG of the site's figures is written with (matplotlib: points).
SVG_SIZE = re.compile(r'<svg\b[^>]*?\bwidth="([\d.]+)(pt|px)?"[^>]*?\bheight="([\d.]+)(pt|px)?"',
                      re.S)
PX_PER_UNIT = {"pt": 4 / 3, "px": 1.0, None: 1.0}


def svg_size(path, sizes):
    if path not in sizes:
        with open(path, encoding="utf-8") as f:
            head = f.read(4096)
        m = SVG_SIZE.search(head)
        if not m:
            raise SystemExit(f"site_images: {path} has no width and height")
        sizes[path] = (round(float(m.group(1)) * PX_PER_UNIT[m.group(2)]),
                       round(float(m.group(3)) * PX_PER_UNIT[m.group(4)]))
    return sizes[path]


def figure(src):
    """One figure's name, the same for its light and its dark copy."""
    return re.sub(r"\.(light|dark)\.svg$", "", src)


def rewrite(page, html, sizes):
    first = None
    counts = {"lazy": 0, "sized": 0}

    def img(m):
        nonlocal first
        tag = m.group(0)
        src = SRC.search(tag)
        if not src or not src.group(1).endswith(".svg"):
            return tag
        path = os.path.normpath(os.path.join(os.path.dirname(page), unquote(src.group(1))))
        if not os.path.isfile(path):
            raise SystemExit(f"site_images: {page}: {src.group(1)} is not in the site")
        attrs = ""
        if " height=" not in tag:
            w, h = svg_size(path, sizes)
            given = WIDTH.search(tag)
            if given:
                # A width the page sets keeps the figure smaller than its SVG.
                attrs += f' height="{round(h * int(given.group(1)) / w)}"'
            else:
                attrs += f' width="{w}" height="{h}"'
            counts["sized"] += 1
        if first is None:
            first = figure(src.group(1))
        if figure(src.group(1)) != first and " loading=" not in tag:
            attrs += ' loading="lazy" decoding="async"'
            counts["lazy"] += 1
        return tag[:4] + attrs + tag[4:]

    def article(m):
        return IMG.sub(img, m.group(0))

    return ARTICLE.sub(article, html), counts


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    sizes, pages, lazy, sized = {}, 0, 0, 0
    for dirpath, _, files in os.walk(sys.argv[1]):
        for name in files:
            if not name.endswith(".html"):
                continue
            page = os.path.join(dirpath, name)
            with open(page, encoding="utf-8") as f:
                html = f.read()
            new, counts = rewrite(page, html, sizes)
            if new != html:
                with open(page, "w", encoding="utf-8") as f:
                    f.write(new)
                pages += 1
                lazy += counts["lazy"]
                sized += counts["sized"]
    print(f"site_images: {sized} figures sized and {lazy} loaded lazily, on {pages} pages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
