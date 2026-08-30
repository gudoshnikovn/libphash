#include "libphash.h"
#include <stddef.h>
#include <stdint.h>

/* libFuzzer harness: feed arbitrary bytes straight into the decoders and
 * run every hash algorithm over whatever gets decoded. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS)
        return 0;

    if (ph_load_from_memory(ctx, data, size) == PH_SUCCESS) {
        uint64_t hash64;
        ph_digest_t digest;

        (void)ph_compute_ahash(ctx, &hash64);
        (void)ph_compute_dhash(ctx, &hash64);
        (void)ph_compute_phash(ctx, &hash64);
        (void)ph_compute_whash(ctx, &hash64);
        (void)ph_compute_mhash(ctx, &hash64);
        (void)ph_compute_color_hash(ctx, &hash64);
        (void)ph_compute_bmh(ctx, &digest);
        (void)ph_compute_color_moments_hash(ctx, &digest);
        (void)ph_compute_radial_hash(ctx, &digest);
    }

    ph_free(ctx);
    return 0;
}
