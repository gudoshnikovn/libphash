#!/usr/bin/env bash
# Checks that each vendored submodule (.gitmodules) sits exactly on an upstream release
# tag, and that the tag is the newest release upstream has cut. A submodule is pinned to
# a release, never to a commit between releases: a release is what upstream tested and
# announced, and the version THIRD-PARTY-NOTICES.md names. Dependabot cannot keep that
# rule -- its gitsubmodule updates follow a branch's newest commit, not its tags -- so
# this script is the monitor instead, run on a schedule by
# .github/workflows/submodule-tags-check.yml.
#
# A release tag is a plain version, optionally with a leading "v": 3.2.0, v1.6.59,
# 2.3.3. Anything else (v1.6.0-rc1, 2.3.0-beta1) is a pre-release, and so is a version
# with a component of 90 or more, which is how libjpeg-turbo numbers its betas (3.1.90).
#
# Reads the pinned commits from the superproject's tree (git ls-tree), so the
# submodules need not be checked out, and the tags from upstream (git ls-remote).
#
# Exit status is a three-way contract the CI workflow relies on:
#   0  every submodule is on its upstream's newest release tag;
#   1  at least one is behind or not on a release tag -- one "STALE:" or "UNTAGGED:"
#      line per submodule on stdout; the workflow turns this into a tracking issue,
#      since a bump is a reviewed task, not something to block unrelated pushes on;
#   2  the check itself could not run (no .gitmodules, upstream unreachable) -- the
#      workflow fails, because a monitor that cannot run must not look the same as one
#      that found nothing.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STATUS=0

fail() {
    echo "check_submodule_tags: $*" >&2
    exit 2
}

is_release() {
    local v="${1#v}"
    [[ "$v" =~ ^[0-9]+(\.[0-9]+)+$ ]] || return 1
    local part
    for part in ${v//./ }; do
        [ "$part" -lt 90 ] || return 1
    done
}

[ -f "$ROOT/.gitmodules" ] || fail "no .gitmodules in $ROOT"
paths=$(git -C "$ROOT" config -f .gitmodules --get-regexp '^submodule\..*\.path$' | cut -d' ' -f2) \
    || fail "could not read the submodule paths from .gitmodules"
[ -n "$paths" ] || fail ".gitmodules lists no submodule"

for path in $paths; do
    name=$(git -C "$ROOT" config -f .gitmodules --get-regexp '^submodule\..*\.path$' \
        | awk -v p="$path" '$2 == p { sub(/^submodule\./, "", $1); sub(/\.path$/, "", $1); print $1 }')
    url=$(git -C "$ROOT" config -f .gitmodules --get "submodule.${name}.url") \
        || fail "no url for submodule $name"
    pinned=$(git -C "$ROOT" ls-tree HEAD "$path" | awk '{ print $3 }')
    [ -n "$pinned" ] || fail "$path is not a submodule in HEAD"

    # "<sha> refs/tags/<tag>" lines; an annotated tag also has "<sha> refs/tags/<tag>^{}"
    # carrying the commit it points at, which is the one to compare with.
    refs=$(git ls-remote --tags "$url") || fail "could not list the tags of $url"
    [ -n "$refs" ] || fail "$url has no tags"
    commits=$(printf '%s\n' "$refs" | awk '
        { sha = $1; tag = $2; sub(/^refs\/tags\//, "", tag) }
        tag ~ /\^\{\}$/ { sub(/\^\{\}$/, "", tag); peeled[tag] = sha; next }
        { direct[tag] = sha }
        END { for (t in direct) print t, (t in peeled ? peeled[t] : direct[t]) }')

    releases=""
    pinned_tag=""
    while read -r tag sha; do
        is_release "$tag" || continue
        releases+="$tag"$'\n'
        [ "$sha" = "$pinned" ] && pinned_tag="$tag"
    done <<< "$commits"
    [ -n "$releases" ] || fail "$url has no release tag"
    latest=$(printf '%s' "$releases" | sed 's/^v//' | sort -V | tail -n 1)

    if [ -z "$pinned_tag" ]; then
        echo "UNTAGGED: $path is at $pinned, which no release tag of $url names; newest release $latest"
        STATUS=1
    elif [ "${pinned_tag#v}" != "$latest" ]; then
        echo "STALE: $path is at $pinned_tag; newest release of $url is $latest"
        STATUS=1
    else
        echo "ok: $path is at $pinned_tag, the newest release"
    fi
done

exit "$STATUS"
