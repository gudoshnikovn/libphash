#!/usr/bin/env bash
# Fails (non-zero exit) unless two builds of the same sources produce byte-identical
# results: for CMake, the whole install tree (static and shared libraries, the vendored
# codec archives, headers, CMake package, libphash.pc), built in two different build
# directories; for the Makefile, libphash.a built twice; for packaging, the release
# archives scripts/package_release.sh writes. The second build starts after the first
# has finished and a second has passed, so any timestamp that reaches an artifact
# differs between them.
#
#   scripts/check_reproducible.sh cmake [extra CMake options]
#   scripts/check_reproducible.sh make
#   scripts/check_reproducible.sh package <platform-name>
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:?usage: check_reproducible.sh cmake|make|package [options]}"
shift
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

checksums() {
    (cd "$1" && find . -type f | LC_ALL=C sort | while IFS= read -r f; do
        printf '%s  %s\n' "$(cksum < "$f" | tr -s ' ' '-')" "$f"
    done)
}

case "$MODE" in
    cmake)
        for n in 1 2; do
            for kind in static shared; do
                extra=()
                [ "$kind" = shared ] && extra=(-DPHASH_BUILD_SHARED=ON)
                log="$WORK_DIR/build$n-$kind.log"
                if ! { cmake -S "$ROOT" -B "$WORK_DIR/build$n-$kind" -DCMAKE_BUILD_TYPE=Release \
                           -DPHASH_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$WORK_DIR/prefix$n-$kind" \
                           ${extra[@]+"${extra[@]}"} "$@" &&
                       cmake --build "$WORK_DIR/build$n-$kind" -j &&
                       cmake --install "$WORK_DIR/build$n-$kind"; } >"$log" 2>&1; then
                    cat "$log" >&2
                    echo "check_reproducible: build $n ($kind) failed" >&2
                    exit 1
                fi
            done
            sleep 1
        done
        status=0
        for kind in static shared; do
            checksums "$WORK_DIR/prefix1-$kind" > "$WORK_DIR/sum1-$kind"
            checksums "$WORK_DIR/prefix2-$kind" > "$WORK_DIR/sum2-$kind"
            if diff "$WORK_DIR/sum1-$kind" "$WORK_DIR/sum2-$kind" >&2; then
                echo "check_reproducible: $kind install tree identical ($(wc -l < "$WORK_DIR/sum1-$kind" | tr -d ' ') files)"
            else
                echo "check_reproducible: $kind install trees differ (above)" >&2
                status=1
            fi
        done
        exit "$status"
        ;;
    make)
        make -C "$ROOT" clean >/dev/null
        make -C "$ROOT" -j libphash.a >/dev/null
        cp "$ROOT/libphash.a" "$WORK_DIR/first.a"
        sleep 1
        make -C "$ROOT" clean >/dev/null
        make -C "$ROOT" -j libphash.a >/dev/null
        if cmp -s "$ROOT/libphash.a" "$WORK_DIR/first.a"; then
            echo "check_reproducible: make libphash.a identical across two builds"
        else
            echo "check_reproducible: make libphash.a differs between two builds" >&2
            exit 1
        fi
        ;;
    package)
        platform="${1:?usage: check_reproducible.sh package <platform-name>}"
        for n in 1 2; do
            if ! "$ROOT/scripts/package_release.sh" "$platform" "$WORK_DIR/dist$n" both \
                    >"$WORK_DIR/package$n.log" 2>&1; then
                cat "$WORK_DIR/package$n.log" >&2
                echo "check_reproducible: packaging $n failed" >&2
                exit 1
            fi
            sleep 1
        done
        checksums "$WORK_DIR/dist1" > "$WORK_DIR/dist1.sum"
        checksums "$WORK_DIR/dist2" > "$WORK_DIR/dist2.sum"
        if diff "$WORK_DIR/dist1.sum" "$WORK_DIR/dist2.sum" >&2; then
            echo "check_reproducible: release archives identical ($(wc -l < "$WORK_DIR/dist1.sum" | tr -d ' ') files)"
        else
            echo "check_reproducible: release archives differ (above)" >&2
            exit 1
        fi
        ;;
    *)
        echo "check_reproducible: unknown mode '$MODE' (cmake, make or package)" >&2
        exit 1
        ;;
esac
