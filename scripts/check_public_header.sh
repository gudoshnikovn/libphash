#!/usr/bin/env bash
# Checks that the public header, include/libphash.h, stands on its own under a
# given ISO C dialect: a consumer TU that includes nothing else must compile with
# zero warnings, and PH_NODISCARD must still fire on a discarded result.
#
# Why this is separate from check_strict_iso.sh: that script compiles the
# library's own translation units, all of which happen to include libphash.h, so a
# header that breaks under one compiler/dialect pair shows up there as N errors in
# N files -- the class is never named. More importantly, what is under test here is
# the consumer's view: someone compiling their own code with `gcc -std=c23` must be
# able to include the header regardless of how the library itself was built.
#
# The class this guards: a C23 attribute ([[nodiscard]]) must open a declaration,
# before any GNU __attribute__ (PH_API) or type specifier. Clang accepts the
# misplaced form, GCC 14+ rejects it outright -- so a header written and tested on
# clang compiles clean for its author and fails with one error per declaration for
# every GCC consumer on C23, which is GCC 15's default dialect. GCC 13 and earlier
# report __STDC_VERSION__ 202000L under -std=c2x, never reach the [[nodiscard]]
# branch at all, and are blind to the class; run this on GCC >= 14.
#
# The second check matters as much as the first: "attribute ignored" is a warning,
# not an error, so a header can compile while silently losing its nodiscard
# contract. That was the original symptom here, one line above the first error.
#
# Usage: check_public_header.sh [std]     # std: 11 | 17 | 23 (or c11/c17/c23), default 17
#        CC=gcc-14 ./scripts/check_public_header.sh 23
set -euo pipefail

STD="${1:-17}"
STD="c${STD#c}"
CC_BIN="${CC:-cc}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# Same -std=c23 / -std=c2x probe as check_strict_iso.sh: GCC <= 13 only knows the
# latter spelling.
printf 'int main(void){return 0;}\n' > "$WORK_DIR/probe.c"
if ! "$CC_BIN" -std="$STD" -fsyntax-only "$WORK_DIR/probe.c" 2>/dev/null; then
    case "$STD" in
        c23) STD=c2x ;;
    esac
    if ! "$CC_BIN" -std="$STD" -fsyntax-only "$WORK_DIR/probe.c" 2>/dev/null; then
        echo "!!! $CC_BIN does not accept -std=$STD" >&2
        exit 1
    fi
fi

echo "==> Public header check: $CC_BIN -std=$STD"
"$CC_BIN" --version | head -n1

FLAGS=(-std="$STD" -Wall -Wextra -Wpedantic -I "$ROOT_DIR/include")

cat > "$WORK_DIR/standalone.c" <<'EOF'
#include <libphash.h>
int main(void) { return 0; }
EOF

if ! "$CC_BIN" "${FLAGS[@]}" -fsyntax-only -Werror "$WORK_DIR/standalone.c"; then
    echo "!!! include/libphash.h does not compile warning-free on its own as -std=$STD" >&2
    exit 1
fi
echo "    standalone include: clean"

cat > "$WORK_DIR/discard.c" <<'EOF'
#include <libphash.h>
int main(void) {
    ph_context_t *ctx = NULL;
    ph_create(&ctx);
    return 0;
}
EOF

# Compiled to an object, not -fsyntax-only: GCC diagnoses warn_unused_result during
# gimplification, which -fsyntax-only never reaches, so a syntax-only run would
# report the contract inert on every GCC.
if ! diag="$("$CC_BIN" "${FLAGS[@]}" -c -o "$WORK_DIR/discard.o" "$WORK_DIR/discard.c" 2>&1)"; then
    printf '%s\n' "$diag" >&2
    echo "!!! discarded-result TU does not compile as -std=$STD" >&2
    exit 1
fi
if ! printf '%s\n' "$diag" | grep -q 'discard.c:4:[0-9]*: warning:'; then
    printf '%s\n' "$diag" >&2
    echo "!!! PH_NODISCARD is inert as -std=$STD: discarding ph_create()'s result draws no warning" >&2
    exit 1
fi
echo "    discarded ph_create() result: warned"

echo "==> include/libphash.h is self-contained and keeps PH_NODISCARD as -std=$STD"
