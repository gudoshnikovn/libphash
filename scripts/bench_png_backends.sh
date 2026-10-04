#!/usr/bin/env bash
# Measure PNG decode time of the libpng backend against the spng backend.
#
# Usage: bench_png_backends.sh <corpus_dir> <work_dir> [rounds] [out_md]
#
# Builds four configurations of test_benchmark: {libpng, spng} x {zlib-ng, system
# zlib}. Only the PNG path differs between them -- JPEG and WebP are off -- so the
# comparison is the decoder and its inflate, nothing else. For every corpus file it
# runs `test_benchmark --json load <file> <iters>` for each build, <rounds> times,
# rotating the build order each round so drift on the machine (thermal, a noisy
# neighbour on a CI runner) lands on every build alike. Each run reports min_ms of
# its iterations; the table shows the median of those across rounds -- the same
# estimator as bench_regression_gate.sh, for the reason given there.
#
# Requires cmake, a C compiler, jq, and zlib development headers.
set -euo pipefail

CORPUS="$(cd "$1" && pwd)"
WORK="$2"
ROUNDS="${3:-5}"
OUT_MD="${4:-}"
SRC="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"

CONFIGS=(libpng-zlibng spng-zlibng libpng-zlib spng-zlib)

flags_for() {
    case "$1" in
        libpng-zlibng) echo "-DPHASH_USE_LIBPNG=ON -DPHASH_USE_SPNG=OFF -DPHASH_USE_ZLIB_NG=ON" ;;
        spng-zlibng) echo "-DPHASH_USE_LIBPNG=OFF -DPHASH_USE_SPNG=ON -DPHASH_USE_ZLIB_NG=ON" ;;
        libpng-zlib) echo "-DPHASH_USE_LIBPNG=ON -DPHASH_USE_SPNG=OFF -DPHASH_USE_ZLIB_NG=OFF" ;;
        spng-zlib) echo "-DPHASH_USE_LIBPNG=OFF -DPHASH_USE_SPNG=ON -DPHASH_USE_ZLIB_NG=OFF" ;;
    esac
}

JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
for c in "${CONFIGS[@]}"; do
    # shellcheck disable=SC2046
    cmake -S "$SRC" -B "$WORK/build-$c" -DCMAKE_BUILD_TYPE=Release \
        -DPHASH_USE_LIBJPEG_TURBO=OFF -DPHASH_USE_WEBP=OFF -DPHASH_STRICT_DEPS=ON \
        $(flags_for "$c") >"$WORK/configure-$c.log"
    cmake --build "$WORK/build-$c" --target test_benchmark -j "$JOBS" >"$WORK/build-$c.log"
done

# The configure flags choose the backend; this confirms the binary really carries
# that decoder (and only it), so a silent fallback can't produce a fake comparison.
for c in "${CONFIGS[@]}"; do
    bin="$WORK/build-$c/test_benchmark"
    syms="$(nm "$bin" 2>/dev/null || true)"
    has_spng=0
    has_libpng=0
    grep -q 'spng_ctx_new' <<<"$syms" && has_spng=1
    grep -q 'png_create_read_struct' <<<"$syms" && has_libpng=1
    case "$c" in
        spng-*) want_spng=1 want_libpng=0 ;;
        *) want_spng=0 want_libpng=1 ;;
    esac
    if [[ $has_spng != "$want_spng" || $has_libpng != "$want_libpng" ]]; then
        echo "error: $c links spng=$has_spng libpng=$has_libpng" >&2
        exit 1
    fi
done

# Iterations per run: enough for ~1.5 s of decoding, at least 5.
iters_for() {
    local bytes
    bytes=$(wc -c <"$1")
    local it=$((300000000 / (bytes + 1000000)))
    ((it < 5)) && it=5
    ((it > 200)) && it=200
    echo "$it"
}

RAW="$WORK/raw.tsv"
: >"$RAW"
for f in "$CORPUS"/*.png; do
    name="$(basename "$f")"
    it="$(iters_for "$f")"
    for ((r = 0; r < ROUNDS; r++)); do
        for ((k = 0; k < ${#CONFIGS[@]}; k++)); do
            c="${CONFIGS[$(((k + r) % ${#CONFIGS[@]}))]}"
            json="$("$WORK/build-$c/test_benchmark" --json load "$f" "$it")"
            gray="$(jq -r '.loading_grayscale.min_ms' <<<"$json")"
            rgb="$(jq -r '.loading_rgb.min_ms' <<<"$json")"
            printf '%s\t%s\t%s\t%s\n' "$name" "$c" "$gray" "$rgb" >>"$RAW"
        done
    done
    echo "done: $name ($it iters x $ROUNDS rounds)" >&2
done

median() { sort -g | awk '{a[NR]=$1} END {print (NR % 2) ? a[(NR+1)/2] : (a[NR/2]+a[NR/2+1])/2}'; }
med() { awk -F'\t' -v n="$1" -v c="$2" -v col="$3" '$1==n && $2==c {print $col}' "$RAW" | median; }
# Positive = spng faster than libpng by that many percent of libpng's time.
gain() { awk -v a="$1" -v b="$2" 'BEGIN {printf "%+.1f%%", (a - b) / a * 100}'; }

{
    echo "### PNG decode: libpng vs spng ($(uname -sm), $(${CC:-cc} --version | head -1))"
    echo
    echo "Median over $ROUNDS rounds of min_ms per load. **spng gain** > 0 means spng is faster."
    for mode in gray rgb; do
        col=3
        [[ $mode == rgb ]] && col=4
        echo
        echo "#### Load to $mode"
        echo
        echo "| file | size | libpng+zlib-ng | spng+zlib-ng | spng gain | libpng+zlib | spng+zlib | spng gain |"
        echo "| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
        for f in "$CORPUS"/*.png; do
            n="$(basename "$f")"
            sz="$(awk -v b="$(wc -c <"$f")" 'BEGIN {printf "%.1f MB", b / 1048576}')"
            a="$(med "$n" libpng-zlibng $col)"
            b="$(med "$n" spng-zlibng $col)"
            c="$(med "$n" libpng-zlib $col)"
            d="$(med "$n" spng-zlib $col)"
            echo "| $n | $sz | $a | $b | $(gain "$a" "$b") | $c | $d | $(gain "$c" "$d") |"
        done
    done
} | tee "${OUT_MD:-/dev/null}"
