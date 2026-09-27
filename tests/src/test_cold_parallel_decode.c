/* The very first decodes of a process, run in parallel.
 *
 * zlib-ng -- the inflate under the native PNG backends -- picks its CPU-specific routines
 * lazily, on the first call, by writing a global function table that other threads read
 * without synchronisation: a data race under the C memory model, reported by
 * ThreadSanitizer with a stack through ph_hash_files(). The library now warms that
 * dispatch once, under a lock, before its first decode.
 *
 * What makes this test worth having is what it does NOT do: nothing is decoded before the
 * threads start. The other thread tests compute a sequential reference first, which warms
 * the dispatch and hides the race. Run under -fsanitize=thread in a build with the native
 * PNG decoder and zlib-ng to see it; everywhere else it checks the results agree. */
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <string.h>

#define ITEMS 48

int main(void) {
    ph_batch_item_t items[ITEMS];
    memset(items, 0, sizeof(items));
    for (int i = 0; i < ITEMS; i++)
        items[i].path = TEST_DATA_DIR "/photo.png";

    ASSERT_OK(ph_hash_files(items, ITEMS, PH_HASH_AHASH | PH_HASH_PHASH, 0));

    for (int i = 0; i < ITEMS; i++) {
        ASSERT_OK(items[i].status);
        ASSERT_UINT64_EQ(items[0].hashes[0], items[i].hashes[0]);
        ASSERT_UINT64_EQ(items[0].hashes[1], items[i].hashes[1]);
    }

    /* And the parallel answer is the sequential one. */
    ph_context_t *ctx = NULL;
    uint64_t ahash = 0;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.png"));
    ASSERT_OK(ph_compute_ahash(ctx, &ahash));
    ASSERT_UINT64_EQ(ahash, items[0].hashes[0]);
    ph_free(ctx);

    PASS("test_cold_parallel_decode");
    return 0;
}
