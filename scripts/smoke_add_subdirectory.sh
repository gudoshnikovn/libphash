#!/usr/bin/env bash
# R11/H3: libphash must not reconfigure a parent project that pulls it in via
# add_subdirectory(). The zlib-ng block used to force BUILD_SHARED_LIBS,
# BUILD_TESTING and ZLIB::ZLIB / ZLIB_INCLUDE_DIR / ZLIB_LIBRARY into the cache,
# silently turning off the parent's shared build and its ctest.
#
# This checks the parent's settings SURVIVE, which a build of libphash alone
# cannot show. Run from anywhere; exits non-zero on the first violation.
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

cat > "$WORK_DIR/parent/main.c" <<'EOF'
#include <libphash.h>
#include <stdio.h>
int main(void) {
    printf("parent app OK, libphash %s\n", ph_version());
    return 0;
}
EOF

# Part 1 -- the H3 claim itself: a parent that asked for a shared build and for testing
# must still have both after libphash has configured. This is configure-time only;
# building the vendored codecs into a *shared* parent is a separate gap, tracked as R51.
echo "==> [1/2] Configuring a parent with BUILD_SHARED_LIBS=ON and BUILD_TESTING=ON"
cmake -S "$WORK_DIR/parent" -B "$WORK_DIR/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_TESTING=ON \
    -DPARENT_EXPECT_SHARED=ON \
    -DPHASH_BUILD_TESTS=OFF

echo "==> Verifying the cache the parent is left with"
for var in BUILD_SHARED_LIBS BUILD_TESTING; do
    value="$(cmake -L "$WORK_DIR/build" 2>/dev/null | sed -n "s/^${var}:BOOL=//p" | head -n1)"
    echo "    ${var}=${value}"
    if [[ "$value" != "ON" && "$value" != "1" && "$value" != "TRUE" ]]; then
        echo "!!! libphash left ${var}=${value} in the parent's cache; expected ON" >&2
        exit 1
    fi
done

# Part 2 -- a static parent (the common case) must build and run against libphash.
# BUILD_TESTING=ON is still asserted inside the parent's CMakeLists.
echo "==> [2/2] Building a static parent end to end"
cmake -S "$WORK_DIR/parent" -B "$WORK_DIR/build-static" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTING=ON \
    -DPHASH_BUILD_TESTS=OFF >/dev/null
cmake --build "$WORK_DIR/build-static" --target parent_app -j
"$WORK_DIR/build-static/parent_app"

echo "==> add_subdirectory() smoke test passed"
