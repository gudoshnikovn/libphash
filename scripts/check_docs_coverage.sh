#!/usr/bin/env bash
# Fails (non-zero exit) if any public ph_* function in include/libphash.h is not
# mentioned anywhere in docs/*.md, docs/theory/*.md, docs/guide/*.md, README.md, or
# MIGRATION.md, if a PHASH_* option() in CMakeLists.txt has no row in the build-flow
# table of docs/development.md, if a ph_context_set_* function has no row in the
# settings tables of docs/guide/configuring.md, or if a ph_error_t failure code has no
# row in the codes table of docs/guide/errors.md. Keeps the docs from quietly falling
# behind the API and the build -- see README.md/docs/README.md for what those files
# cover.
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

# The settings tables of docs/guide/configuring.md -- those whose header row starts
# `| Setter |` -- have one row per ph_context_set_* function between them, and no row
# for a function the header does not declare.
CONFIG_DOC="$ROOT/docs/guide/configuring.md"
settings_table=$(awk '/^\| Setter \|/{t=1} t&&!/^\|/{t=0} t' "$CONFIG_DOC")
table_setters=$(printf '%s\n' "$settings_table" | sed -nE 's/^\| \[`(ph_context_set_[a-z0-9_]+)\(\)`\].*/\1/p' | sort)
setters=$(printf '%s\n' "$symbols" | grep '^ph_context_set_' || true)
setter_count=$(printf '%s\n' "$setters" | grep -c . || true)
while IFS= read -r sym; do
    [ -z "$sym" ] && continue
    if ! printf '%s\n' "$table_setters" | grep -qx "$sym"; then
        missing="${missing}  - ${sym} (no row in the settings tables of docs/guide/configuring.md)\n"
        missing_count=$((missing_count + 1))
    fi
done <<EOF
$setters
EOF
while IFS= read -r sym; do
    [ -z "$sym" ] && continue
    if ! printf '%s\n' "$setters" | grep -qx "$sym"; then
        missing="${missing}  - ${sym} (a row in the settings tables of docs/guide/configuring.md, but no such setter in include/libphash.h)\n"
        missing_count=$((missing_count + 1))
    fi
done <<EOF
$table_setters
EOF
for sym in $(printf '%s\n' "$table_setters" | uniq -d); do
    missing="${missing}  - ${sym} (two rows in the settings tables of docs/guide/configuring.md)\n"
    missing_count=$((missing_count + 1))
done

# The codes table of docs/guide/errors.md -- the one whose header row starts `| Code |
# Value |` -- has one row per failure code of ph_error_t, each with the value the header
# assigns it.
ERRORS_DOC="$ROOT/docs/guide/errors.md"
codes_table=$(awk '/^\| Code \| Value \|/{t=1} t&&!/^\|/{t=0} t' "$ERRORS_DOC")
table_codes=$(printf '%s\n' "$codes_table" | sed -nE 's/^\| \[`(PH_ERR_[A-Z_]+)`\][^|]*\| (-[0-9]+) \|.*/\1 \2/p' | sort)
codes=$(sed -nE 's/^[[:space:]]*(PH_ERR_[A-Z_]+)[[:space:]]*=[[:space:]]*(-[0-9]+),.*/\1 \2/p' "$HEADER" | sort)
code_count=$(printf '%s\n' "$codes" | grep -c . || true)
if [ "$code_count" -eq 0 ]; then
    echo "check_docs_coverage: no PH_ERR_* = -N values parsed from include/libphash.h" >&2
    exit 1
fi
while IFS= read -r code; do
    [ -z "$code" ] && continue
    if ! printf '%s\n' "$table_codes" | grep -qx "$code"; then
        missing="${missing}  - ${code% *} = ${code#* } (no row with this value in the codes table of docs/guide/errors.md)\n"
        missing_count=$((missing_count + 1))
    fi
done <<EOF
$codes
EOF
while IFS= read -r code; do
    [ -z "$code" ] && continue
    if ! printf '%s\n' "$codes" | grep -qx "$code"; then
        missing="${missing}  - ${code% *} = ${code#* } (a row in the codes table of docs/guide/errors.md, but no such code in include/libphash.h)\n"
        missing_count=$((missing_count + 1))
    fi
done <<EOF
$table_codes
EOF
for code in $(printf '%s\n' "$table_codes" | cut -d' ' -f1 | uniq -d); do
    missing="${missing}  - ${code} (two rows in the codes table of docs/guide/errors.md)\n"
    missing_count=$((missing_count + 1))
done

if [ "$missing_count" -gt 0 ]; then
    echo "check_docs_coverage: $missing_count item(s) missing from the docs (functions are looked up in docs/*.md, docs/theory/*.md, docs/guide/*.md, README.md and MIGRATION.md):" >&2
    printf '%b' "$missing" >&2
    exit 1
fi

echo "check_docs_coverage: all $symbol_count public functions are documented; all $option_count PHASH_* options have a build-flow row; all $setter_count setters have a row in the settings tables; all $code_count error codes have a row in the codes table."
