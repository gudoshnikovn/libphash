#!/usr/bin/env bash
# The one definition of what gets formatted and with which clang-format. Both
# `make format` and the CI format-check job call this script, so the two can no
# longer disagree about either.
#
# Why the version is pinned: "the tree is formatted" is only true for a given
# clang-format major -- defaults change between majors (14 and 15 disagree on
# spacing around a binary operator inside a macro, for one), so an unpinned tool
# turns the check red on unchanged code the day a runner image or a developer's
# package manager moves. The major is raised deliberately, in its own commit,
# together with whatever reformatting the new version asks for -- never silently.
#
# Why this perimeter: everything of ours that is C -- the library, its public
# header, all of tests/ (tests/fuzz/ included), examples/, which are the files an
# outside user actually reads, and tools/. vendor/ is outside it by construction.
#
# Usage: scripts/format.sh           # rewrite files in place
#        scripts/format.sh --check   # report a diff and fail, change nothing
#        CLANG_FORMAT=/path/to/clang-format scripts/format.sh
set -euo pipefail

REQUIRED_MAJOR=23
INSTALL_HINT="pip install clang-format==23.1.1"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CF="${CLANG_FORMAT:-clang-format}"

if ! command -v "$CF" >/dev/null 2>&1; then
    echo "!!! $CF not found; install clang-format $REQUIRED_MAJOR: $INSTALL_HINT" >&2
    exit 1
fi

version="$("$CF" --version)"
major="$(printf '%s\n' "$version" | sed -nE 's/.*clang-format version ([0-9]+)\..*/\1/p')"
if [ "$major" != "$REQUIRED_MAJOR" ]; then
    echo "!!! $CF is major ${major:-unknown}, this tree is formatted with $REQUIRED_MAJOR" >&2
    echo "    ($version)" >&2
    echo "    install it with: $INSTALL_HINT" >&2
    exit 1
fi

cd "$ROOT_DIR"
files=()
while IFS= read -r f; do
    files+=("$f")
done < <(find src include tests examples tools -type f \( -name '*.c' -o -name '*.h' \) | sort)

case "${1:-}" in
    --check)
        "$CF" --dry-run --Werror "${files[@]}"
        echo "==> ${#files[@]} files clean under clang-format $major"
        ;;
    "")
        "$CF" -i "${files[@]}"
        ;;
    *)
        echo "usage: $0 [--check]" >&2
        exit 2
        ;;
esac
