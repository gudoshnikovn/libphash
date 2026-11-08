#!/bin/sh
# Generates a version header from the project() VERSION in CMakeLists.txt so the
# Makefile build (which never runs CMake's configure_file) still derives its
# version from the same single source of truth as the CMake build.
#
# Usage: gen_version.sh <CMakeLists.txt> <template.h.in> <output.h>
set -eu

CMAKE_FILE="$1"
TEMPLATE="$2"
OUT="$3"

VERSION=$(sed -nE 's/.*project\([^)]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$CMAKE_FILE" | head -n1)
if [ -z "$VERSION" ]; then
    echo "gen_version.sh: could not find project(... VERSION x.y.z ...) in $CMAKE_FILE" >&2
    exit 1
fi

MAJOR=$(echo "$VERSION" | cut -d. -f1)
MINOR=$(echo "$VERSION" | cut -d. -f2)
PATCH=$(echo "$VERSION" | cut -d. -f3)

mkdir -p "$(dirname "$OUT")"
sed -e "s/@PH_VERSION_MAJOR@/$MAJOR/" \
    -e "s/@PH_VERSION_MINOR@/$MINOR/" \
    -e "s/@PH_VERSION_PATCH@/$PATCH/" \
    -e "s/@PH_VERSION_STRING@/$VERSION/" \
    "$TEMPLATE" > "$OUT"
