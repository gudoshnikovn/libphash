/*
 * Sole translation unit that instantiates the vendored stb_image implementation.
 * Its only content is the #define/#include pair and the thread-local
 * guard that depends on it -- the code that calls stb_image is in src/loader.c.
 *
 * Keeping the decoder (PNG inflate, JPEG, GIF, ...) out of loader.c's translation
 * unit keeps loader.c's code layout independent of the decoder's. In one TU, a
 * loader.c change that never touches decoding can shift the decoder's alignment and
 * move loading benchmarks past the 10% regression-gate threshold
 * (loading_grayscale/loading_rgb move by up to ~15% across -falign-functions
 * variants of byte-identical source).
 */

/* stb_image keeps its failure reason in one global, stbi__g_failure_reason, which
 * src/loader.c reads through stbi_failure_reason() to classify a decode failure. The
 * batch API decodes from several threads at once, so that global must be per-thread or
 * one worker's message overwrites another's -- and the classification with it.
 *
 * stb_image does qualify it thread-local on its own, but through a fallback chain that
 * quietly yields nothing on a toolchain it does not recognize, and not at all if
 * STBI_NO_THREAD_LOCALS is defined. Neither is safe to leave implicit under a threaded
 * API, so the outcome is asserted rather than assumed: the guard below turns a silent
 * data race into a build failure, including after a vendor bump that reworks the chain.
 * stb_image chooses the qualifier unconditionally (it does not honour a definition made
 * before the include), so this checks its choice instead of making one. */
#if defined(STBI_NO_THREAD_LOCALS)
#error "libphash decodes from several threads; stb_image's failure reason must stay thread-local"
#endif

/* stb's own code, not ours: the stricter warnings the library is built with
 * (CMakeLists.txt, PHASH_LIBRARY_WARNING_CANDIDATES) are silenced for this file, which
 * holds nothing else. */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wcast-align"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "../../vendor/stb_image.h"

#ifndef STBI_THREAD_LOCAL
#error "stb_image left its failure reason non-thread-local; ph_batch_* would race on it"
#endif
