#!/usr/bin/env bash
# Installs the Doxygen release scripts/api_docs.sh requires, from the project's official
# Linux x86-64 binary, verified against its SHA-256. CI runs it; so can a Linux developer
# whose package manager carries another version.
#
# Usage: scripts/install_doxygen.sh <dir>    # installs <dir>/bin/doxygen
set -euo pipefail

VERSION=1.18.0
SHA256=14fa81bdc34171edb5f1f02b1d60e74802f0439b77fa44e592565d517d72df90
URL="https://github.com/doxygen/doxygen/releases/download/Release_${VERSION//./_}/doxygen-${VERSION}.linux.bin.tar.gz"

DEST="${1:?usage: $0 <install dir>}"
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) ;;
    *) echo "!!! install_doxygen.sh installs the Linux x86-64 binary; elsewhere install Doxygen $VERSION another way" >&2; exit 1 ;;
esac

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
curl -fsSL --retry 3 -o "$WORK_DIR/doxygen.tar.gz" "$URL"
echo "$SHA256  $WORK_DIR/doxygen.tar.gz" | sha256sum -c -
mkdir -p "$DEST"
tar -xzf "$WORK_DIR/doxygen.tar.gz" -C "$DEST" --strip-components=1
"$DEST/bin/doxygen" --version
