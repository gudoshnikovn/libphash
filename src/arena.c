#ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#endif

#include "arena.h"

#include "context.h"

#include <stdlib.h>
#include <string.h>

/* The backing buffer is aligned to PH_ARENA_ALIGNMENT, which malloc() does not promise.
 * `size` is a multiple of the alignment (ph_get_scratchpad() rounds it up), as
 * posix_memalign() and _aligned_malloc() both accept. */
static uint8_t *ph_aligned_alloc(size_t size) {
#if defined(_WIN32)
    return (uint8_t *)_aligned_malloc(size, PH_ARENA_ALIGNMENT);
#else
    void *p = NULL;
    return posix_memalign(&p, PH_ARENA_ALIGNMENT, size) == 0 ? (uint8_t *)p : NULL;
#endif
}

static void ph_aligned_free(uint8_t *p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    free(p);
#endif
}

ph_arena_mark_t ph_arena_mark(const ph_context_t *ctx) { return ctx->arena.offset; }

void ph_arena_release(ph_context_t *ctx, ph_arena_mark_t mark) { ctx->arena.offset = mark; }

uint8_t *ph_get_scratchpad(ph_context_t *ctx, size_t size) {
    if (!ctx || size == 0) {
        return NULL;
    }

    /* Auto-trim on top-level calls only to prevent unbounded memory growth */
    if (ctx->arena.offset == 0 && ctx->arena.buffer && ctx->arena.capacity > size * 4) {
        ph_aligned_free(ctx->arena.buffer);
        ctx->arena.buffer = NULL;
        ctx->arena.capacity = 0;
    }

    /* Every block starts on a PH_ARENA_ALIGNMENT boundary: the offset left by the previous
     * block is rounded up first. The backing buffer itself is allocated with that
     * alignment, so the block is aligned in absolute terms too. */
    size_t start = ph_arena_align_up(ctx->arena.offset);
    if (start < ctx->arena.offset || size > SIZE_MAX - start) {
        return NULL;
    }
    size_t required = start + size;

    if (ctx->arena.capacity < required) {
        // Grow by more than required to avoid frequent reallocs
        size_t new_size = required > ctx->arena.capacity * 2 ? required : ctx->arena.capacity * 2;
        if (new_size < 1024) {
            new_size = 1024;
        }

        // Ensure new_size is a multiple of the alignment for posix_memalign
        if (new_size > SIZE_MAX - (PH_ARENA_ALIGNMENT - 1)) {
            return NULL;
        }
        new_size = ph_arena_align_up(new_size);

        uint8_t *new_ptr = ph_aligned_alloc(new_size);
        if (!new_ptr) {
            return NULL;
        }

        if (ctx->arena.buffer) {
            // Realloc alternative for aligned memory
            memcpy(new_ptr, ctx->arena.buffer, ctx->arena.offset);
            ph_aligned_free(ctx->arena.buffer);
        }
        ctx->arena.buffer = new_ptr;
        ctx->arena.capacity = new_size;
    }

    uint8_t *ptr = ctx->arena.buffer + start;
    ctx->arena.offset = required;

    return ptr;
}

void ph_arena_free(ph_arena_t *arena) {
    ph_aligned_free(arena->buffer);
    arena->buffer = NULL;
    arena->capacity = 0;
    arena->offset = 0;
}

size_t ph_arena_capacity(const ph_context_t *ctx) { return ctx->arena.capacity; }
