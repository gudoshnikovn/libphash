#include "internal.h"

#define PH_HASH_FLAGS_ALL                                                                          \
    (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH | PH_HASH_COLOR_HASH)

PH_API ph_error_t ph_compute_multi(ph_context_t *ctx, uint32_t flags, uint64_t out[]) {
    if (!ctx || !ctx->image.is_loaded || !out || flags == 0 ||
        (flags & ~(uint32_t)PH_HASH_FLAGS_ALL)) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    int idx = 0;
    ph_error_t err;

    if (flags & PH_HASH_AHASH) {
        if ((err = ph_compute_ahash(ctx, &out[idx])) != PH_SUCCESS)
            return err;
        idx++;
    }
    if (flags & PH_HASH_DHASH) {
        if ((err = ph_compute_dhash(ctx, &out[idx])) != PH_SUCCESS)
            return err;
        idx++;
    }
    if (flags & PH_HASH_PHASH) {
        if ((err = ph_compute_phash(ctx, &out[idx])) != PH_SUCCESS)
            return err;
        idx++;
    }
    if (flags & PH_HASH_WHASH) {
        if ((err = ph_compute_whash(ctx, &out[idx])) != PH_SUCCESS)
            return err;
        idx++;
    }
    if (flags & PH_HASH_COLOR_HASH) {
        if ((err = ph_compute_color_hash(ctx, &out[idx])) != PH_SUCCESS)
            return err;
        idx++;
    }

    return PH_SUCCESS;
}
