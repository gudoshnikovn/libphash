#!/usr/bin/env bash
# Builds the documentation site from docs/ with Zensical (zensical.toml) into build/site/,
# with the API reference (scripts/api_docs.sh) under api/. `make site` and
# `make site-serve` call this script.
#
# The build is strict: a broken link, a page missing from the navigation or an included
# file that does not exist fails it, so the site cannot ship a dead reference.
#
# Why the version is pinned: Zensical is young and its output and warnings change between
# releases; an unpinned tool would turn the build red, or quietly change the site, the day
# a developer's environment moves. The version is raised deliberately, in its own commit,
# in scripts/site-requirements.txt.
#
# Usage: scripts/site.sh          # build into build/site/
#        scripts/site.sh serve    # build, then serve with live reload on 127.0.0.1:8000
#        ZENSICAL=/path/to/zensical scripts/site.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REQUIRED_VERSION="$(sed -nE 's/^zensical==([0-9.]+)$/\1/p' "$ROOT_DIR/scripts/site-requirements.txt")"
INSTALL_HINT="python3 -m pip install -r scripts/site-requirements.txt"
ZS="${ZENSICAL:-zensical}"
MODE="${1:-build}"

if ! command -v "$ZS" >/dev/null 2>&1; then
    echo "!!! $ZS not found; install Zensical $REQUIRED_VERSION: $INSTALL_HINT" >&2
    exit 1
fi
version="$("$ZS" --version 2>/dev/null | tr -d '[:space:]' || true)"
if [ "$version" != "$REQUIRED_VERSION" ]; then
    echo "!!! $ZS is ${version:-an unknown version}, the site is built with $REQUIRED_VERSION" >&2
    echo "    install it with: $INSTALL_HINT" >&2
    exit 1
fi

cd "$ROOT_DIR"

# The API reference is generated, not tracked: Doxygen writes it to build/api-docs/html/,
# and it is copied into docs/api/ (ignored by git) so the site serves it as static files
# and the navigation's link to api/index.html resolves.
scripts/api_docs.sh
rm -rf docs/api
cp -R build/api-docs/html docs/api
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
