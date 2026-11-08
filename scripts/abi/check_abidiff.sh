#!/usr/bin/env bash
# Fails unless the shared library built from the working tree keeps the ABI of the latest
# release tag (or the tag given): both are built here with the same compiler and
# compared by abidiff (libabigail, Linux). Added functions pass; a removed function, a
# changed signature, a changed struct layout or enum value fail. A struct with a
# struct_size field may grow at its end (docs/project/compatibility.md). The tags have
# to be fetched (CI: fetch-depth 0).
#
#   scripts/abi/check_abidiff.sh [tag]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TAG="${1:-$(cd "$ROOT/scripts/docs" && python3 -c 'from check_symbol_versions import latest_tag; print(latest_tag()[0])')}"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# The decoders are behind the public API and do not change it: stb_image only, so
# neither tree needs its submodules.
build() {  # <source dir> <name>
    if ! { cmake -S "$1" -B "$WORK_DIR/build-$2" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
               -DPHASH_BUILD_SHARED=ON -DPHASH_BUILD_TESTS=OFF \
               -DPHASH_USE_LIBJPEG_TURBO=OFF -DPHASH_USE_LIBPNG=OFF -DPHASH_USE_WEBP=OFF \
               -DPHASH_USE_ZLIB_NG=OFF -DCMAKE_INSTALL_PREFIX="$WORK_DIR/$2" &&
           cmake --build "$WORK_DIR/build-$2" -j &&
           cmake --install "$WORK_DIR/build-$2"; } >"$WORK_DIR/$2.log" 2>&1; then
        cat "$WORK_DIR/$2.log" >&2
        echo "check_abidiff: the $2 build failed" >&2
        exit 1
    fi
}

mkdir "$WORK_DIR/src-release"
git -C "$ROOT" archive "$TAG" | tar -x -C "$WORK_DIR/src-release"
build "$WORK_DIR/src-release" release
build "$ROOT" current

cat >"$WORK_DIR/extensible.suppr" <<'EOF'
[suppress_type]
  type_kind = struct
  has_data_member = {struct_size}
  has_data_member_inserted_at = end
  has_size_change = yes
EOF
# ph_digest_t gives two of its reserved bytes a meaning, algorithm and revision, which
# abidiff reports as a changed member. Its size and the offset of every other field stay;
# scripts/abi/check_abi_layout.py checks exactly that, on every platform.
cat >>"$WORK_DIR/extensible.suppr" <<'EOF'

[suppress_type]
  type_kind = struct
  name = ph_digest_t
EOF

lib() { find "$WORK_DIR/$1" -name 'libphash.so.*' -type f | head -n 1; }
if ! abidiff --no-added-syms --suppressions "$WORK_DIR/extensible.suppr" \
        --hd1 "$WORK_DIR/release/include" --hd2 "$WORK_DIR/current/include" \
        "$(lib release)" "$(lib current)"; then
    echo "check_abidiff: libphash breaks the ABI of $TAG (docs/project/compatibility.md)" >&2
    exit 1
fi
echo "check_abidiff: the ABI of $TAG is kept"
