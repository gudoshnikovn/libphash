#!/usr/bin/env bash
# Smoke test for task 4 (install()/pkg-config/find_package packaging):
# builds libphash, installs it into a throwaway prefix, then builds and runs
# a tiny consumer against the installed tree via both find_package(phash)
# and pkg-config, to catch anything cmake --install alone wouldn't (missing
# transitive link deps, wrong installed names, broken generated config).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

BUILD_DIR="$WORK_DIR/build"
PREFIX_DIR="$WORK_DIR/prefix"
CONSUMER_DIR="$WORK_DIR/consumer"

echo "==> Configuring + building libphash (${1:-static})"
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" -DPHASH_BUILD_TESTS=OFF)
if [[ "${1:-static}" == "shared" ]]; then
    CMAKE_ARGS+=(-DPHASH_BUILD_SHARED=ON)
fi
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" --target phash -j
cmake --install "$BUILD_DIR"

echo "==> Building consumer via find_package(phash)"
mkdir -p "$CONSUMER_DIR"
cat > "$CONSUMER_DIR/main.c" <<'EOF'
#include <libphash.h>
#include <stdio.h>
int main(void) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) { fprintf(stderr, "ph_create failed\n"); return 1; }
    printf("libphash smoke test OK (version %s)\n", ph_version());
    ph_free(ctx);
    return 0;
}
EOF
cat > "$CONSUMER_DIR/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.10)
project(phash_smoke_consumer C)
find_package(phash REQUIRED CONFIG)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE phash::phash)
EOF
cmake -S "$CONSUMER_DIR" -B "$CONSUMER_DIR/build" -DCMAKE_PREFIX_PATH="$PREFIX_DIR"
cmake --build "$CONSUMER_DIR/build" -j
if [[ "${1:-static}" == "shared" ]]; then
    DYLD_LIBRARY_PATH="$PREFIX_DIR/lib" LD_LIBRARY_PATH="$PREFIX_DIR/lib" "$CONSUMER_DIR/build/consumer"
else
    "$CONSUMER_DIR/build/consumer"
fi

if command -v pkg-config >/dev/null 2>&1; then
    echo "==> Building consumer via pkg-config"
    PKG_CONFIG_PATH="$PREFIX_DIR/lib/pkgconfig" pkg-config --exists libphash
    PKG_FLAGS=$(PKG_CONFIG_PATH="$PREFIX_DIR/lib/pkgconfig" pkg-config --cflags --libs --static libphash)
    cc "$CONSUMER_DIR/main.c" $PKG_FLAGS -o "$CONSUMER_DIR/consumer_pc"
    if [[ "${1:-static}" == "shared" ]]; then
        DYLD_LIBRARY_PATH="$PREFIX_DIR/lib" LD_LIBRARY_PATH="$PREFIX_DIR/lib" "$CONSUMER_DIR/consumer_pc"
    else
        "$CONSUMER_DIR/consumer_pc"
    fi
else
    echo "==> pkg-config not found, skipping that half of the smoke test"
fi

echo "==> Smoke test passed"
