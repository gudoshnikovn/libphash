#ifndef PH_ARENA_H
#define PH_ARENA_H

/* The scratch arena every context carries, which the algorithms take their working
 * buffers from so that hashing does not go to malloc() on every call. The fields of
 * ph_arena_t belong to src/arena.c; everything else goes through the functions below. */

#include "libphash.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t offset;
} ph_arena_t;

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

/* The address `offset` bytes into a scratchpad block, for a typed buffer carved out of it:
 * `float *f = ph_arena_at(block, off)`. The block starts on PH_ARENA_ALIGNMENT, and the
 * caller keeps each offset a multiple of its buffer's alignment -- ph_arena_align_up()
 * where the types differ. Returning void * rather than casting uint8_t * states that
 * promise once, here, where -Wcast-align cannot see it at every call site. */
static inline void *ph_arena_at(uint8_t *block, size_t offset) { return block + offset; }

/* ph_get_scratchpad() hands out a block of at least `size` bytes, aligned to
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

/* Frees the backing buffer; the arena is empty afterwards and may be used again. */
void ph_arena_free(ph_arena_t *arena);

/* Size of the backing buffer. For the tests of growth and trimming; nothing in the
 * library needs it. */
size_t ph_arena_capacity(const ph_context_t *ctx);

#endif /* PH_ARENA_H */
