/* Algorithms as values: ph_algorithm_t, the dispatcher, the digest shape and the names.
 *
 * ph_digest_shape() below is the only place a digest's size and kind are decided. The
 * ph_compute_* functions that return a digest take theirs from it (through
 * ph_digest_begin()), and ph_digest_info() reports it, so the answer to "what will this
 * algorithm return" and what it actually returns come from the same lines. */

#include "internal.h"
#include <string.h>

/* The ph_hash_flags_t bit of a uint64_t algorithm is 1 << its ph_algorithm_t value; the
 * header promises it, and batch/multi code relies on neither going out of step. */
_Static_assert(PH_HASH_AHASH == 1u << PH_ALGO_AHASH, "flag bit == 1 << algorithm");
_Static_assert(PH_HASH_DHASH == 1u << PH_ALGO_DHASH, "flag bit == 1 << algorithm");
_Static_assert(PH_HASH_PHASH == 1u << PH_ALGO_PHASH, "flag bit == 1 << algorithm");
_Static_assert(PH_HASH_WHASH == 1u << PH_ALGO_WHASH, "flag bit == 1 << algorithm");
_Static_assert(PH_HASH_FLAGS_COUNT == PH_ALGO_WHASH + 1,
               "the uint64_t algorithms are exactly the first PH_HASH_FLAGS_COUNT values");

static const char *const PH_ALGORITHM_NAMES[] = {
    "ahash", "dhash", "phash", "whash", "bmh", "mhash", "radial", "color_hash", "color_moments",
};
_Static_assert(sizeof(PH_ALGORITHM_NAMES) / sizeof(*PH_ALGORITHM_NAMES) == PH_ALGORITHM_COUNT,
               "every algorithm needs a name");
_Static_assert(PH_ALGO_COLOR_MOMENTS + 1 == PH_ALGORITHM_COUNT,
               "PH_ALGORITHM_COUNT must follow the last algorithm");

static int ph_algorithm_is_valid(ph_algorithm_t algo) {
    return (int)algo >= 0 && (int)algo < PH_ALGORITHM_COUNT;
}

/* A uint64_t hash as a digest: 8 bytes, most significant first. */
#define PH_UINT64_DIGEST_BYTES 8

void ph_digest_shape(const struct ph_context_config *config, ph_algorithm_t algo, uint8_t *size,
                     uint8_t *kind) {
    switch (algo) {
        case PH_ALGO_AHASH:
        case PH_ALGO_DHASH:
        case PH_ALGO_PHASH:
        case PH_ALGO_WHASH:
            *size = PH_UINT64_DIGEST_BYTES;
            *kind = PH_DIGEST_KIND_BITS;
            return;
        case PH_ALGO_BMH: {
            /* One bit per block of a block_size x block_size grid. The setter keeps
             * block_size <= PH_BLOCK_MAX_SIZE, whose grid exactly fills a digest; the cap
             * is defence in depth for a configuration written by another route. */
            uint64_t blocks = (uint64_t)config->block_size * (uint64_t)config->block_size;
            uint64_t bytes = (blocks + 7) / 8;
            *size = (uint8_t)(bytes > PH_DIGEST_MAX_BYTES ? PH_DIGEST_MAX_BYTES : bytes);
            *kind = PH_DIGEST_KIND_BITS;
            return;
        }
        case PH_ALGO_MHASH:
            *size = (uint8_t)PH_MH_BYTES; /* always a 31x31 grid, whatever the preset */
            *kind = PH_DIGEST_KIND_BITS;
            return;
        case PH_ALGO_RADIAL:
            *size = (uint8_t)PH_RADIAL_COEFFS; /* DCT coefficients, not one per angle */
            *kind = PH_DIGEST_KIND_COEFFICIENTS;
            return;
        case PH_ALGO_COLOR_HASH:
            *size = (uint8_t)PH_COLOR_BINS;
            *kind = PH_DIGEST_KIND_HISTOGRAM;
            return;
        case PH_ALGO_COLOR_MOMENTS:
            *size = (uint8_t)PH_COLOR_MOMENTS_DIGEST_BYTES;
            *kind = PH_DIGEST_KIND_VECTOR16;
            return;
        case PH_ALGO_FORCE_INT32_:
        default:
            *size = 0;
            *kind = PH_DIGEST_KIND_UNSPECIFIED;
            return;
    }
}

void ph_digest_begin(ph_digest_t *out, const ph_context_t *ctx, ph_algorithm_t algo) {
    memset(out, 0, sizeof(*out));
    ph_digest_shape(&ctx->config, algo, &out->size, &out->kind);
}

PH_API ph_error_t ph_digest_info(const ph_context_t *ctx, ph_algorithm_t algo, size_t *out_size,
                                 ph_digest_kind_t *out_kind) {
    if (!ph_algorithm_is_valid(algo))
        return PH_ERR_INVALID_ARGUMENT;
    struct ph_context_config defaults;
    const struct ph_context_config *config = &defaults;
    if (ctx)
        config = &ctx->config;
    else
        ph_config_init_defaults(&defaults);

    uint8_t size = 0, kind = 0;
    ph_digest_shape(config, algo, &size, &kind);
    if (out_size)
        *out_size = size;
    if (out_kind)
        *out_kind = (ph_digest_kind_t)kind;
    return PH_SUCCESS;
}

/* Stores a uint64_t hash as a digest: 8 bytes, most significant first. */
static ph_error_t ph_uint64_digest(ph_context_t *ctx, ph_algorithm_t algo, ph_error_t err,
                                   uint64_t hash, ph_digest_t *out) {
    if (err != PH_SUCCESS)
        return err;
    ph_digest_begin(out, ctx, algo);
    for (int i = 0; i < PH_UINT64_DIGEST_BYTES; i++)
        out->data[i] = (uint8_t)(hash >> (8 * (PH_UINT64_DIGEST_BYTES - 1 - i)));
    return PH_SUCCESS;
}

PH_API ph_error_t ph_compute_digest(ph_context_t *ctx, ph_algorithm_t algo,
                                    ph_digest_t *out_digest) {
    /* The digest algorithms check their own arguments; the uint64_t ones write into a
     * local hash, so the output pointer is checked here for them. */
    uint64_t hash = 0;
    switch (algo) {
        case PH_ALGO_AHASH:
            if (!out_digest)
                return PH_ERR_INVALID_ARGUMENT;
            return ph_uint64_digest(ctx, algo, ph_compute_ahash(ctx, &hash), hash, out_digest);
        case PH_ALGO_DHASH:
            if (!out_digest)
                return PH_ERR_INVALID_ARGUMENT;
            return ph_uint64_digest(ctx, algo, ph_compute_dhash(ctx, &hash), hash, out_digest);
        case PH_ALGO_PHASH:
            if (!out_digest)
                return PH_ERR_INVALID_ARGUMENT;
            return ph_uint64_digest(ctx, algo, ph_compute_phash(ctx, &hash), hash, out_digest);
        case PH_ALGO_WHASH:
            if (!out_digest)
                return PH_ERR_INVALID_ARGUMENT;
            return ph_uint64_digest(ctx, algo, ph_compute_whash(ctx, &hash), hash, out_digest);
        case PH_ALGO_BMH:
            return ph_compute_bmh(ctx, out_digest);
        case PH_ALGO_MHASH:
            return ph_compute_mhash(ctx, out_digest);
        case PH_ALGO_RADIAL:
            return ph_compute_radial_hash(ctx, out_digest);
        case PH_ALGO_COLOR_HASH:
            return ph_compute_color_hash(ctx, out_digest);
        case PH_ALGO_COLOR_MOMENTS:
            return ph_compute_color_moments_hash(ctx, out_digest);
        case PH_ALGO_FORCE_INT32_:
        default:
            return PH_ERR_INVALID_ARGUMENT;
    }
}

PH_API const char *ph_algorithm_name(ph_algorithm_t algo) {
    return ph_algorithm_is_valid(algo) ? PH_ALGORITHM_NAMES[algo] : "unknown";
}

PH_API ph_error_t ph_algorithm_from_name(const char *name, ph_algorithm_t *out_algo) {
    if (!name || !out_algo)
        return PH_ERR_INVALID_ARGUMENT;
    for (int i = 0; i < PH_ALGORITHM_COUNT; i++) {
        if (strcmp(name, PH_ALGORITHM_NAMES[i]) == 0) {
            *out_algo = (ph_algorithm_t)i;
            return PH_SUCCESS;
        }
    }
    return PH_ERR_INVALID_ARGUMENT;
}
