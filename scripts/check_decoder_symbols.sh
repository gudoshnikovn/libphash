#!/usr/bin/env bash
# Fails (non-zero exit) unless a binary linked against a full vendored build of libphash
# carries exactly one zlib and no codec it does not use:
#
#   - zlib-ng's dispatch table (`functable`) is defined -- libpng's inflate() calls
#     resolved to the bundled zlib-ng, not to another zlib that won the link;
#   - no libspng symbol (spng_*) and no TurboJPEG API symbol (tj*) is defined. The JPEG
#     decoder is libjpeg-turbo's libjpeg archive (jpeg-static); its TurboJPEG archive
#     compiles a private zlib and libspng in with global symbols, and linking it would put
#     a second zlib next to zlib-ng.
#
#   scripts/check_decoder_symbols.sh build/test_png_variants
#
# Linux (ELF) and macOS (Mach-O, where every C symbol carries a leading underscore).
set -euo pipefail

BIN="${1:?usage: check_decoder_symbols.sh <linked binary>}"
[ -f "$BIN" ] || { echo "check_decoder_symbols: no file at '$BIN'" >&2; exit 1; }

# Defined symbols only, without the Mach-O underscore.
DEFINED="$(nm "$BIN" | awk 'NF >= 3 && $2 != "U" { print $3 }' | sed 's/^_//')"

status=0
if ! grep -qx 'functable' <<<"$DEFINED"; then
    echo "check_decoder_symbols: $BIN has no zlib-ng functable -- inflate() comes from another zlib" >&2
    status=1
fi
stray="$(grep -E '^(spng_|tj3|tjDecompress|tjInit)' <<<"$DEFINED" || true)"
if [ -n "$stray" ]; then
    echo "check_decoder_symbols: $BIN defines libspng/TurboJPEG symbols:" >&2
    sed 's/^/    /' <<<"$stray" | head -20 >&2
    status=1
fi
[ "$status" -eq 0 ] && echo "check_decoder_symbols: $BIN -- one zlib (zlib-ng), no libspng, no TurboJPEG"
exit "$status"
