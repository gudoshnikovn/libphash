/* The very first decodes of a process, run in parallel.
 *
 * Decoders and the library initialise some state lazily, on first use: zlib-ng -- the
 * inflate under the native PNG backend -- picks its CPU-specific routines by writing a
 * global function table that other threads read, a data race under the C memory model
 * that ThreadSanitizer reports with a stack through ph_hash_files(); the library warms
 * that dispatch once, under a lock, before its first decode. The DCT matrix and the
 * other decoders' one-time setup are first touched here too.
 *
 * What makes this test worth having is what it does NOT do: nothing is decoded or hashed
 * before the threads start, and the batch mixes every format, so each format's first
 * decode happens on several threads at once. A sequential reference computed first
 * would warm all of it and hide the race; here it comes after. Run under
 * -fsanitize=thread in a build with the native decoders to see a race; everywhere else
 * it checks the results agree. */
#include "libphash.h"
#include "test_macros.h"

#include <stdio.h>
#include <string.h>

#define ITEMS 48
#define FLAGS (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH)
#define NSET  4

static const char *const PATHS[] = {
    TEST_DATA_DIR "/photo.png",
    TEST_DATA_DIR "/photo.jpeg",
    TEST_DATA_DIR "/photo.webp",
};
#define NUM_PATHS (sizeof(PATHS) / sizeof(PATHS[0]))

int main(void) {
    ph_batch_item_t items[ITEMS];
    memset(items, 0, sizeof(items));
    for (int i = 0; i < ITEMS; i++) {
        items[i].path = PATHS[(size_t)i % NUM_PATHS];
    }

    ASSERT_OK(ph_hash_files(items, ITEMS, FLAGS, 0));

    /* And the parallel answer is the sequential one, status included: WebP is
     * PH_ERR_DECODER_UNAVAILABLE in a build without its backend. */
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    for (size_t p = 0; p < NUM_PATHS; p++) {
        uint64_t want[PH_HASH_FLAGS_COUNT] = {0};
        ph_error_t want_status = ph_load_from_file(ctx, PATHS[p]);
        if (want_status == PH_SUCCESS) {
            want_status = ph_compute_multi(ctx, FLAGS, want);
        }
        for (size_t i = p; i < ITEMS; i += NUM_PATHS) {
            ASSERT_MSG(items[i].status == want_status, "%s (item %zu): status %d, sequential %d",
                       PATHS[p], i, items[i].status, want_status);
            if (want_status == PH_SUCCESS) {
                ASSERT_MSG(memcmp(items[i].hashes, want, NSET * sizeof(uint64_t)) == 0,
                           "%s (item %zu): hashes differ from the sequential run", PATHS[p], i);
            }
        }
    }
    ph_free(ctx);

    PASS("test_cold_parallel_decode");
    return 0;
}
