#!/usr/bin/env bash
# Fails (non-zero exit) if any public ph_* function in include/libphash.h is not
# mentioned anywhere in docs/*.md, docs/theory/*.md, docs/guide/*.md, README.md, or
# MIGRATION.md, or if a PHASH_* option() in CMakeLists.txt has no row in the build-flow
# table of docs/development.md. Keeps the docs from quietly falling behind the API and the
# build -- see README.md/docs/README.md for what those files cover.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HEADER="$ROOT/include/libphash.h"
DEV_DOC="$ROOT/docs/development.md"

# One symbol per declaration line that starts with PH_API, optionally after other PH_*
# attribute macros (`PH_NODISCARD PH_API ...`): the function name immediately preceding
# its opening '('. Any leading PH_* macro is accepted, so a new attribute macro cannot
# hide declarations from the check. `ph_error_t`, `ph_digest_t` and the other typedefs
# are not picked up (there is no PH_API line for a typedef), so this only checks
# functions. Not `mapfile` (bash 4+ only) -- this script must run under whatever `bash`
# a contributor has, including macOS's stock bash 3.2.
decl_lines=$(grep -E '^(PH_[A-Z_]+[[:space:]]+)*PH_API[[:space:]]' "$HEADER")
symbols=$(printf '%s\n' "$decl_lines" | sed -E 's/.*[^a-zA-Z0-9_](ph_[a-zA-Z0-9_]+)[[:space:]]*\(.*/\1/' | sort -u)

# Parser self-check: every PH_API declaration line yields exactly one ph_ name.
decl_count=$(printf '%s\n' "$decl_lines" | grep -c . || true)
parsed_count=$(printf '%s\n' "$symbols" | grep -c '^ph_[a-zA-Z0-9_]*$' || true)
if [ "$parsed_count" -ne "$decl_count" ]; then
    echo "check_docs_coverage: $decl_count PH_API declarations in include/libphash.h, but $parsed_count function names parsed from them" >&2
    exit 1
fi

# The pages written by hand. Not docs/api/: it is generated from the header itself, so a
# function found there would say nothing about whether anything explains it.
DOC_FILES=("$ROOT"/docs/*.md "$ROOT"/docs/theory/*.md "$ROOT"/docs/guide/*.md "$ROOT/README.md"
    "$ROOT/MIGRATION.md")

missing=""
missing_count=0
symbol_count=0
while IFS= read -r sym; do
    [ -z "$sym" ] && continue
    symbol_count=$((symbol_count + 1))
    if ! grep -qF "$sym" "${DOC_FILES[@]}" 2>/dev/null; then
        missing="${missing}  - ${sym}\n"
        missing_count=$((missing_count + 1))
    fi
done <<EOF
$symbols
EOF

# The build-flow table -- the one whose header row starts `| Knob |` -- writes each CMake
# option as `PHASH_NAME=<default>`. Only that table counts: the CI matrix table names
# options too.
flow_table=$(awk '/^\| Knob \|/{t=1} t&&!/^\|/{exit} t' "$DEV_DOC")
options=$(grep -E '^[[:space:]]*option\(PHASH_[A-Z0-9_]+' "$ROOT/CMakeLists.txt" |
    sed -E 's/^[[:space:]]*option\((PHASH_[A-Z0-9_]+).*/\1/' | sort -u)
option_count=0
while IFS= read -r opt; do
    [ -z "$opt" ] && continue
    option_count=$((option_count + 1))
    if ! printf '%s\n' "$flow_table" | grep -qE "\`${opt}="; then
        missing="${missing}  - ${opt} (no row in the build-flow table of docs/development.md)\n"
        missing_count=$((missing_count + 1))
    fi
done <<EOF
$options
EOF

if [ "$missing_count" -gt 0 ]; then
    echo "check_docs_coverage: $missing_count item(s) missing from the docs (functions are looked up in docs/*.md, docs/theory/*.md, docs/guide/*.md, README.md and MIGRATION.md):" >&2
    printf '%b' "$missing" >&2
    exit 1
fi

echo "check_docs_coverage: all $symbol_count public functions are documented; all $option_count PHASH_* options have a build-flow row."
