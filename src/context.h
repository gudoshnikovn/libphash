#ifndef PH_CONTEXT_H
#define PH_CONTEXT_H

/* The context: struct ph_context, its configuration, and the scratch arena every
 * algorithm allocates its working buffers from. */

#include "libphash.h"
#include <stddef.h>
#include <stdint.h>

/* Max length (including NUL) of the diagnostic message stashed by a failed load. */
#define PH_LAST_ERROR_MAX 160

/* Internal Context Structure */
struct ph_context {
    // Uploaded image data
    struct {
        uint8_t *raw_rgb;
        uint8_t *gray_cache;
        int width;
        int height;
        int channels;
        int is_loaded;
    } image;

    // Diagnostic message for the most recent failed load; empty string if none.
    char last_error[PH_LAST_ERROR_MAX];

    // User-defined configuration parameters. Plain values only, no pointers: the batch
    // API copies it by value from a caller's template context into each worker's.
    struct ph_context_config {
        // gamma is applied per image, normalised by the blurred buffer's own maximum;
        // see ph_apply_gamma(), src/image/color.c.
        float gamma;
        int gray_r, gray_g, gray_b;
        int load_grayscale;
        int auto_orient; // On by default: see ph_context_set_auto_orient().

        // Various tunings for hashes
        float mhash_alpha;
        float mhash_level;
        int mhash_size;
        int phash_dct_size;
        int phash_reduction_size;
        int radial_projections;
        int radial_samples;
        float radial_sigma;
        int block_size;
        ph_whash_mode_t whash_mode;
        int whash_remove_max_haar_ll;

        // 0 = no caller limit; PH_MAX_SUPPORTED_PIXELS still applies. Otherwise the max
        // allowed width*height before decoding a pixel buffer.
        uint64_t max_pixels;

        // See ph_context_set_decode_scale(). Only the JPEG backend honors this; other
        // backends decode at full resolution regardless.
        ph_decode_scale_t decode_scale;
    } config;

    // System data of the allocator (Arena)
    struct {
        uint8_t *buffer;
        size_t capacity;
        size_t offset;
    } arena;
};

/* Fills a configuration with the defaults of a freshly created context (src/core.c). */
void ph_config_init_defaults(struct ph_context_config *config);

/* Alignment of the arena's backing buffer and of every block ph_get_scratchpad() hands
 * out. 32 covers every scalar type (max_align_t is 16 on the supported targets) and a
 * 256-bit vector. */
#define PH_ARENA_ALIGNMENT 32

/* Rounds n up to a multiple of PH_ARENA_ALIGNMENT. For carving one scratchpad block into
 * several typed buffers: the arena aligns the start of a block, the offsets inside it are
 * the caller's. Callers pass sizes far below SIZE_MAX, so the rounding cannot wrap. */
static inline size_t ph_arena_align_up(size_t n) {
    return (n + (PH_ARENA_ALIGNMENT - 1)) & ~(size_t)(PH_ARENA_ALIGNMENT - 1);
}

/* The context's scratch arena: the fields of ctx->arena belong to src/core.c; everything
 * else goes through the three functions below.
 *
 * ph_get_scratchpad() hands out a block of at least `size` bytes, aligned to
 * PH_ARENA_ALIGNMENT, or NULL on failure (the arena is then unchanged). A block stays
 * valid only until the next ph_get_scratchpad() on the same context: growing the arena
 * moves its backing buffer, so every pointer handed out before is stale. A caller that
 * needs several buffers at once takes one block and carves it (ph_arena_align_up()).
 *
 * Every caller brackets its blocks with a mark and a release, and releases on EVERY
 * return path, the error paths included:
 *
 *     ph_arena_mark_t mark = ph_arena_mark(ctx);
 *     uint8_t *buf = ph_get_scratchpad(ctx, n);
 *     ...
 *     ph_arena_release(ctx, mark);
 *
 * A missed release is silent -- nothing leaks and the result is right -- but the arena
 * stays grown for the life of the context and stops trimming itself, since it trims only
 * when nothing is handed out. tests/src/test_arena.c checks the balance for every
 * public entry point. */
typedef size_t ph_arena_mark_t;

ph_arena_mark_t ph_arena_mark(const ph_context_t *ctx);
void ph_arena_release(ph_context_t *ctx, ph_arena_mark_t mark);
uint8_t *ph_get_scratchpad(ph_context_t *ctx, size_t size);

#endif /* PH_CONTEXT_H */
