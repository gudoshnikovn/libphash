#!/usr/bin/env bash
# R24: `make coverage` (Makefile) only ever measures the stb_image-only path -- the
# native decoders in src/loaders/{jpeg,png,webp}.c compile down to nothing but their
# ph_can_use_*() stub there, so their max_pixels checks, error-callback plumbing
# (png_error_fn/png_warning_fn + the longjmp that carries libpng's message out,
# spng_strerror() branches) and the pitch/alloc_size overflow guards in jpeg.c are
# never exercised or measured by that flow -- exactly the code R16-R18 lived in.
#
# This script drives two separate CMake+ctest runs with PHASH_COVERAGE=ON:
#   - "native": the default vendored decoder set (TurboJPEG + libpng + libwebp +
#     zlib-ng), i.e. the config CI's build-and-test job and releases actually ship.
#   - "spng": the alternative PNG backend (PHASH_USE_SPNG=ON, PHASH_USE_LIBPNG=OFF)
#     -- a separate run because the two PNG backends are mutually exclusive within
#     one configure and spng's own error-path code is otherwise never measured.
# Each run's lcov trace is filtered to this project's own sources, then the two are
# merged (`lcov -a`) into one report so the published number reflects both.
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

# --ignore-errors mismatch,unused: same rationale/pairs as the Makefile `coverage`
# target -- lcov 2.x otherwise aborts on a handful of harmless version-mismatch and
# zero-hit-file warnings from the vendored decoder trees.
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

    lcov --capture --directory "$build_dir" --output-file "$OUT_DIR/$label.info" \
        --ignore-errors mismatch,mismatch,unused,unused
    lcov --remove "$OUT_DIR/$label.info" '/usr/*' '*/vendor/*' '*/tests/*' \
        --output-file "$OUT_DIR/$label.info" --ignore-errors unused,unused
}

capture_run "$OUT_DIR/build-native" native \
    -DPHASH_USE_TURBOJPEG=ON -DPHASH_USE_LIBPNG=ON -DPHASH_USE_SPNG=OFF \
    -DPHASH_USE_WEBP=ON -DPHASH_USE_ZLIB_NG=ON

capture_run "$OUT_DIR/build-spng" spng \
    -DPHASH_USE_TURBOJPEG=ON -DPHASH_USE_LIBPNG=OFF -DPHASH_USE_SPNG=ON \
    -DPHASH_USE_WEBP=ON -DPHASH_USE_ZLIB_NG=ON

lcov -a "$OUT_DIR/native.info" -a "$OUT_DIR/spng.info" -o "$OUT_DIR/merged.info" \
    --ignore-errors unused,inconsistent
genhtml "$OUT_DIR/merged.info" --output-directory "$OUT_DIR/html"

echo "Coverage report: $OUT_DIR/html/index.html"
lcov --summary "$OUT_DIR/merged.info"
