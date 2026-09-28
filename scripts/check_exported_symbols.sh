#!/usr/bin/env bash
# Fails (non-zero exit) unless a shared libphash exports exactly the functions
# include/libphash.h declares PH_API: no internal helper, no stb_image symbol, no symbol
# of a vendored decoder. A PH_API function missing from the export table fails too --
# a consumer would get an unresolved symbol.
#
#   scripts/check_exported_symbols.sh                    # builds a shared libphash first
#   scripts/check_exported_symbols.sh -DPHASH_USE_WEBP=OFF   # ... with extra CMake options
#   scripts/check_exported_symbols.sh --lib path/to/libphash.so
#
# Linux (ELF) and macOS (Mach-O). Windows exports only __declspec(dllexport), which is
# PH_API itself, so there is nothing to check there.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HEADER="$ROOT/include/libphash.h"

LIB=""
if [ "${1:-}" = "--lib" ]; then
    LIB="${2:?--lib needs a path}"
else
    WORK_DIR="$(mktemp -d)"
    trap 'rm -rf "$WORK_DIR"' EXIT
    echo "==> Building a shared libphash ($*)"
    cmake -S "$ROOT" -B "$WORK_DIR" -DCMAKE_BUILD_TYPE=Release -DPHASH_BUILD_SHARED=ON \
        -DPHASH_BUILD_TESTS=OFF "$@" >/dev/null
    cmake --build "$WORK_DIR" --target phash -j >/dev/null
    LIB="$(find "$WORK_DIR" -maxdepth 1 \( -name 'libphash.so.*.*' -o -name 'libphash.*.dylib' \) | head -1)"
    [ -n "$LIB" ] || LIB="$(find "$WORK_DIR" -maxdepth 1 \( -name 'libphash.so' -o -name 'libphash.dylib' \) | head -1)"
fi
[ -f "$LIB" ] || { echo "check_exported_symbols: no shared library at '$LIB'" >&2; exit 1; }

# Every function include/libphash.h declares: the name before the '(' on each line that
# starts a declaration at file scope. Deliberately not "every line with PH_API": a
# function that lost PH_API on both its declaration and its definition is hidden, and
# must show up here as missing rather than drop out of the expected set with it.
expected=$(grep -E '^[A-Za-z_][A-Za-z0-9_ *]*[ *]ph_[a-z0-9_]+\(' "$HEADER" \
    | sed -E 's/.*[^a-zA-Z0-9_](ph_[a-zA-Z0-9_]+)\(.*/\1/' | sort -u)
if ! grep -E '^[A-Za-z_][A-Za-z0-9_ *]*[ *]ph_[a-z0-9_]+\(' "$HEADER" | grep -qv PH_API; then :; else
    echo "check_exported_symbols: public declarations without PH_API:" >&2
    grep -E '^[A-Za-z_][A-Za-z0-9_ *]*[ *]ph_[a-z0-9_]+\(' "$HEADER" | grep -v PH_API | sed 's/^/  ! /' >&2
    exit 1
fi

case "$(uname -s)" in
    Darwin) actual=$(nm -gU "$LIB" | awk '{print $3}' | sed 's/^_//' | sort -u) ;;
    *) actual=$(nm -D --defined-only "$LIB" | awk '$2 ~ /^[TDBRVWiu]$/ {print $3}' \
           | sed 's/@.*//' | sort -u) ;;
esac

extra=$(comm -13 <(echo "$expected") <(echo "$actual"))
missing=$(comm -23 <(echo "$expected") <(echo "$actual"))

status=0
if [ -n "$extra" ]; then
    echo "check_exported_symbols: $(echo "$extra" | wc -l | tr -d ' ') symbol(s) exported but not declared in include/libphash.h:" >&2
    echo "$extra" | sed 's/^/  + /' >&2
    status=1
fi
if [ -n "$missing" ]; then
    echo "check_exported_symbols: $(echo "$missing" | wc -l | tr -d ' ') function(s) of include/libphash.h not exported:" >&2
    echo "$missing" | sed 's/^/  - /' >&2
    status=1
fi
[ "$status" -eq 0 ] && echo "check_exported_symbols: $(basename "$LIB") exports exactly the $(echo "$expected" | wc -l | tr -d ' ') functions of include/libphash.h."
exit "$status"
