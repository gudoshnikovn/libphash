#!/usr/bin/env bash
# R42: builds and installs the release configuration (the same vendored decoder
# set as CI's build-and-test job -- TurboJPEG + libpng + libwebp + zlib-ng, see
# scripts/coverage_cmake.sh's "native" leg for the same claim) into a throwaway
# prefix, then packs that prefix plus LICENSE/THIRD-PARTY-NOTICES.md into a
# release archive named libphash-<version>-<platform>[-shared].{tar.gz,zip}.
#
# Both linkage kinds are packaged (two separate configure+build+install passes --
# PHASH_BUILD_SHARED is an either/or CMake option, not a knob two targets can
# share in one configure) because the two audiences this task exists for want
# different things: a C/C++ consumer linking in-tree wants the static archive,
# an FFI/ctypes-style consumer (the motivating case in R42's problem statement)
# needs a loadable shared object/dylib/DLL.
#
# Usage: scripts/package_release.sh <platform-name> <out-dir> [static|shared|both]
#   platform-name: e.g. linux-x86_64, macos-arm64, windows-x86_64 -- caller's
#                   choice, just becomes part of the archive file name.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="${1:?usage: package_release.sh <platform-name> <out-dir> [static|shared|both]}"
OUT_DIR="${2:?usage: package_release.sh <platform-name> <out-dir> [static|shared|both]}"
KINDS="${3:-both}"

VERSION=$(sed -nE 's/.*project\([^)]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$ROOT_DIR/CMakeLists.txt" | head -n1)
[ -n "$VERSION" ] || { echo "package_release.sh: could not read version from CMakeLists.txt" >&2; exit 1; }

mkdir -p "$OUT_DIR"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

package_one() {
    local kind="$1" shared_flag="$2"
    local name="libphash-${VERSION}-${PLATFORM}"
    [ "$kind" = "shared" ] && name="${name}-shared"
    local build_dir="$WORK_DIR/build-$kind"
    local stage_dir="$WORK_DIR/stage-$kind/$name"

    echo "==> [$kind] configuring (release decoder set: TurboJPEG+libpng+webp+zlib-ng)"
    cmake -S "$ROOT_DIR" -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$stage_dir" \
        -DPHASH_BUILD_TESTS=OFF \
        -DPHASH_STRICT_DEPS=ON \
        $shared_flag
    cmake --build "$build_dir" --config Release --target phash -j
    cmake --install "$build_dir" --config Release

    cp "$ROOT_DIR/LICENSE" "$stage_dir/"
    cp "$ROOT_DIR/THIRD-PARTY-NOTICES.md" "$stage_dir/"

    mkdir -p "$OUT_DIR"
    if [[ "$PLATFORM" == windows-* ]]; then
        local archive="$OUT_DIR/$name.zip"
        rm -f "$archive"
        if command -v 7z >/dev/null 2>&1; then
            (cd "$WORK_DIR/stage-$kind" && 7z a -tzip "$archive" "$name" >/dev/null)
        else
            (cd "$WORK_DIR/stage-$kind" && zip -qr "$archive" "$name")
        fi
        echo "==> wrote $archive"
    else
        local archive="$OUT_DIR/$name.tar.gz"
        tar -czf "$archive" -C "$WORK_DIR/stage-$kind" "$name"
        echo "==> wrote $archive"
    fi
}

case "$KINDS" in
    static) package_one static "-DPHASH_BUILD_SHARED=OFF" ;;
    shared) package_one shared "-DPHASH_BUILD_SHARED=ON" ;;
    both)
        package_one static "-DPHASH_BUILD_SHARED=OFF"
        package_one shared "-DPHASH_BUILD_SHARED=ON"
        ;;
    *) echo "package_release.sh: unknown kind '$KINDS' (want static|shared|both)" >&2; exit 1 ;;
esac
