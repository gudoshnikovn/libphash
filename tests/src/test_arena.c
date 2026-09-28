/* The scratch arena's mark/release contract (src/context.h): every entry point that takes
 * blocks from the arena gives them back on every return path. A missed release is silent --
 * the hash is right and nothing leaks -- so the only place it can show is the arena's own
 * offset, which is what this test watches: after each call it must be exactly where it was
 * before, on success and on failure alike. */
#include "alloc_shim.h"
#include "context.h"
#include "libphash.h"
#include "test_macros.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void check_balanced(const ph_context_t *ctx, size_t before, const char *what,
                           ph_error_t err) {
    if (ph_arena_mark(ctx) != before) {
        fprintf(stderr, "[FAIL] %s (returned %d): arena offset %zu before, %zu after\n", what,
                (int)err, before, ph_arena_mark(ctx));
        g_failures++;
    }
}

/* Runs every algorithm and ph_compute_multi() on ctx, checking the balance after each.
 * Whether a call succeeds does not matter here: the balance is checked either way. */
static void run_all(ph_context_t *ctx, const char *label) {
    char what[128];
    for (int a = 0; a < PH_ALGORITHM_COUNT; a++) {
        ph_digest_t digest;
        size_t before = ph_arena_mark(ctx);
        ph_error_t err = ph_compute_digest(ctx, (ph_algorithm_t)a, &digest);
        snprintf(what, sizeof(what), "%s: %s", label, ph_algorithm_name((ph_algorithm_t)a));
        check_balanced(ctx, before, what, err);
    }
    uint64_t out[PH_HASH_FLAGS_COUNT];
    size_t before = ph_arena_mark(ctx);
    ph_error_t err =
        ph_compute_multi(ctx, PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH, out);
    snprintf(what, sizeof(what), "%s: multi", label);
    check_balanced(ctx, before, what, err);
}

/* Every algorithm from an empty arena and from one that already has a block handed out --
 * the second is how the entry points are nested inside ph_compute_multi() and the batch. */
static void test_success_paths(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    run_all(ctx, "top level");
    ASSERT_OK(ph_context_set_whash_mode(ctx, PH_WHASH_FULL));
    run_all(ctx, "wHash full");
    ASSERT_OK(ph_context_set_whash_mode(ctx, PH_WHASH_FAST));

    ph_arena_mark_t outer = ph_arena_mark(ctx);
    ASSERT_PTR_NOT_NULL(ph_get_scratchpad(ctx, 5));
    run_all(ctx, "nested");
    ph_arena_release(ctx, outer);
    ASSERT(ph_arena_mark(ctx) == outer);

    ph_free(ctx);
    PASS("test_success_paths");
}

/* Early returns that are not allocation failures: a flat image (Radial's flat-projection
 * shortcut), a one-channel image (the colour algorithms refuse it), and configurations
 * that the setters would reject, poisoned directly to reach the in-function checks. */
static void test_error_paths(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    uint8_t flat[64 * 64 * 3];
    memset(flat, 128, sizeof(flat));
    ASSERT_OK(ph_load_from_pixels(ctx, flat, 64, 64, 3, 64 * 3));
    run_all(ctx, "flat image");

    uint8_t gray[64 * 64];
    for (int i = 0; i < 64 * 64; i++) {
        gray[i] = (uint8_t)(i * 7);
    }
    ASSERT_OK(ph_load_from_pixels(ctx, gray, 64, 64, 1, 64));
    run_all(ctx, "one channel");

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ctx->config.phash_dct_size = 33;
    run_all(ctx, "bad dct_size");
    ph_config_init_defaults(&ctx->config);
    ctx->config.block_size = 0;
    run_all(ctx, "bad block_size");
    ph_config_init_defaults(&ctx->config);
    /* A kernel wider than the builder allows: mHash fails after taking its block. */
    ctx->config.mhash_alpha = 10.0f;
    run_all(ctx, "oversized mHash kernel");
    ph_config_init_defaults(&ctx->config);

    ph_free(ctx);
    PASS("test_error_paths");
}

/* Every allocation an algorithm makes, failed one at a time. Each failure point hits a
 * different early return; the arena must come back to where it was on all of them. A
 * fresh context per point, so that the arena's own growth is among the failures. wHash
 * runs in full mode here: the fast mode takes nothing from the arena. */
static void test_allocation_failure_paths(void) {
#if !PH_SHIM_SUPPORTED
    printf("[SKIP] test_allocation_failure_paths: allocator shim unavailable in this build\n");
#else
    ph_shim_arm(0);
    ph_context_t *probe = NULL;
    ph_error_t perr = ph_create(&probe);
    long seen = ph_shim_count();
    ph_shim_disarm();
    ph_shim_reset();
    if (perr == PH_SUCCESS) {
        ph_free(probe);
    }
    if (seen <= 0) {
        printf("[SKIP] test_allocation_failure_paths: the shim does not reach the library\n");
        return;
    }

    char what[128];
    for (int a = 0; a < PH_ALGORITHM_COUNT; a++) {
        /* Count the allocations a clean run makes, then fail each in turn. */
        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
        ASSERT_OK(ph_context_set_whash_mode(ctx, PH_WHASH_FULL));
        ph_digest_t digest;
        ph_shim_arm(0);
        ph_error_t clean = ph_compute_digest(ctx, (ph_algorithm_t)a, &digest);
        long n = ph_shim_count();
        ph_shim_disarm();
        ph_shim_reset();
        ph_free(ctx);
        ASSERT_OK(clean);

        for (long k = 1; k <= n; k++) {
            ctx = NULL;
            ASSERT_OK(ph_create(&ctx));
            ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
            ASSERT_OK(ph_context_set_whash_mode(ctx, PH_WHASH_FULL));
            size_t before = ph_arena_mark(ctx);
            ph_shim_arm(k);
            ph_error_t err = ph_compute_digest(ctx, (ph_algorithm_t)a, &digest);
            ph_shim_disarm();
            ph_shim_reset();
            snprintf(what, sizeof(what), "%s, allocation %ld of %ld failed",
                     ph_algorithm_name((ph_algorithm_t)a), k, n);
            check_balanced(ctx, before, what, err);
            ph_free(ctx);
        }
    }
    PASS("test_allocation_failure_paths");
#endif
}

int main(void) {
    test_success_paths();
    test_error_paths();
    test_allocation_failure_paths();
    if (g_failures) {
        fprintf(stderr, "%d unbalanced arena use(s)\n", g_failures);
        return 1;
    }
    return 0;
}
