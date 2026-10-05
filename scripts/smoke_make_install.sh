#!/usr/bin/env bash
# Smoke test for the Makefile's install/uninstall: installs into a throwaway prefix,
# builds and runs a consumer with the plain `pkg-config --cflags --libs libphash` README
# shows, moves the prefix and repeats the build from there, then uninstalls and checks
# that no file is left behind. Builds libphash.a in the checkout, like `make` does.
#
#   scripts/smoke_make_install.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
PREFIX_DIR="$WORK_DIR/prefix"
MAKE="${MAKE:-make}"

echo "==> make install PREFIX=$PREFIX_DIR"
"$MAKE" -C "$ROOT_DIR" -j libphash.a >/dev/null
"$MAKE" -C "$ROOT_DIR" install PREFIX="$PREFIX_DIR" >/dev/null
for f in lib/libphash.a include/libphash.h include/phash_version.h lib/pkgconfig/libphash.pc; do
    [ -f "$PREFIX_DIR/$f" ] || { echo "!!! make install did not write $f" >&2; exit 1; }
done
grep -q '^prefix=${pcfiledir}/' "$PREFIX_DIR/lib/pkgconfig/libphash.pc" ||
    { echo "!!! libphash.pc prefix is not relative to \${pcfiledir}" >&2; exit 1; }

cat > "$WORK_DIR/main.c" <<'C'
#include <libphash.h>
#include <stdio.h>
int main(void) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) { fprintf(stderr, "ph_create failed\n"); return 1; }
    printf("libphash make install smoke test OK (version %s)\n", ph_version());
    ph_free(ctx);
    return 0;
}
C

build_and_run() {
    local prefix="$1" flags
    flags=$(PKG_CONFIG_PATH="$prefix/lib/pkgconfig" pkg-config --cflags --libs libphash)
    echo "    pkg-config: $flags"
    # shellcheck disable=SC2086 # the flags are a list of words
    cc "$WORK_DIR/main.c" $flags -o "$WORK_DIR/consumer"
    "$WORK_DIR/consumer"
}

if command -v pkg-config >/dev/null 2>&1; then
    echo "==> Building a consumer via pkg-config"
    build_and_run "$PREFIX_DIR"
    MOVED_DIR="$WORK_DIR/moved-prefix"
    mv "$PREFIX_DIR" "$MOVED_DIR"
    echo "==> Building the consumer from the moved prefix"
    build_and_run "$MOVED_DIR"
    mv "$MOVED_DIR" "$PREFIX_DIR"
else
    echo "==> pkg-config not found, skipping the consumer"
fi

echo "==> make uninstall"
"$MAKE" -C "$ROOT_DIR" uninstall PREFIX="$PREFIX_DIR" >/dev/null
left=$(find "$PREFIX_DIR" -type f)
[ -z "$left" ] || { echo "!!! make uninstall left files behind:" >&2; echo "$left" >&2; exit 1; }

echo "==> Smoke test passed"
