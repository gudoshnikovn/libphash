#!/usr/bin/env bash
# Checks that each copied-in stb header is exactly "upstream file at the recorded commit +
# the patch in vendor/patches/": downloads the upstream file at the commit recorded in
# THIRD-PARTY-NOTICES.md, requires its SHA-256 to be the recorded upstream hash, applies
# vendor/patches/<file>.patch, and requires the result to be vendor/<file> byte for byte,
# with the recorded vendored hash.
#
# A bump of either header therefore cannot drop the local patch silently, and the patch
# in the tree is the one that is actually applied: a vendored file edited without its
# patch, or a patch edited without the file, fails here.
#
# Exit status: 0 all match; 1 a mismatch (named on stderr); 2 the check could not run
# (no recorded commit or hash, download failed, a tool missing).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NOTICES="$ROOT/THIRD-PARTY-NOTICES.md"
UPSTREAM="https://raw.githubusercontent.com/nothings/stb"
WORK=$(mktemp -d) || { echo "check_stb_patches: mktemp failed" >&2; exit 2; }
trap 'rm -rf "$WORK"' EXIT
STATUS=0

cannot_run() {
    echo "check_stb_patches: $*" >&2
    exit 2
}

if command -v sha256sum > /dev/null 2>&1; then
    sha256() { sha256sum "$1" | cut -d' ' -f1; }
elif command -v shasum > /dev/null 2>&1; then
    sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
else
    cannot_run "neither sha256sum nor shasum is installed"
fi
command -v patch > /dev/null 2>&1 || cannot_run "patch is not installed"

check_one() {
    local name="$1" line commit upstream_hash vendored_hash
    line=$(grep "^\* \*\*Vendored version:\*\*.*\`vendor/${name}\`" "$NOTICES")
    vendored_hash=$(sed -n "s/.*\`vendor\/${name}\`, SHA-256 \`\([0-9a-f]\{64\}\)\`.*/\1/p" <<<"$line")
    commit=$(sed -n 's/.*upstream commit `\([0-9a-f]\{40\}\)`.*/\1/p' <<<"$line")
    upstream_hash=$(sed -n 's/.*before the local patch: `\([0-9a-f]\{64\}\)`.*/\1/p' <<<"$line")
    [ -n "$vendored_hash" ] && [ -n "$commit" ] && [ -n "$upstream_hash" ] \
        || cannot_run "no vendored hash, upstream commit and upstream hash for $name in $NOTICES"

    mkdir -p "$WORK/$name.d"
    curl -fsSL --retry 3 --max-time 60 -o "$WORK/$name.d/$name" "$UPSTREAM/$commit/$name" \
        || cannot_run "could not download $UPSTREAM/$commit/$name"

    local got
    got=$(sha256 "$WORK/$name.d/$name")
    if [ "$got" != "$upstream_hash" ]; then
        echo "check_stb_patches: upstream $name at $commit has SHA-256 $got, THIRD-PARTY-NOTICES.md records $upstream_hash" >&2
        STATUS=1
        return
    fi
    if ! (cd "$WORK/$name.d" && patch -p1 -s --no-backup-if-mismatch < "$ROOT/vendor/patches/$name.patch"); then
        echo "check_stb_patches: vendor/patches/$name.patch does not apply to upstream $name at $commit" >&2
        STATUS=1
        return
    fi
    if ! cmp -s "$WORK/$name.d/$name" "$ROOT/vendor/$name"; then
        echo "check_stb_patches: upstream $name at $commit + vendor/patches/$name.patch is not vendor/$name" >&2
        STATUS=1
        return
    fi
    got=$(sha256 "$ROOT/vendor/$name")
    if [ "$got" != "$vendored_hash" ]; then
        echo "check_stb_patches: vendor/$name has SHA-256 $got, THIRD-PARTY-NOTICES.md records $vendored_hash" >&2
        STATUS=1
        return
    fi
    echo "check_stb_patches: vendor/$name = upstream $commit + vendor/patches/$name.patch"
}

check_one "stb_image.h"
check_one "stb_image_resize2.h"
exit $STATUS
