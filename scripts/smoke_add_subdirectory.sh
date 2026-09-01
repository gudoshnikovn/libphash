#!/usr/bin/env bash
# R11/H3 + R51: libphash must not reconfigure a parent project that pulls it in via
# add_subdirectory(), and the result must actually BUILD and RUN -- with the parent
# building static or shared, and across a repeated configure of the same build tree.
#
# R11/H3: the zlib-ng block used to force BUILD_SHARED_LIBS, BUILD_TESTING and
# ZLIB::ZLIB / ZLIB_INCLUDE_DIR / ZLIB_LIBRARY into the cache, silently turning off
# the parent's shared build and its ctest.
#
# R51: the ZLIB_INCLUDE_DIR/ZLIB_LIBRARY pins were still forced into the cache, so on
# the SECOND configure of the same build tree libphash mistook its own pin for "the
# parent brought its own zlib", stood aside, and left libpng linking an absolute path
# to a zlib-ng archive no target produced any more:
#   No rule to make target 'phash_build/vendor/zlib-ng/libz.a', needed by 'parent_app'
# Hence the re-configure step below -- a plain `cmake -S . -B build` re-run, which is
# what every incremental build does. Without it this script has no teeth for R51.
#
# Run from anywhere; exits non-zero on the first violation.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

mkdir -p "$WORK_DIR/parent"
cat > "$WORK_DIR/parent/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.10)
project(parent_project VERSION 1.0.0 LANGUAGES C)

# The parent wants shared libs and its own testing enabled.
enable_testing()

add_subdirectory("$ROOT_DIR" phash_build EXCLUDE_FROM_ALL)

# Assert the parent's own settings survived libphash's configure step.
if(PARENT_EXPECT_SHARED AND NOT BUILD_SHARED_LIBS)
    message(FATAL_ERROR "libphash clobbered BUILD_SHARED_LIBS (now '\${BUILD_SHARED_LIBS}')")
endif()
if(NOT BUILD_TESTING)
    message(FATAL_ERROR "libphash clobbered BUILD_TESTING (now '\${BUILD_TESTING}')")
endif()
message(STATUS "parent: BUILD_SHARED_LIBS=\${BUILD_SHARED_LIBS} BUILD_TESTING=\${BUILD_TESTING}")

add_executable(parent_app main.c)
target_link_libraries(parent_app PRIVATE phash)
EOF

# The app hashes a real PNG, so the vendored libpng/zlib-ng wiring is exercised for
# real rather than just linked: a wrong zlib would fail at compile or decode time.
cat > "$WORK_DIR/parent/main.c" <<'EOF'
#include <libphash.h>
#include <stdio.h>
int main(int argc, char **argv) {
    ph_context_t *ctx = NULL;
    uint64_t hash = 0;
    printf("parent app OK, libphash %s\n", ph_version());
    if (argc < 2) {
        return 0;
    }
    if (ph_create(&ctx) != PH_SUCCESS) {
        fprintf(stderr, "ph_create failed\n");
        return 1;
    }
    if (ph_load_from_file(ctx, argv[1]) != PH_SUCCESS) {
        fprintf(stderr, "ph_load_from_file(%s) failed\n", argv[1]);
        ph_free(ctx);
        return 1;
    }
    if (ph_compute_ahash(ctx, &hash) != PH_SUCCESS) {
        fprintf(stderr, "ph_compute_ahash failed\n");
        ph_free(ctx);
        return 1;
    }
    printf("aHash(%s) = %016llx\n", argv[1], (unsigned long long)hash);
    ph_free(ctx);
    return 0;
}
EOF

# Both parent configurations get the same treatment: configure, check the parent's
# cache survived, RE-configure (the R51 trigger), then build and run.
check_parent() {
    local label="$1" shared="$2" expect_shared="$3"
    local build_dir="$WORK_DIR/build-${label}"

    echo "==> [$label] Configuring a parent with BUILD_SHARED_LIBS=${shared} and BUILD_TESTING=ON"
    cmake -S "$WORK_DIR/parent" -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS="$shared" \
        -DBUILD_TESTING=ON \
        -DPARENT_EXPECT_SHARED="$expect_shared" \
        -DPHASH_BUILD_TESTS=OFF >/dev/null

    echo "==> [$label] Verifying the cache the parent is left with"
    local var value
    for var in BUILD_SHARED_LIBS BUILD_TESTING; do
        value="$(cmake -L "$build_dir" 2>/dev/null | sed -n "s/^${var}:BOOL=//p" | head -n1)"
        echo "    ${var}=${value}"
    done
    value="$(cmake -L "$build_dir" 2>/dev/null | sed -n "s/^BUILD_TESTING:BOOL=//p" | head -n1)"
    if [[ "$value" != "ON" && "$value" != "1" && "$value" != "TRUE" ]]; then
        echo "!!! libphash left BUILD_TESTING=${value} in the parent's cache; expected ON" >&2
        exit 1
    fi
    value="$(cmake -L "$build_dir" 2>/dev/null | sed -n "s/^BUILD_SHARED_LIBS:BOOL=//p" | head -n1)"
    if [[ "$value" != "$shared" ]]; then
        echo "!!! libphash left BUILD_SHARED_LIBS=${value} in the parent's cache; expected ${shared}" >&2
        exit 1
    fi

    # R51: libphash must not leave its zlib pins in the parent's cache -- that is
    # exactly what made the second configure stand down and break the link.
    for var in ZLIB_LIBRARY ZLIB_INCLUDE_DIR; do
        if cmake -L "$build_dir" 2>/dev/null | grep -q "^${var}:"; then
            echo "!!! libphash left ${var} in the parent's cache:" >&2
            cmake -L "$build_dir" 2>/dev/null | grep "^${var}:" >&2
            exit 1
        fi
    done

    echo "==> [$label] Re-configuring the same build tree (incremental configure)"
    cmake -S "$WORK_DIR/parent" -B "$build_dir" >/dev/null

    echo "==> [$label] Building and running the parent end to end"
    cmake --build "$build_dir" --target parent_app -j
    local out
    out="$("$build_dir/parent_app" "$ROOT_DIR/tests/data/photo_complex.png")"
    echo "$out"
    # The expected value is photo_complex.png's golden aHash: a real PNG really went
    # through the vendored libpng + zlib-ng in the parent's build tree.
    if [[ "$out" != *"= 000001071f7fffff"* ]]; then
        echo "!!! parent app produced an unexpected aHash for photo_complex.png" >&2
        exit 1
    fi
}

check_parent shared ON ON
check_parent static OFF OFF

echo "==> add_subdirectory() smoke test passed"
