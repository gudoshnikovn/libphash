#!/usr/bin/env python3
"""Fails unless every relative link on a site page stays inside docs/ and exists.

Zensical's strict build checks links to pages and their anchors, but not a link to any
other file: `../include/libphash.h` works when the Markdown is read on GitHub and is a
dead link on the site. A page links to a file outside docs/ by its GitHub URL instead.

Pages are the Markdown files under docs/ except the ones zensical.toml excludes from the
site (the glob list of its exclude plugin) and the generated API reference (docs/api/).

Usage: scripts/check_site_links.py
"""
import fnmatch
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")
LINK = re.compile(r"\]\(([^)\s]+)\)")


def excluded():
    """The glob patterns zensical.toml's exclude plugin keeps off the site."""
    with open(os.path.join(ROOT, "zensical.toml"), encoding="utf-8") as f:
        text = f.read()
    block = re.search(r"\[project\.plugins\.exclude\]\s*glob\s*=\s*\[(.*?)\]", text, re.S)
    if not block:
        sys.exit("check_site_links: no glob list under [project.plugins.exclude] in zensical.toml")
    return re.findall(r'"([^"]+)"', block.group(1)) + ["api/*"]


def is_excluded(rel, entries):
    return any(fnmatch.fnmatch(rel, e) for e in entries)


def main():
    entries = excluded()
    problems = []
    for dirpath, _, files in os.walk(DOCS):
        for name in sorted(files):
            if not name.endswith(".md"):
                continue
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, DOCS).replace(os.sep, "/")
            if is_excluded(rel, entries):
                continue
            fence = False
            with open(path, encoding="utf-8") as f:
                for lineno, line in enumerate(f, 1):
                    if line.lstrip().startswith("```"):
                        fence = not fence
                    if fence:
                        continue
                    for target in LINK.findall(line):
                        if re.match(r"^[a-z]+:|^#", target):
                            continue
                        dest = os.path.normpath(os.path.join(dirpath, target.split("#")[0]))
                        if not dest.startswith(DOCS + os.sep):
                            problems.append(f"docs/{rel}:{lineno}: {target} leaves docs/; "
                                            "link to it by its GitHub URL")
                        elif not os.path.exists(dest):
                            problems.append(f"docs/{rel}:{lineno}: {target} does not exist")
    for p in problems:
        print(p, file=sys.stderr)
    if problems:
        return 1
    print("check_site_links: every relative link on a site page stays inside docs/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
