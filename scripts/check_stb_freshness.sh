#!/usr/bin/env bash
# Checks whether the two copied-in stb headers (vendor/stb_image.h,
# vendor/stb_image_resize2.h -- not git submodules, so Dependabot can't see them,
# see .github/dependabot.yml and SECURITY.md) are still current against upstream
# nothings/stb. Compares each file's pristine upstream SHA-256, as recorded in
# THIRD-PARTY-NOTICES.md, against the current raw file on upstream's default branch.
#
# Exit 0 and print nothing actionable if both match. Exit 1 with a message per stale
# file otherwise -- the CI workflow that runs this turns that into a tracking issue,
# it does not fail a build (an upstream bump is a deliberate, reviewed task, not
# something to block unrelated PRs on).
set -euo pipefail

NOTICES="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/THIRD-PARTY-NOTICES.md"
STALE=0

check_one() {
    local name="$1" upstream_path="$2"
    # THIRD-PARTY-NOTICES.md records two hashes per file: "as vendored" and
    # "as imported from upstream, before the local patch" -- only the second one is
    # comparable to a fresh upstream download, since both files carry local patches.
    local recorded
    recorded=$(grep -A2 "^\* \*\*Vendored version:\*\*.*\`vendor/${name}\`" "$NOTICES" \
        | grep -oE 'before the local patch: \`[0-9a-f]{64}' \
        | grep -oE '[0-9a-f]{64}$' || true)
    if [ -z "$recorded" ]; then
        echo "check_stb_freshness: could not find a recorded upstream hash for $name in $NOTICES" >&2
        exit 2
    fi

    local current
    current=$(curl -fsSL "https://raw.githubusercontent.com/nothings/stb/master/${upstream_path}" | sha256sum | cut -d' ' -f1)

    if [ "$recorded" != "$current" ]; then
        echo "STALE: $name -- recorded upstream hash $recorded, current upstream hash $current"
        STALE=1
    fi
}

check_one "stb_image.h" "stb_image.h"
check_one "stb_image_resize2.h" "stb_image_resize2.h"

exit $STALE
