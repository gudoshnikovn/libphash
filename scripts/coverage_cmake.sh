#!/usr/bin/env bash
# `make coverage` (Makefile) measures only the stb_image-only path -- the
# native decoders in src/loaders/ compile to nothing or are not built there, so their
# max_pixels checks, error-callback plumbing (png_error_fn/png_warning_fn + the
# longjmp that carries libpng's message out) and the pitch/alloc_size overflow guards
# in jpeg.c are never exercised or measured by that flow.
#
# This script runs CMake+ctest with PHASH_COVERAGE=ON on the default vendored decoder
# set (libjpeg-turbo + libpng + libwebp + zlib-ng), i.e. the config CI's
# build-and-test job and releases actually ship, filters the lcov trace to this
# project's own sources and renders it.
#
# Usage: scripts/coverage_cmake.sh [output_dir]
# Requires: cmake, ctest, lcov, genhtml.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

OUT_DIR="${1:-docs/coverage/cmake}"
mkdir -p "$OUT_DIR"

for tool in cmake ctest lcov genhtml; do
    command -v "$tool" >/dev/null || {
        echo "$tool is required" >&2
        exit 1
    }
done

# Branches as well as lines, with the same suppressions as the Makefile `coverage`
# target (see LCOV_BRANCH_FLAGS there for what each one is for).
LCOV_FLAGS=(--rc branch_coverage=1
    --ignore-errors "mismatch,mismatch,unused,unused,inconsistent,inconsistent,unsupported,unsupported")

capture_run() {
    local build_dir="$1" label="$2"
    shift 2

    rm -rf "$build_dir"
    cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug -DPHASH_BUILD_TESTS=ON \
        -DPHASH_COVERAGE=ON "$@"
    cmake --build "$build_dir" -j
    # Coverage is a measurement pass, not a correctness gate (same stance as the
    # Makefile's `coverage` target) -- a failing test still ran its lines, and this
    # script's job is to report what got exercised, not to re-litigate `make test`/
    # `ctest` on their own. Warn instead of aborting so one flaky/environment-gapped
    # test (e.g. a golden-hash fixture missing for this decoder combo) doesn't blank
    # out the whole report.
    (cd "$build_dir" && ctest --output-on-failure) || echo "WARNING: ctest reported failures in the '$label' run -- see above" >&2

    lcov --capture --directory "$build_dir" --output-file "$OUT_DIR/$label.info" "${LCOV_FLAGS[@]}"
    lcov --remove "$OUT_DIR/$label.info" '/usr/*' '*/vendor/*' '*/tests/*' \
        --output-file "$OUT_DIR/$label.info" "${LCOV_FLAGS[@]}"
}

capture_run "$OUT_DIR/build-native" native \
    -DPHASH_USE_LIBJPEG_TURBO=ON -DPHASH_USE_LIBPNG=ON \
    -DPHASH_USE_WEBP=ON -DPHASH_USE_ZLIB_NG=ON

genhtml "$OUT_DIR/native.info" --output-directory "$OUT_DIR/html" "${LCOV_FLAGS[@]}"

echo "Coverage report: $OUT_DIR/html/index.html"
python3 scripts/check_coverage.py --report "$OUT_DIR/native.info"
