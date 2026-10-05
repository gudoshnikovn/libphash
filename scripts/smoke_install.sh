#!/usr/bin/env bash
# Smoke test for install()/pkg-config/find_package packaging:
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

# Runs a consumer the way its user would, with no LD_LIBRARY_PATH/DYLD_LIBRARY_PATH: a
# shared libphash must be found through the rpath the link gave the consumer. One that
# starts only with the variable set is reported as missing its rpath.
run_consumer() {
    local bin="$1" libdir="$2"
    if env -u LD_LIBRARY_PATH -u DYLD_LIBRARY_PATH "$bin"; then
        return 0
    fi
    if LD_LIBRARY_PATH="$libdir" DYLD_LIBRARY_PATH="$libdir" "$bin" >/dev/null 2>&1; then
        echo "!!! $bin starts only with LD_LIBRARY_PATH/DYLD_LIBRARY_PATH=$libdir: no rpath" >&2
    fi
    return 1
}

echo "==> Configuring + building libphash (${1:-static})"
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" -DPHASH_BUILD_TESTS=OFF)
if [[ "${1:-static}" == "shared" ]]; then
    CMAKE_ARGS+=(-DPHASH_BUILD_SHARED=ON)
fi
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" --target phash -j
cmake --install "$BUILD_DIR"

# Structural check on the generated .pc: `prefix` is the .pc file's own directory walked
# back up (${pcfiledir}), and `libdir`/`includedir` are relative to ${prefix} -- the form
# a plain `pkg-config` relocates with no option, under every implementation. An absolute
# path anywhere would be the configure-time prefix, gone once the tree moves.
PC_FILE="$PREFIX_DIR/lib/pkgconfig/libphash.pc"
echo "==> Checking generated $PC_FILE is relocatable"
[[ -f "$PC_FILE" ]] || { echo "!!! $PC_FILE was not installed" >&2; exit 1; }
prefix_val=$(sed -n 's/^prefix=//p' "$PC_FILE")
echo "    prefix=$prefix_val"
case "$prefix_val" in
    '${pcfiledir}/'*) ;;
    *) echo "!!! prefix in libphash.pc is not relative to \${pcfiledir}: $prefix_val" >&2; exit 1 ;;
esac
for var in libdir includedir; do
    val=$(sed -n "s/^${var}=//p" "$PC_FILE")
    echo "    $var=$val"
    case "$val" in
        '${prefix}/'*|'${exec_prefix}/'*) ;;
        *) echo "!!! $var in libphash.pc is not relative to \${prefix}: $val" >&2; exit 1 ;;
    esac
done

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
run_consumer "$CONSUMER_DIR/build/consumer" "$PREFIX_DIR/lib"

if command -v pkg-config >/dev/null 2>&1; then
    echo "==> Building consumer via pkg-config"
    PKG_CONFIG_PATH="$PREFIX_DIR/lib/pkgconfig" pkg-config --exists libphash
    PKG_FLAGS=$(PKG_CONFIG_PATH="$PREFIX_DIR/lib/pkgconfig" pkg-config --cflags --libs libphash)
    cc "$CONSUMER_DIR/main.c" $PKG_FLAGS -o "$CONSUMER_DIR/consumer_pc"
    run_consumer "$CONSUMER_DIR/consumer_pc" "$PREFIX_DIR/lib"
    # The install tree is physically moved and the consumer is rebuilt from the new
    # location with the same plain command (no --define-prefix), as README shows it.
    MOVED_DIR="$WORK_DIR/moved-prefix"
    mv "$PREFIX_DIR" "$MOVED_DIR"
    echo "==> Checking .pc relocatability (prefix moved to $MOVED_DIR)"
    MOVED_FLAGS=$(PKG_CONFIG_PATH="$MOVED_DIR/lib/pkgconfig" \
        pkg-config --cflags --libs libphash)
    echo "    pkg-config after move: $MOVED_FLAGS"
    case "$MOVED_FLAGS" in
        *"$PREFIX_DIR"*)
            echo "!!! .pc is NOT relocatable: still points at the old prefix $PREFIX_DIR" >&2
            exit 1
            ;;
    esac
    for want in "-I$MOVED_DIR/" "-L$MOVED_DIR/"; do
        case " $MOVED_FLAGS " in
            *" $want"*) ;;
            *) echo "!!! expected $want... in relocated pkg-config output" >&2; exit 1 ;;
        esac
    done
    # And it must still build and run from the moved tree, the shared library found
    # through the rpath the .pc gave it.
    cc "$CONSUMER_DIR/main.c" $MOVED_FLAGS -o "$CONSUMER_DIR/consumer_moved"
    run_consumer "$CONSUMER_DIR/consumer_moved" "$MOVED_DIR/lib"
else
    echo "==> pkg-config not found, skipping that half of the smoke test"
fi

echo "==> Smoke test passed"
