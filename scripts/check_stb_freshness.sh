#!/usr/bin/env bash
# Checks whether the two copied-in stb headers (vendor/stb_image.h,
# vendor/stb_image_resize2.h -- not git submodules, so scripts/check_submodule_tags.sh
# does not see them, see SECURITY.md) are still current against upstream
# nothings/stb. Compares each file's pristine upstream SHA-256, as recorded in
# THIRD-PARTY-NOTICES.md, against the current raw file on upstream's default branch.
#
# Exit status is a three-way contract the CI workflow relies on:
#   0  both files match upstream;
#   1  at least one file is stale -- one "STALE:" line per file on stdout; the
#      workflow turns this into a tracking issue rather than a failed build (an
#      upstream bump is a deliberate, reviewed task, not something to block
#      unrelated PRs on);
#   2  the check itself could not run (no recorded hash, download failed, no
#      SHA-256 tool) -- the workflow fails, because a monitor that cannot run
#      must not look the same as one that found nothing.
set -uo pipefail

NOTICES="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/THIRD-PARTY-NOTICES.md"
UPSTREAM="https://raw.githubusercontent.com/nothings/stb/master"
STALE=0
TMP=$(mktemp) || { echo "check_stb_freshness: mktemp failed" >&2; exit 2; }
trap 'rm -f "$TMP"' EXIT

fail() {
    echo "check_stb_freshness: $*" >&2
    exit 2
}

# sha256sum is GNU coreutils; macOS ships shasum instead.
if command -v sha256sum > /dev/null 2>&1; then
    sha256() { sha256sum | cut -d' ' -f1; }
elif command -v shasum > /dev/null 2>&1; then
    sha256() { shasum -a 256 | cut -d' ' -f1; }
else
    fail "neither sha256sum nor shasum is installed"
fi

check_one() {
    local name="$1"
    # THIRD-PARTY-NOTICES.md records two hashes per file: "as vendored" and
    # "as imported from upstream, before the local patch" -- only the second one is
    # comparable to a fresh upstream download, since both files carry local patches.
    # The backtick in the second pattern stays unescaped: GNU grep reads \` as the
    # start-of-buffer anchor, BSD grep as a literal backtick.
    local recorded
    recorded=$(grep -A2 "^\* \*\*Vendored version:\*\*.*\`vendor/${name}\`" "$NOTICES" \
        | grep -oE 'before the local patch: `[0-9a-f]{64}' \
        | grep -oE '[0-9a-f]{64}$')
    [ -n "$recorded" ] || fail "could not find a recorded upstream hash for $name in $NOTICES"

    local current
    curl -fsSL --retry 3 --max-time 60 -o "$TMP" "${UPSTREAM}/${name}" \
        || fail "could not download ${UPSTREAM}/${name}"
    [ -s "$TMP" ] || fail "${UPSTREAM}/${name} is empty"
    current=$(sha256 < "$TMP") || fail "could not hash ${name}"

    if [ "$recorded" != "$current" ]; then
        echo "STALE: $name -- recorded upstream hash $recorded, current upstream hash $current"
        STALE=1
    fi
}

check_one "stb_image.h"
check_one "stb_image_resize2.h"

exit $STALE
