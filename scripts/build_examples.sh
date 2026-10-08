#!/usr/bin/env bash
# Builds and (where a two-argument example is given a real image) runs every example
# under examples/ against a throwaway install, via pkg-config -- the same route
# README.md's "Compiling & Linking" section documents. Exists so the examples in the
# README stay compiling, not just readable, and so do the C blocks in README.md and
# MIGRATION.md (scripts/doc_snippets.py): see docs/README.md and README.md for the
# check that keeps every public symbol documented, which this complements by keeping
# the documented *usage* buildable too.
#
# Against the shared library: it exports only what include/libphash.h marks PH_API, so
# a public function that lost its PH_API fails here as an unresolved symbol. A static
# archive would link it regardless.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

BUILD_DIR="$WORK_DIR/build"
PREFIX_DIR="$WORK_DIR/prefix"

echo "==> Configuring + installing a shared libphash (minimal, stb_image only -- examples don't need the vendored decoders)"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DPHASH_BUILD_SHARED=ON \
    -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" -DPHASH_BUILD_TESTS=OFF \
    -DPHASH_USE_LIBJPEG_TURBO=OFF -DPHASH_USE_LIBPNG=OFF -DPHASH_USE_WEBP=OFF -DPHASH_USE_ZLIB_NG=OFF
cmake --build "$BUILD_DIR" --target phash -j
cmake --install "$BUILD_DIR"

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "!!! pkg-config not found -- cannot build examples/ this way" >&2
    exit 1
fi

PKG_FLAGS=$(PKG_CONFIG_PATH="$PREFIX_DIR/lib/pkgconfig" pkg-config --cflags --libs libphash)
export LD_LIBRARY_PATH="$PREFIX_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export DYLD_LIBRARY_PATH="$PREFIX_DIR/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

status=0
for src in "$ROOT_DIR"/examples/*.c; do
    name="$(basename "${src%.c}")"
    echo "==> Building examples/$name.c"
    if ! cc -Wall -Wextra -Werror "$src" $PKG_FLAGS -o "$WORK_DIR/$name"; then
        echo "!!! examples/$name.c failed to compile" >&2
        status=1
        continue
    fi
    D="$ROOT_DIR/tests/data"
    echo "==> Running examples/$name"
    case "$name" in
        compare_two_images)
            "$WORK_DIR/$name" "$D/photo.jpeg" "$D/photo.jpeg"
            ;;
        hash_distance)
            "$WORK_DIR/$name" "$D/photo.jpeg" "$D/photo_copy.jpeg"
            ;;
        digest_and_metrics)
            "$WORK_DIR/$name" "$D/photo.jpeg" "$D/photo_rotated_90.jpeg"
            ;;
        batch_hash)
            "$WORK_DIR/$name" "$D/photo.jpeg" "$D/photo_copy.jpeg" "$D/photo_complex.png" \
                "$D/no-such-file.jpg"
            ;;
        load_sources)
            # Writes the file it then loads.
            "$WORK_DIR/$name" "$WORK_DIR/frame.ppm"
            ;;
        error_handling)
            # One path per outcome it explains; the build has no WebP decoder.
            "$WORK_DIR/$name" "$D/photo.png" "$D/no-such-file.jpg" "$ROOT_DIR/README.md" \
                "$D/corrupted.jpg" "$D/photo.jpeg" "$D/photo.webp"
            ;;
        *)
            "$WORK_DIR/$name" "$D/photo.jpeg"
            ;;
    esac
done

# The C blocks in README.md and MIGRATION.md, against the same installed headers and
# nothing else: a fragment that needs an include the document does not name fails here.
# Unused results and variables are allowed, since a fragment shows one call, not a
# program around it.
PKG_CFLAGS=$(PKG_CONFIG_PATH="$PREFIX_DIR/lib/pkgconfig" pkg-config --cflags libphash)
while read -r origin snippet; do
    echo "==> Compiling the code block at $origin"
    # shellcheck disable=SC2086 # PKG_CFLAGS is a list of flags
    if ! cc -std=c17 -fsyntax-only -Wall -Wextra -Werror -Wno-unused-variable \
        -Wno-unused-result -Wno-shadow $PKG_CFLAGS "$snippet"; then
        echo "!!! the code block at $origin does not compile" >&2
        status=1
    fi
done < <(python3 "$ROOT_DIR/scripts/doc_snippets.py" "$WORK_DIR/snippets")

if [ "$status" -eq 0 ]; then
    echo "==> All examples built and ran successfully; every code block in README.md and MIGRATION.md compiles"
else
    exit "$status"
fi
