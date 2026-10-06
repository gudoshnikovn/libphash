#!/usr/bin/env bash
# Builds the API reference from include/libphash.h with Doxygen (docs/Doxyfile) into
# build/api-docs/html/. `make docs` and the CI format-check job both call this script.
#
# Why the version is pinned: the run fails on any Doxygen warning, and what Doxygen
# warns about, and its defaults, change between releases -- an unpinned tool turns the
# check red, or quietly green, on unchanged text the day a runner image or a
# developer's package manager moves. The version is raised deliberately, in its own
# commit, in this script and scripts/install_doxygen.sh together.
#
# Usage: scripts/api_docs.sh
#        DOXYGEN=/path/to/doxygen scripts/api_docs.sh
set -euo pipefail

REQUIRED_VERSION=1.18
INSTALL_HINT="brew install doxygen (macOS), scripts/install_doxygen.sh <dir> (Linux x86-64), or https://www.doxygen.nl/download.html"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DX="${DOXYGEN:-doxygen}"

if ! command -v "$DX" >/dev/null 2>&1; then
    echo "!!! $DX not found; install Doxygen $REQUIRED_VERSION: $INSTALL_HINT" >&2
    exit 1
fi

version="$("$DX" --version 2>/dev/null | sed -nE 's/^([0-9]+\.[0-9]+)\..*/\1/p' || true)"
if [ "$version" != "$REQUIRED_VERSION" ]; then
    echo "!!! $DX is ${version:-an unknown version}, the API reference is checked with $REQUIRED_VERSION" >&2
    echo "    install it with: $INSTALL_HINT" >&2
    exit 1
fi

cd "$ROOT_DIR"
mkdir -p build/api-docs
"$DX" docs/Doxyfile
echo "==> API reference: build/api-docs/html/index.html"
