#!/usr/bin/env python3
"""Puts the documentation site's navigation on top of every API reference page.

Doxygen writes the API reference as a site of its own, with no way back to the pages
around it; on the documentation site it lives under api/. scripts/site.sh runs this on
its copy in docs/api/, so `make docs` and the CI artifact stay plain Doxygen output.

The bar goes inside Doxygen's #top block, whose height the tree view's layout script
reads, so the side panel still fits under it.

Usage: scripts/site_api_nav.py docs/api
"""
import os
import sys

# Relative to an API page: the site's root is one level up.
LINKS = [
    ("libphash", "../"),
    ("Theory", "../theory/perceptual-hashing/"),
    ("Guide", "../guide/quickstart/"),
    ("API reference", "index.html"),
    ("Project", "../project/changelog/"),
]
CURRENT = "API reference"

STYLE = """<style>
.ph-site-bar { display: flex; align-items: center; gap: 1.4em; padding: 0 16px;
  height: 44px; box-sizing: border-box; border-bottom: 1px solid #e4e3df;
  background: #fcfcfb; font: 14px/1.2 system-ui, -apple-system, "Segoe UI", sans-serif; }
.ph-site-bar a { color: #3a3a37; text-decoration: none; }
.ph-site-bar a:hover { color: #2a78d6; }
.ph-site-bar a.ph-home { font-weight: 700; color: #1f1f1e; margin-right: 0.6em; }
.ph-site-bar a.ph-current { color: #2a78d6; font-weight: 600; }
</style>
"""
MARKER = 'class="ph-site-bar"'
TOP = '<div id="top"><!-- do not remove this div, it is closed by doxygen! -->'


def bar():
    items = []
    for i, (label, href) in enumerate(LINKS):
        cls = "ph-home" if i == 0 else ("ph-current" if label == CURRENT else "")
        attr = f' class="{cls}"' if cls else ""
        items.append(f'<a href="{href}"{attr}>{label}</a>')
    return f'<nav {MARKER} aria-label="Documentation">' + "".join(items) + "</nav>"


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    root = sys.argv[1]
    done = present = 0
    for name in sorted(os.listdir(root)):
        if not name.endswith(".html"):
            continue
        path = os.path.join(root, name)
        with open(path, encoding="utf-8") as f:
            html = f.read()
        if MARKER in html:
            present += 1
            continue
        if TOP not in html or "</head>" not in html:
            continue
        html = html.replace("</head>", STYLE + "</head>", 1)
        html = html.replace(TOP, TOP + "\n" + bar(), 1)
        with open(path, "w", encoding="utf-8") as f:
            f.write(html)
        done += 1
    if done + present == 0:
        print(f"site_api_nav: no Doxygen page in {root} took the bar", file=sys.stderr)
        return 1
    print(f"site_api_nav: navigation bar on {done} API reference pages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
