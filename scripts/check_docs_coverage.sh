#!/usr/bin/env bash
# Fails (non-zero exit) if any public ph_* symbol in include/libphash.h is not
# mentioned anywhere in docs/*.md, README.md, or MIGRATION.md. Keeps the docs from
# quietly falling behind the API again the way they did before the 2.0.0 backlog --
# see README.md/docs/README.md for what those files cover.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HEADER="$ROOT/include/libphash.h"

# One symbol per PH_API-prefixed declaration line -- the function name immediately
# preceding its opening '('. `ph_error_t`, `ph_digest_t` and the other typedefs are not
# picked up (there is no PH_API line for a typedef), so this only checks functions.
# Not `mapfile` (bash 4+ only) -- the Makefile path is meant to build/run with
# whatever `bash` a contributor has, including macOS's stock bash 3.2.
symbols=$(grep '^PH_API' "$HEADER" | sed -E 's/.*[^a-zA-Z0-9_](ph_[a-zA-Z0-9_]+)[[:space:]]*\(.*/\1/' | sort -u)

DOC_FILES=("$ROOT"/docs/*.md "$ROOT/README.md" "$ROOT/MIGRATION.md")

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

if [ "$missing_count" -gt 0 ]; then
    echo "check_docs_coverage: $missing_count public symbol(s) not documented anywhere in docs/*.md, README.md or MIGRATION.md:" >&2
    printf '%b' "$missing" >&2
    exit 1
fi

echo "check_docs_coverage: all $symbol_count public symbols are documented."
