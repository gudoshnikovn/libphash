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
# Git Bash on Windows: a native cmake reads /tmp/... as a path on the current drive, so
# every path handed to it is written the mixed way (C:/Users/...), which bash reads too.
ON_WINDOWS=""
if command -v cygpath >/dev/null 2>&1; then
    WORK_DIR="$(cygpath -m "$WORK_DIR")"
    ON_WINDOWS=1
fi

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

# $1: static (default) or shared. $2: the build type, Release by default; a Debug build
# names some vendored archives differently (libpng16d.a), and the installed package and
# .pc have to name the files that are there.
BUILD_TYPE="${2:-Release}"
echo "==> Configuring + building libphash (${1:-static}, $BUILD_TYPE)"
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" -DPHASH_BUILD_TESTS=OFF)
if [[ "${1:-static}" == "shared" ]]; then
    CMAKE_ARGS+=(-DPHASH_BUILD_SHARED=ON)
fi
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"
# --config for a multi-config generator (Visual Studio), ignored by a single-config one.
cmake --build "$BUILD_DIR" --target phash --config "$BUILD_TYPE" -j
cmake --install "$BUILD_DIR" --config "$BUILD_TYPE"

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

# Every -lNAME of the installed .pc has to name a file in lib/, the one a linker looks
# for: libNAME.a (or libNAME.so/.dylib) with a Unix linker, NAME.lib under MSVC (pkgconf
# --msvc-syntax). Read from the file itself, so it holds where no pkg-config or no cc is
# installed, and for the names a Debug build gives the vendored archives.
echo "==> Checking every -l in libphash.pc names an installed file"
libs_line=$(sed -n 's/^Libs: //p' "$PC_FILE")
for flag in $libs_line; do
    case "$flag" in
        -l*) name="${flag#-l}" ;;
        *) continue ;;
    esac
    found=""
    for f in "lib$name.a" "lib$name.so" "lib$name.dylib" "$name.lib"; do
        if [[ -f "$PREFIX_DIR/lib/$f" ]]; then
            found="lib/$f"
        fi
    done
    if [[ -z "$found" ]]; then
        case "$name" in
            m | dl | pthread | z) found="the system's" ;; # z: a build with the system zlib
            *)
                echo "!!! libphash.pc has $flag, but lib/ has no such file:" >&2
                ls "$PREFIX_DIR/lib" >&2
                exit 1
                ;;
        esac
    fi
    echo "    $flag -> $found"
done

echo "==> Building consumer via find_package(phash)"
mkdir -p "$CONSUMER_DIR"
# The consumer is examples/cmake_consumer, the one the README points to.
cp "$ROOT_DIR/examples/cmake_consumer/CMakeLists.txt" "$ROOT_DIR/examples/cmake_consumer/main.c" \
    "$CONSUMER_DIR/"
cmake -S "$CONSUMER_DIR" -B "$CONSUMER_DIR/build" -DCMAKE_PREFIX_PATH="$PREFIX_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$CONSUMER_DIR/build" --config "$BUILD_TYPE" -j
CONSUMER_BIN="$CONSUMER_DIR/build/consumer"
if [[ -f "$CONSUMER_DIR/build/$BUILD_TYPE/consumer.exe" ]]; then
    CONSUMER_BIN="$CONSUMER_DIR/build/$BUILD_TYPE/consumer.exe"
fi
run_consumer "$CONSUMER_BIN" "$PREFIX_DIR/lib"

# Not on Windows: the archives are MSVC's, and a `cc` or `pkg-config` found in Git Bash
# belongs to another toolchain. The -l check above is what covers the .pc there.
if [[ -z "$ON_WINDOWS" ]] && command -v pkg-config >/dev/null 2>&1 &&
    command -v cc >/dev/null 2>&1; then
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
    echo "==> Windows, or no pkg-config or cc: skipping the pkg-config build"
fi

echo "==> Smoke test passed"
