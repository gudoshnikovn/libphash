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

# R15/L11: structural check on the generated .pc. This is the check with actual teeth:
# `libdir`/`includedir` must be written relative to ${prefix}, because that is the only
# form every pkg-config implementation can relocate. It used to substitute
# @CMAKE_INSTALL_FULL_LIBDIR@ -- a configure-time absolute path.
#
# The behavioural check further down is kept, but on its own it does NOT catch the
# regression everywhere: pkgconf's --define-prefix also string-replaces the old prefix
# inside absolute variable values, so pkgconf >= 3 papers over the broken form.
# freedesktop pkg-config only redefines the `prefix` variable and does not.
PC_FILE="$PREFIX_DIR/lib/pkgconfig/libphash.pc"
echo "==> Checking generated $PC_FILE is prefix-relative"
[[ -f "$PC_FILE" ]] || { echo "!!! $PC_FILE was not installed" >&2; exit 1; }
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
    # R15/L11: end-to-end companion to the structural check above -- the install tree is
    # physically moved and the consumer is rebuilt from the new location, so the .pc has
    # to be usable and not merely well-formed. `--define-prefix` is required: no
    # pkg-config redefines the prefix unless asked (Windows builds aside).
    MOVED_DIR="$WORK_DIR/moved-prefix"
    mv "$PREFIX_DIR" "$MOVED_DIR"
    if pkg-config --dont-define-prefix --version >/dev/null 2>&1; then
        echo "==> Checking .pc relocatability (prefix moved to $MOVED_DIR)"
        MOVED_FLAGS=$(PKG_CONFIG_PATH="$MOVED_DIR/lib/pkgconfig" \
            pkg-config --define-prefix --cflags --libs --static libphash)
        echo "    pkg-config after move: $MOVED_FLAGS"
        case "$MOVED_FLAGS" in
            *"$PREFIX_DIR"*)
                echo "!!! .pc is NOT relocatable: still points at the old prefix $PREFIX_DIR" >&2
                exit 1
                ;;
        esac
        for want in "-I$MOVED_DIR/include" "-L$MOVED_DIR/lib"; do
            case " $MOVED_FLAGS " in
                *" $want "*) ;;
                *) echo "!!! expected $want in relocated pkg-config output" >&2; exit 1 ;;
            esac
        done
        # And it must still actually build and run from the moved tree.
        cc "$CONSUMER_DIR/main.c" $MOVED_FLAGS -o "$CONSUMER_DIR/consumer_moved"
        if [[ "${1:-static}" == "shared" ]]; then
            DYLD_LIBRARY_PATH="$MOVED_DIR/lib" LD_LIBRARY_PATH="$MOVED_DIR/lib" \
                "$CONSUMER_DIR/consumer_moved"
        else
            "$CONSUMER_DIR/consumer_moved"
        fi
    else
        echo "==> pkg-config has no --define-prefix, skipping the relocatability check"
    fi
else
    echo "==> pkg-config not found, skipping that half of the smoke test"
fi

echo "==> Smoke test passed"
