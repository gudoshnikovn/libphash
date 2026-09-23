#!/usr/bin/env bash
# Syntax-checks every translation unit of the project against a *strict* ISO C
# dialect (-std=cNN, never -std=gnuNN).
#
# Why this exists as its own check and not just as "the build is green": the build
# only proves the dialect on the machine that ran it, and the interesting failure is
# invisible on the machine most of this project is written on. A strict dialect makes
# the compiler define __STRICT_ANSI__; glibc hides every non-ISO declaration behind
# that macro, while Darwin's libc hides nothing. So a file that reaches for M_PI,
# clock_gettime() or openat() compiles clean on macOS and fails to compile on Linux --
# which is exactly how the library spent nine commits unbuildable on glibc without
# anyone noticing. A TU that genuinely needs POSIX asks for it with an explicit
# _POSIX_C_SOURCE (see tests/src/test_benchmark.c); this script is what notices when
# a new one forgets.
#
# Usage: check_strict_iso.sh [std]        # std: 11 | 17 | 23 (or c11/c17/c23), default 17
#        CC=gcc ./scripts/check_strict_iso.sh 23
#
# Implicit function declarations are promoted to errors here on purpose. A hidden
# non-ISO *constant* (M_PI) fails to compile on its own, but a hidden non-ISO
# *function* (mkstemp, fdopen) is merely an implicit declaration -- a warning on
# GCC <= 13 and clang <= 15, a hard error from GCC 14 on. Left at warning level the
# check would call a broken file clean and hand the failure to whoever upgrades the
# compiler next.
#
# Scope note: the loaders are checked in their stb_image-only configuration, since
# that is the one that needs no vendored headers to be present. The native decoder
# backends are compiled under the same strict dialect by every CMake leg that enables
# them, so they are covered by those, not here.
set -euo pipefail

STD="${1:-17}"
STD="c${STD#c}"
CC_BIN="${CC:-cc}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# src/*.c includes phash_version.h, which is generated, not checked in.
mkdir -p "$WORK_DIR/generated"
"$ROOT_DIR/scripts/gen_version.sh" "$ROOT_DIR/CMakeLists.txt" \
    "$ROOT_DIR/include/phash_version.h.in" "$WORK_DIR/generated/phash_version.h"

LIB_FLAGS=(-I "$ROOT_DIR/include" -I "$ROOT_DIR/src" -I "$WORK_DIR/generated")
TEST_FLAGS=("${LIB_FLAGS[@]}" -I "$ROOT_DIR/tests/src"
            "-DTEST_DATA_DIR=\"$ROOT_DIR/tests/data\"" -DPH_TESTING)

# GCC only learned the -std=c23 spelling in 14; 13 and earlier call the same
# dialect -std=c2x. Probe rather than version-sniff, so the check keeps working on
# whatever compiler a runner happens to ship.
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

echo "==> Strict ISO syntax check: $CC_BIN -std=$STD"
"$CC_BIN" --version | head -n1

checked=0
failed=0

check() {
    local src="$1"
    shift
    checked=$((checked + 1))
    if ! "$CC_BIN" -std="$STD" -fsyntax-only -Werror=implicit-function-declaration \
        -Werror=implicit-int "$@" "$src"; then
        echo "!!! ${src#"$ROOT_DIR"/} does not compile as -std=$STD" >&2
        failed=$((failed + 1))
    fi
}

while IFS= read -r src; do
    check "$src" "${LIB_FLAGS[@]}"
done < <(find "$ROOT_DIR/src" -name '*.c' | sort)

while IFS= read -r src; do
    check "$src" "${TEST_FLAGS[@]}"
done < <(find "$ROOT_DIR/tests/src" -name '*.c' | sort)

while IFS= read -r src; do
    check "$src" "${LIB_FLAGS[@]}"
done < <(find "$ROOT_DIR/examples" -name '*.c' | sort)

if [ "$failed" -ne 0 ]; then
    echo "==> $failed of $checked translation units failed -std=$STD" >&2
    exit 1
fi

echo "==> $checked translation units compile as -std=$STD"
