#!/usr/bin/env bash
# Measure what each vendored decoder buys over the stb_image fallback.
#
# Usage: bench_decoders.sh <corpus_dir> <work_dir> [rounds] [out_md]
#
# <corpus_dir> holds png/ and jpeg/ (scripts/gen_bench_corpus.py). Three builds of
# bench_hash, WebP off in all of them:
#   native   libjpeg-turbo + libpng + zlib-ng
#   syszlib  libpng + the system zlib (PNG only)
#   stb      every native decoder off: stb_image decodes both formats
# PNG files run on all three; JPEG files on native at full scale, native at
# PH_DECODE_SCALE_EIGHTH (libjpeg's DCT-domain downscale, which stb_image does not have),
# and stb. For every file the order of the variants rotates each round so machine drift lands on all of them alike; each run
# reports min_ms over its iterations and the table shows the median of those across
# rounds (the estimator of bench_regression_gate.sh). Also reported: the stripped size of
# each bench_hash binary and the peak RSS of one decode of the largest file of each
# format. Recorded results are in docs/benchmarks/.
#
# Requires cmake, a C compiler, jq, zlib headers, and nasm on x86.
set -euo pipefail

CORPUS="$(cd "$1" && pwd)"
WORK="$2"
ROUNDS="${3:-5}"
OUT_MD="${4:-}"
SRC="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

if [[ ! -f "$SRC/vendor/libjpeg-turbo/build/libjpeg.a" ]]; then
    cmake -S "$SRC/vendor/libjpeg-turbo" -B "$SRC/vendor/libjpeg-turbo/build" \
        -DCMAKE_BUILD_TYPE=Release -DENABLE_SHARED=OFF >"$WORK/jpeg-configure.log"
    cmake --build "$SRC/vendor/libjpeg-turbo/build" --target jpeg-static -j "$JOBS" \
        >"$WORK/jpeg-build.log"
fi

flags_for() {
    case "$1" in
        native) echo "-DPHASH_USE_LIBJPEG_TURBO=ON -DPHASH_USE_LIBPNG=ON -DPHASH_USE_ZLIB_NG=ON" ;;
        syszlib) echo "-DPHASH_USE_LIBJPEG_TURBO=OFF -DPHASH_USE_LIBPNG=ON -DPHASH_USE_ZLIB_NG=OFF" ;;
        stb) echo "-DPHASH_USE_LIBJPEG_TURBO=OFF -DPHASH_USE_LIBPNG=OFF -DPHASH_USE_ZLIB_NG=OFF" ;;
    esac
}
for c in native syszlib stb; do
    # shellcheck disable=SC2046
    cmake -S "$SRC" -B "$WORK/build-$c" -DCMAKE_BUILD_TYPE=Release -DPHASH_USE_WEBP=OFF \
        -DPHASH_STRICT_DEPS=ON $(flags_for "$c") >"$WORK/configure-$c.log"
    cmake --build "$WORK/build-$c" --target bench_hash -j "$JOBS" >"$WORK/build-$c.log"
done

# The configure flags choose the decoders; nm confirms each binary carries exactly them.
# Not `nm | grep -q`: grep exiting early kills nm with SIGPIPE, which pipefail reports.
links() { grep -c "$2" <<<"$(nm "$WORK/build-$1/bench_hash" 2>/dev/null)" >/dev/null; }
links native jpeg_start_decompress && links native png_create_read_struct ||
    { echo "error: native build lacks libjpeg or libpng" >&2; exit 1; }
links syszlib png_create_read_struct && ! links syszlib jpeg_start_decompress ||
    { echo "error: syszlib build is not libpng-only" >&2; exit 1; }
! links stb jpeg_start_decompress && ! links stb png_create_read_struct ||
    { echo "error: stb build links a native decoder" >&2; exit 1; }

run_variant() { # <variant> <file> <iters>
    case "$1" in
        native-eighth) "$WORK/build-native/bench_hash" --json load "$2" "$3" 3 ;;
        *) "$WORK/build-$1/bench_hash" --json load "$2" "$3" ;;
    esac
}

RAW="$WORK/raw.tsv"
: >"$RAW"
measure() { # <format> <budget_bytes> <variants...>
    local fmt="$1" budget="$2"
    shift 2
    local variants=("$@") n=$#
    for f in "$CORPUS/$fmt"/*; do
        local name it bytes
        name="$(basename "$f")"
        bytes=$(wc -c <"$f")
        it=$((budget / (bytes + 1000000)))
        ((it < 5)) && it=5
        ((it > 200)) && it=200
        for ((r = 0; r < ROUNDS; r++)); do
            for ((k = 0; k < n; k++)); do
                local v="${variants[$(((k + r) % n))]}" json
                json="$(run_variant "$v" "$f" "$it")"
                printf '%s\t%s\t%s\t%s\n' "$name" "$v" \
                    "$(jq -r '.loading_grayscale.min_ms' <<<"$json")" \
                    "$(jq -r '.loading_rgb.min_ms' <<<"$json")" >>"$RAW"
            done
        done
        echo "done: $fmt/$name ($it iters x $ROUNDS rounds)" >&2
    done
}
measure png 150000000 native syszlib stb
measure jpeg 100000000 native native-eighth stb

median() { sort -g | awk '{a[NR]=$1} END {print (NR % 2) ? a[(NR+1)/2] : (a[NR/2]+a[NR/2+1])/2}'; }
med() { awk -F'\t' -v n="$1" -v c="$2" -v col="$3" '$1==n && $2==c {print $col}' "$RAW" | median; }
ms() { awk -v x="$1" 'BEGIN {printf "%.2f", x}'; }
# How many times faster the native variant is than stb.
x() { awk -v s="$1" -v n="$2" 'BEGIN {printf "%.2f×", s / n}'; }

peak_rss_kb() { # <build> <file> [scale]
    local out
    if [[ "$(uname)" == Darwin ]]; then
        out="$(/usr/bin/time -l "$WORK/build-$1/bench_hash" load "$2" 1 "${3:-0}" 2>&1 >/dev/null)"
        awk '/maximum resident set size/ {printf "%d", $1 / 1024}' <<<"$out"
    else
        out="$(/usr/bin/time -f '%M' "$WORK/build-$1/bench_hash" load "$2" 1 "${3:-0}" 2>&1 >/dev/null)"
        tail -1 <<<"$out"
    fi
}
stripped_kb() {
    strip -o "$WORK/stripped-$1" "$WORK/build-$1/bench_hash" 2>/dev/null ||
        { cp "$WORK/build-$1/bench_hash" "$WORK/stripped-$1" && strip "$WORK/stripped-$1"; }
    echo $(($(wc -c <"$WORK/stripped-$1") / 1024))
}
largest() { ls -S "$CORPUS/$1" | head -1; }

{
    echo "### Decoders vs stb_image ($(uname -sm), $(${CC:-cc} --version | head -1))"
    echo
    echo "Median over $ROUNDS rounds of min_ms per load. **×** = how many times faster than stb_image."
    for mode in gray rgb; do
        col=3
        [[ $mode == rgb ]] && col=4
        echo
        echo "#### PNG, load to $mode"
        echo
        echo "| file | libpng+zlib-ng | libpng+zlib | stb | zlib-ng vs stb | zlib vs stb |"
        echo "| :--- | ---: | ---: | ---: | ---: | ---: |"
        for f in "$CORPUS"/png/*; do
            n="$(basename "$f")"
            a="$(med "$n" native $col)" b="$(med "$n" syszlib $col)" s="$(med "$n" stb $col)"
            echo "| $n | $(ms "$a") | $(ms "$b") | $(ms "$s") | $(x "$s" "$a") | $(x "$s" "$b") |"
        done
        echo
        echo "#### JPEG, load to $mode"
        echo
        echo "| file | libjpeg-turbo | libjpeg-turbo 1/8 | stb | turbo vs stb | turbo 1/8 vs stb |"
        echo "| :--- | ---: | ---: | ---: | ---: | ---: |"
        for f in "$CORPUS"/jpeg/*; do
            n="$(basename "$f")"
            a="$(med "$n" native $col)" e="$(med "$n" native-eighth $col)" s="$(med "$n" stb $col)"
            echo "| $n | $(ms "$a") | $(ms "$e") | $(ms "$s") | $(x "$s" "$a") | $(x "$s" "$e") |"
        done
    done
    echo
    echo "#### Footprint"
    echo
    echo "| build | stripped bench_hash | peak RSS, largest PNG | peak RSS, largest JPEG |"
    echo "| :--- | ---: | ---: | ---: |"
    lp="$CORPUS/png/$(largest png)" lj="$CORPUS/jpeg/$(largest jpeg)"
    echo "| native | $(stripped_kb native) KB | $(peak_rss_kb native "$lp") KB | $(peak_rss_kb native "$lj") KB (1/8: $(peak_rss_kb native "$lj" 3) KB) |"
    echo "| syszlib | $(stripped_kb syszlib) KB | $(peak_rss_kb syszlib "$lp") KB | — |"
    echo "| stb | $(stripped_kb stb) KB | $(peak_rss_kb stb "$lp") KB | $(peak_rss_kb stb "$lj") KB |"
} | tee "${OUT_MD:-/dev/null}"
