#include "libphash.h"
#include <stddef.h>
#include <stdint.h>

/* libFuzzer harness: feed arbitrary bytes straight into the decoders and
 * run every hash algorithm over whatever gets decoded. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS)
        return 0;

    /* R27: without a cap this runs at the library default (256 MP), so a corpus
     * entry with a huge decoded-size header (a valid PNG/JPEG can claim gigapixel
     * dimensions in a few bytes) spends the fuzzer's time budget on one giant
     * allocation instead of exploring decoder logic. 4 MP (e.g. 2048x2048) is
     * larger than every tests/data fixture, so the corpus still round-trips, while
     * keeping each iteration fast enough that libFuzzer's 90s CI budget is spent
     * on finding real bugs rather than on OOM/timeouts. auto_orient stays on
     * (its default) -- the EXIF path is a fuzz target in its own right (R31), not
     * something to fuzz around. */
    (void)ph_context_set_max_pixels(ctx, 4 * 1024 * 1024);

    if (ph_load_from_memory(ctx, data, size) == PH_SUCCESS) {
        uint64_t hash64;
        ph_digest_t digest;

        (void)ph_compute_ahash(ctx, &hash64);
        (void)ph_compute_dhash(ctx, &hash64);
        (void)ph_compute_phash(ctx, &hash64);
        (void)ph_compute_whash(ctx, &hash64);
        (void)ph_compute_mhash(ctx, &hash64);
        (void)ph_compute_color_hash(ctx, &digest);
        (void)ph_compute_bmh(ctx, &digest);
        (void)ph_compute_color_moments_hash(ctx, &digest);
        (void)ph_compute_radial_hash(ctx, &digest);
    }

    ph_free(ctx);
    return 0;
}
