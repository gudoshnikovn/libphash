#!/usr/bin/env bash
# Builds the documentation site from docs/ with Zensical (zensical.toml) into build/site/,
# with the API reference (scripts/api_pages.py) under api/ and the algorithm pages' figures
# drawn from the release build (tools/site/). `make site` and `make site-serve` call this
# script; it needs the vendored submodules, Doxygen 1.18 and Python 3.12 or later, and
# installs the Python packages itself (below).
#
# The build is strict: a broken link, a page missing from the navigation or an included
# file that does not exist fails it, so the site cannot ship a dead reference.
#
# The Python packages come from scripts/site-requirements.txt, a lock compiled from
# scripts/site-requirements.in that pins every package with its hashes, into a virtual
# environment of their own, build/site-venv/. The script creates it on the first run and
# again whenever the lock changes, with uv when it is installed and with python3 -m venv
# and pip otherwise; both install exactly the lock. Every version is pinned because
# Zensical is young, and its output and warnings change between releases, and the Markdown
# extensions and the drawing stack decide how a page renders and the bytes of a figure:
# an unpinned package would turn the build red, or quietly change the site, the day it
# moves. A version is raised deliberately, in its own commit, in the .in file and the lock.
#
# Usage: scripts/site.sh          # build into build/site/
#        scripts/site.sh serve    # build, then serve with live reload on 127.0.0.1:8000
#        ZENSICAL=/path/to/zensical PYTHON=/path/to/python3 scripts/site.sh
#                                 # use an environment set up elsewhere instead
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCK="$ROOT_DIR/scripts/site-requirements.txt"
VENV="$ROOT_DIR/build/site-venv"
REQUIRED_VERSION="$(sed -nE 's/^zensical==([0-9.]+)$/\1/p' "$ROOT_DIR/scripts/site-requirements.in")"
MODE="${1:-build}"

# The environment is current when it was installed from this very lock; a copy of the
# lock inside it records which.
if [ -z "${ZENSICAL:-}" ] && [ -z "${PYTHON:-}" ]; then
    if ! cmp -s "$LOCK" "$VENV/site-requirements.txt"; then
        echo "==> site: installing the Python packages into build/site-venv/"
        rm -rf "$VENV"
        if command -v uv >/dev/null 2>&1; then
            uv venv --quiet "$VENV"
            uv pip sync --quiet --python "$VENV/bin/python" --require-hashes "$LOCK"
        else
            python3 -m venv "$VENV"
            "$VENV/bin/python" -m pip install --quiet --require-hashes --no-deps -r "$LOCK"
        fi
        cp "$LOCK" "$VENV/site-requirements.txt"
    fi
    ZENSICAL="$VENV/bin/zensical"
    PYTHON="$VENV/bin/python"
fi
ZS="${ZENSICAL:-zensical}"
PY="${PYTHON:-python3}"

if ! command -v "$ZS" >/dev/null 2>&1; then
    echo "!!! $ZS not found; the site is built with Zensical $REQUIRED_VERSION" >&2
    exit 1
fi
version="$("$ZS" --version 2>/dev/null | tr -d '[:space:]' || true)"
if [ "$version" != "$REQUIRED_VERSION" ]; then
    echo "!!! $ZS is ${version:-an unknown version}, the site is built with $REQUIRED_VERSION" >&2
    echo "    (scripts/site-requirements.txt)" >&2
    exit 1
fi

cd "$ROOT_DIR"

# The API reference is generated, not tracked: Doxygen reads the header's doc comments
# and fails on any warning (scripts/api_docs.sh), and scripts/api_pages.py turns its XML
# into Markdown pages in docs/api/ (ignored by git), one per topic. They are pages of the
# site like any other -- its theme, its search, and anchors the strict build checks when
# a page links to a function.
#
# Generated files reach docs/ through sync_into(): written elsewhere first, then copied
# over only where their bytes differ, and the directory itself is never removed. A running
# `zensical serve` watches docs/, and rewriting a whole generated tree under it, even with
# the same bytes, sends the server into a rebuild loop that reloads an open page every
# second.
sync_into() {
    mkdir -p "$2"
    rsync -a --delete --checksum "$1/" "$2/"
}
scripts/api_docs.sh
rm -rf build/api-pages
python3 scripts/api_pages.py build/api-docs/xml build/api-pages
sync_into build/api-pages docs/api

# The figures and measured tables on the algorithm pages are drawn by
# tools/site/render.py from what site_stages (tools/site/stages/) measures, against the
# release build (every bundled decoder, as the published archives), into
# docs/assets/generated/ (ignored by git): a page always shows the code it is built with.
# The charts over the photo corpus need its files: the first build downloads them
# (tools/site/fetch_corpus.py, into ~/.cache/libphash-site/), and a build without a
# network or a cached copy draws them without the photographs and says so on the chart.
# The measurements are cached in build/site-cache/ and repeated only when the library,
# the tool or a corpus changes.
cmake --preset release >/dev/null
cmake --build --preset release --target site_stages
rm -rf build/site-generated
"$PY" tools/site/render.py --tool build/release/site_stages --image tests/data/photo.jpeg \
    --out build/site-generated
sync_into build/site-generated docs/assets/generated
"$PY" tools/site/fetch_corpus.py page --out build/site-corpus.md
cmp -s build/site-corpus.md docs/project/corpus.md || cp build/site-corpus.md docs/project/corpus.md

python3 scripts/check_site_links.py

case "$MODE" in
    build)
        "$ZS" build --clean --strict
        echo "==> site: build/site/index.html"
        ;;
    serve)
        exec "$ZS" serve
        ;;
    *)
        echo "!!! unknown mode '$MODE' (build or serve)" >&2
        exit 1
        ;;
esac
