#!/usr/bin/env bash
# Builds and (where a two-argument example is given a real image) runs every example
# under examples/ against a throwaway install, via pkg-config -- the same route
# README.md's "Compiling & Linking" section documents. Exists so the examples in the
# README stay compiling, not just readable: see docs/README.md and README.md for the
# check that keeps every public symbol documented, which this complements by keeping
# the documented *usage* buildable too.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

BUILD_DIR="$WORK_DIR/build"
PREFIX_DIR="$WORK_DIR/prefix"

echo "==> Configuring + installing libphash (minimal, stb_image only -- examples don't need the vendored decoders)"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" -DPHASH_BUILD_TESTS=OFF \
    -DPHASH_USE_TURBOJPEG=OFF -DPHASH_USE_LIBPNG=OFF -DPHASH_USE_WEBP=OFF -DPHASH_USE_ZLIB_NG=OFF
cmake --build "$BUILD_DIR" --target phash -j
cmake --install "$BUILD_DIR"

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "!!! pkg-config not found -- cannot build examples/ this way" >&2
    exit 1
fi

PKG_FLAGS=$(PKG_CONFIG_PATH="$PREFIX_DIR/lib/pkgconfig" pkg-config --cflags --libs --static libphash)

status=0
for src in "$ROOT_DIR"/examples/*.c; do
    name="$(basename "${src%.c}")"
    echo "==> Building examples/$name.c"
    if ! cc -Wall -Wextra -Werror "$src" $PKG_FLAGS -o "$WORK_DIR/$name"; then
        echo "!!! examples/$name.c failed to compile" >&2
        status=1
        continue
    fi
    echo "==> Running examples/$name (against tests/data/photo.jpeg)"
    case "$name" in
        compare_two_images)
            "$WORK_DIR/$name" "$ROOT_DIR/tests/data/photo.jpeg" "$ROOT_DIR/tests/data/photo.jpeg"
            ;;
        *)
            "$WORK_DIR/$name" "$ROOT_DIR/tests/data/photo.jpeg"
            ;;
    esac
done

if [ "$status" -eq 0 ]; then
    echo "==> All examples built and ran successfully"
else
    exit "$status"
fi
