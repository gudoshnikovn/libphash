#!/usr/bin/env bash
# Fails (non-zero exit) unless a binary linked against libphash carries exactly the
# decoders its CMake build tree asked for, linked statically, and nothing else:
#
#   - "asked for" is the PHASH_USE_* options in the CMakeCache.txt of the build tree the
#     binary sits in. A decoder that is on there and silently fell back to stb_image, or
#     that is off and was linked anyway, fails the check;
#   - each decoder that is on defines its entry point in the binary (libjpeg-turbo
#     jpeg_std_error, libpng png_create_read_struct, libwebp WebPDecode), and each one
#     that is off defines none;
#   - with libpng and zlib-ng on, zlib-ng's dispatch table (`functable`) is defined:
#     libpng's inflate() resolved to the bundled zlib-ng, not to another zlib that won
#     the link. With libpng on and zlib-ng off, libpng uses the system zlib, and that is
#     the one dynamic dependency allowed;
#   - no libspng symbol (spng_*) and no TurboJPEG API symbol (tj*) is defined. The JPEG
#     decoder is libjpeg-turbo's libjpeg archive (jpeg-static); its TurboJPEG archive
#     compiles a private zlib and libspng in with global symbols, and linking it would put
#     a second zlib next to zlib-ng;
#   - no dynamic dependency on a decoder library (libjpeg, libturbojpeg, libpng, libspng,
#     libwebp, libsharpyuv) or on libz: the vendored decoders are static archives, and a
#     system copy that won the link would show up here.
#
#   scripts/check_decoder_symbols.sh build/release/test_png_variants
#
# The binary may also be libphash's shared library itself. Linux (ELF) and macOS
# (Mach-O, where every C symbol carries a leading underscore).
set -euo pipefail

BIN="${1:?usage: check_decoder_symbols.sh <linked binary>}"
[ -f "$BIN" ] || { echo "check_decoder_symbols: no file at '$BIN'" >&2; exit 1; }

# The build tree is the nearest directory above the binary with a CMakeCache.txt.
dir="$(cd "$(dirname "$BIN")" && pwd)"
while [ ! -f "$dir/CMakeCache.txt" ]; do
    [ "$dir" != "/" ] || { echo "check_decoder_symbols: no CMakeCache.txt above '$BIN'" >&2; exit 1; }
    dir="$(dirname "$dir")"
done
CACHE="$dir/CMakeCache.txt"

option_on() {
    local value
    value="$(sed -n "s/^$1:BOOL=//p" "$CACHE")"
    case "$value" in
        ON | on | TRUE | true | 1 | YES | yes | Y | y) return 0 ;;
        OFF | off | FALSE | false | 0 | NO | no | N | n | "") return 1 ;;
        *)
            echo "check_decoder_symbols: unexpected $1=$value in $CACHE" >&2
            exit 1
            ;;
    esac
}

# Defined symbols only, without the Mach-O underscore.
DEFINED="$(nm "$BIN" | awk 'NF >= 3 && $2 != "U" { print $3 }' | sed 's/^_//')"

if [ "$(uname -s)" = Darwin ]; then
    NEEDED="$(otool -L "$BIN" | tail -n +2 | awk '{ print $1 }' | xargs -n1 basename)"
else
    NEEDED="$(readelf -d "$BIN" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')"
fi

status=0
fail() {
    echo "check_decoder_symbols: $BIN: $*" >&2
    status=1
}

defines() { grep -qx "$1" <<<"$DEFINED"; }

expect_decoder() { # option symbol name
    if option_on "$1"; then
        defines "$2" || fail "$1=ON, but $3 is not linked ($2 is not defined)"
    else
        defines "$2" && fail "$1=OFF, but $3 is linked ($2 is defined)"
    fi
    return 0
}

expect_decoder PHASH_USE_LIBJPEG_TURBO jpeg_std_error libjpeg-turbo
expect_decoder PHASH_USE_LIBPNG png_create_read_struct libpng
expect_decoder PHASH_USE_WEBP WebPDecode libwebp

zlib_allowed=0
if option_on PHASH_USE_LIBPNG; then
    if option_on PHASH_USE_ZLIB_NG; then
        defines functable || fail "zlib-ng is on, but there is no zlib-ng functable -- inflate() comes from another zlib"
    else
        defines functable && fail "PHASH_USE_ZLIB_NG=OFF, but zlib-ng is linked (functable is defined)"
        zlib_allowed=1
    fi
else
    defines functable && fail "no decoder needs zlib, but zlib-ng is linked (functable is defined)"
fi

stray="$(grep -E '^(spng_|tj3|tjDecompress|tjInit)' <<<"$DEFINED" || true)"
[ -z "$stray" ] || fail "defines libspng/TurboJPEG symbols: $(head -5 <<<"$stray" | tr '\n' ' ')"

decoder_libs="$(grep -E '^lib(jpeg|turbojpeg|png[0-9]*|spng|webp|webpdecoder|sharpyuv)([.-]|$)' <<<"$NEEDED" || true)"
[ -z "$decoder_libs" ] || fail "links decoder libraries dynamically: $(tr '\n' ' ' <<<"$decoder_libs")"
zlib_libs="$(grep -E '^libz([.-]|$)' <<<"$NEEDED" || true)"
if [ -n "$zlib_libs" ] && [ "$zlib_allowed" -eq 0 ]; then
    fail "links the system zlib dynamically ($(tr '\n' ' ' <<<"$zlib_libs")), which this build does not use"
fi

if [ "$status" -eq 0 ]; then
    summary=""
    option_on PHASH_USE_LIBJPEG_TURBO && summary+=" libjpeg-turbo"
    option_on PHASH_USE_LIBPNG && summary+=" libpng"
    option_on PHASH_USE_WEBP && summary+=" libwebp"
    if option_on PHASH_USE_LIBPNG; then
        if [ "$zlib_allowed" -eq 1 ]; then summary+=" system-zlib"; else summary+=" zlib-ng"; fi
    fi
    echo "check_decoder_symbols: $BIN -- linked as configured:${summary:- stb_image only}; static, no libspng, no TurboJPEG"
fi
exit "$status"
