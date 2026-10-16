#!/usr/bin/env python3
"""Fails unless every page of the built site describes itself to a search engine and a
link preview: a title, a description of its own, and a preview image that exists.

A search result shows a page's <title> and its meta description, and a link pasted into a
chat or a forum shows its og:title, og:description and og:image (overrides/main.html
writes them from the page's front matter). A page without a description of its own shows
the site's, the same text as every other such page, and a search engine takes two pages
with one description for one page said twice. So every page has a description, no two
pages share one, and each is between MIN and MAX characters: a shorter one says too
little to choose the page by, and a search result cuts a longer one off.

The check reads the HTML the build wrote, so it covers every page, including the API
reference that scripts/api_pages.py generates.

Usage: scripts/check_site_meta.py <built site dir>     (build/site, from scripts/site.sh)
"""
import html
import os
import re
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MIN, MAX = 100, 160

TITLE = re.compile(r"<title>(.*?)</title>", re.S)


def meta(page, attr, name):
    m = re.search(rf'<meta {attr}="{re.escape(name)}" content="([^"]*)"', page)
    return html.unescape(m.group(1)) if m else None


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    site = sys.argv[1]
    with open(os.path.join(ROOT, "zensical.toml"), "rb") as f:
        project = tomllib.load(f)["project"]
    site_url, default = project["site_url"], project["site_description"]

    problems, seen, pages = [], {}, 0
    for dirpath, _, files in os.walk(site):
        for name in files:
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, site)
            if name != "index.html":  # 404.html and the theme's own files are not pages
                continue
            pages += 1
            with open(path, encoding="utf-8") as f:
                page = f.read()
            title = TITLE.search(page)
            description = meta(page, "name", "description")
            image = meta(page, "property", "og:image")
            if not title or not title.group(1).strip():
                problems.append(f"{rel}: no <title>")
            if not description or description == default:
                problems.append(f"{rel}: no description of its own (front matter `description`)")
                continue
            if not MIN <= len(description) <= MAX:
                problems.append(f"{rel}: description of {len(description)} characters, "
                                f"not {MIN}-{MAX}")
            if description in seen:
                problems.append(f"{rel}: the same description as {seen[description]}")
            seen.setdefault(description, rel)
            if not image or not image.startswith(site_url):
                problems.append(f"{rel}: no og:image under {site_url}")
            elif not os.path.isfile(os.path.join(site, image[len(site_url):])):
                problems.append(f"{rel}: og:image {image[len(site_url):]} is not in the site")
    if problems:
        print("check_site_meta: pages that do not describe themselves:\n  - "
              + "\n  - ".join(sorted(problems)), file=sys.stderr)
        return 1
    print(f"check_site_meta: {pages} pages, each with a title, its own description and "
          "a preview image")
    return 0


if __name__ == "__main__":
    sys.exit(main())
