/*
 * Sole translation unit that instantiates the vendored stb_image implementation.
 * Its only content is the #define/#include pair and the thread-local
 * guard that depends on it -- everything else stays in src/core.c.
 *
 * stb_image previously had STB_IMAGE_IMPLEMENTATION defined directly inside
 * core.c, which meant the whole decoder (PNG inflate, JPEG, GIF, ...) compiled
 * into the same translation unit as context lifecycle and load orchestration.
 * A change to core.c that never touches decoding could still shuffle the
 * decoder's code layout in the binary and move loading-benchmark numbers by as
 * much as the 10% regression-gate threshold, purely from alignment (measured
 * at one point: loading_grayscale/loading_rgb moved by up to ~15% across three
 * builds of byte-identical source that only varied -falign-functions). Giving
 * stb_image its own TU makes core.c's own code layout independent of the
 * decoder's.
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

#define STB_IMAGE_IMPLEMENTATION
#include "../../vendor/stb_image.h"

#ifndef STBI_THREAD_LOCAL
#error "stb_image left its failure reason non-thread-local; ph_batch_* would race on it"
#endif
