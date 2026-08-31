/*
 * Sole translation unit that instantiates the vendored stb_image_resize2
 * implementation (R46).
 *
 * Why this file exists at all — it holds nothing but the #define/#include pair:
 *
 * stb_image_resize2 packs its filter coefficients with deliberately unaligned
 * 64-bit moves (`STBIR_MOVE_2` in `stbir__pack_coefficients`, which casts a
 * `float*` to `stbir_uint64*`). The coefficient array is stb's own internal
 * bump allocation and is 16-byte aligned; the misalignment comes from stb's own
 * indexing (`coeffs += coefficient_width` / `pc += 7` with an odd stride), not
 * from any buffer we hand it — our `src`/`dst` are plain malloc'd and always
 * 16-byte aligned. So this is UB by design inside the vendored code, present
 * verbatim in current upstream master (v2.18) and not fixable by a version bump.
 *
 * Under `-fsanitize=undefined` it produced 5 `runtime error: load/store of
 * misaligned address ... for type 'stbir_uint64'` per test run, which drowned
 * out our own findings (that noise is exactly why defect H6 went unnoticed).
 *
 * The build therefore compiles THIS FILE ONLY with `-fno-sanitize=alignment`
 * (see the `STB_NOSAN_CFLAGS` rule in the Makefile and the
 * `set_source_files_properties` call in CMakeLists.txt). Keeping the stb
 * implementation in a file that contains zero lines of our own code means the
 * exemption cannot possibly mask an alignment bug of ours.
 *
 * A runtime `UBSAN_OPTIONS=suppressions=...` file was evaluated and rejected:
 * it is silently ignored under `-fno-sanitize-recover=all` (which the CI
 * `sanitizers` job uses), so the job would still abort.
 */

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../../vendor/stb_image_resize2.h"
