#!/usr/bin/env bash
# R42: smoke test for a packaged release archive (scripts/package_release.sh
# output). Extracts the archive in isolation and builds a minimal consumer
# against it via find_package(phash) -- the same mechanism scripts/smoke_install.sh
# already exercises against a fresh `cmake --install` tree, just pointed at an
# archive instead. Reusing find_package (rather than hand-listing -l flags for
# TurboJPEG/libpng/webp/zlib-ng) is deliberate: getting the static archive's
# transitive link set right by hand here would just re-derive, and could easily
# drift from, the INTERFACE_LINK_LIBRARIES the installed phashConfig.cmake
# already carries (see CMakeLists.txt's install(EXPORT phashTargets ...) block).
#
# This is run on a clean machine with nothing but the archive plus a compiler
# and CMake -- no access to this checkout's build tree or CMake cache -- which
# is the actual condition a consumer downloading the archive is in.
#
# Usage: scripts/smoke_release_artifact.sh <archive-path> <static|shared>
set -euo pipefail

ARCHIVE="${1:?usage: smoke_release_artifact.sh <archive-path> <static|shared>}"
KIND="${2:?usage: smoke_release_artifact.sh <archive-path> <static|shared>}"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

echo "==> Extracting $ARCHIVE"
case "$ARCHIVE" in
    *.zip) unzip -q "$ARCHIVE" -d "$WORK_DIR" ;;
    *.tar.gz|*.tgz) tar -xzf "$ARCHIVE" -C "$WORK_DIR" ;;
    *) echo "smoke_release_artifact.sh: unrecognized archive extension: $ARCHIVE" >&2; exit 1 ;;
esac

STAGE_DIR=$(find "$WORK_DIR" -mindepth 1 -maxdepth 1 -type d | head -n1)
[ -n "$STAGE_DIR" ] || { echo "smoke_release_artifact.sh: archive was empty" >&2; exit 1; }

for f in "$STAGE_DIR/include/libphash.h" "$STAGE_DIR/LICENSE" "$STAGE_DIR/THIRD-PARTY-NOTICES.md"; do
    [ -f "$f" ] || { echo "!!! missing $f in archive" >&2; exit 1; }
done

if [ "$KIND" = "shared" ]; then
    # The FFI-consumer case R42 exists for: no compiler involved at all, just a
    # loadable object present in the archive under a predictable name.
    SEARCH_DIRS=()
    for d in "$STAGE_DIR/bin" "$STAGE_DIR/lib"; do
        [ -d "$d" ] && SEARCH_DIRS+=("$d")
    done
    SHARED_LIB=$(find "${SEARCH_DIRS[@]}" -maxdepth 1 \
        \( -name '*.so*' -o -name '*.dylib' -o -name '*.dll' \) 2>/dev/null | head -n1 || true)
    [ -n "$SHARED_LIB" ] || { echo "!!! no shared library found in archive" >&2; exit 1; }
    echo "==> Found shared library: $SHARED_LIB"
fi

CONSUMER_DIR="$WORK_DIR/consumer"
mkdir -p "$CONSUMER_DIR"
cat > "$CONSUMER_DIR/main.c" <<'EOF'
#include <libphash.h>
#include <stdio.h>
int main(void) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) { fprintf(stderr, "ph_create failed\n"); return 1; }
    printf("libphash release artifact smoke test OK (version %s)\n", ph_version());
    ph_free(ctx);
    return 0;
}
EOF
cat > "$CONSUMER_DIR/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.10)
project(phash_release_smoke_consumer C)
find_package(phash REQUIRED CONFIG)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE phash::phash)
EOF

echo "==> Building consumer via find_package(phash) against the extracted archive"
cmake -S "$CONSUMER_DIR" -B "$CONSUMER_DIR/build" -DCMAKE_PREFIX_PATH="$STAGE_DIR"
cmake --build "$CONSUMER_DIR/build" --config Release -j

BIN="$CONSUMER_DIR/build/consumer"
[ -f "$BIN" ] || BIN="$CONSUMER_DIR/build/Release/consumer.exe"
[ -f "$BIN" ] || BIN="$CONSUMER_DIR/build/consumer.exe"

echo "==> Running consumer"
LIBDIR="$STAGE_DIR/lib"
[ -d "$LIBDIR" ] || LIBDIR="$STAGE_DIR/lib64"
PATH="$LIBDIR:$STAGE_DIR/bin:$PATH" \
    LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DYLD_LIBRARY_PATH="$LIBDIR${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
    "$BIN"

echo "==> Smoke test passed"
