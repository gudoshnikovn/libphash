#include "libphash.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* libFuzzer harness for the whole untrusted-input path: decode arbitrary bytes, then
 * hash what was decoded, under a configuration that the input itself chooses.
 *
 * The configuration comes from the input's last bytes, which are *also* passed to the
 * decoder: every format the library reads ignores bytes after its end marker (or
 * reports truncation either way), so a plain image file is a working seed as it is,
 * and mutating the tail explores configurations while the image still decodes. Only
 * inputs long enough to carry a configuration get one; shorter inputs run with the
 * defaults, so short-input coverage is not lost.
 *
 * One algorithm per input, also chosen by the tail: each costs about as much as the
 * decode, and running all of them on every input would spend most of the budget on
 * hashing the same reduced buffer. Over a corpus every algorithm is reached.
 *
 * The oracle, beyond crashes and sanitizer reports: a successful load has positive
 * dimensions and 1 or 3 channels, and hashing is deterministic -- the same call on the
 * same context returns the same result twice. A violation aborts. */

/* Bytes taken from the end of the input to choose the configuration. */
enum {
    CONFIG_BYTES = 8,
};

/* Every value below is valid for its setter, except where `raw` selects the raw byte:
 * that share of inputs exercises the setters' own range checks. Radial and mHash take
 * small sets instead of their full ranges, whose top end costs tens of milliseconds an
 * iteration without reaching any code a smaller value does not. */
static const int k_radial_projections[] = {40, 90, 180, 360};
static const int k_radial_samples[] = {2, 16, 128, 256};
static const float k_radial_sigma[] = {0.5f, 1.0f, 3.5f, 21.0f};
static const int k_mhash_size[] = {62, 128, 256, 512};
static const float k_mhash_alpha[] = {1.5f, 2.0f, 2.5f, 3.0f};
static const float k_gamma[] = {0.5f, 1.0f, 2.2f, 1000.0f};

struct config {
    const uint8_t *b; /* CONFIG_BYTES bytes, or NULL for the defaults */
};

static unsigned bits(const struct config *c, int byte, int shift, unsigned mask) {
    return c->b ? ((unsigned)c->b[byte] >> shift) & mask : 0u;
}

static void configure(ph_context_t *ctx, const struct config *c) {
    /* Without a cap this runs at the library default (256 MP), so a corpus entry with a
     * huge decoded-size header (a valid PNG/JPEG can claim gigapixel dimensions in a
     * few bytes) spends the time budget on one allocation instead of on decoder logic.
     * 4 MP is larger than every seed in tests/fuzz/seeds/. */
    (void)ph_context_set_max_pixels(ctx, 4 * 1024 * 1024);
    if (!c->b) {
        return;
    }
    /* One input in sixteen passes raw bytes to some setters instead. 9, not 0 or 15: the
     * last bytes of a real file are often 0x00 or 0xFF. */
    int raw = bits(c, 6, 4, 15u) == 9u;

    /* Load-time options: all four decode scales, grayscale or colour, with or without
     * EXIF orientation, every alpha mode. */
    (void)ph_context_set_decode_scale(ctx, (ph_decode_scale_t)bits(c, 0, 0, 3u));
    (void)ph_context_set_load_grayscale(ctx, (int)bits(c, 0, 2, 1u));
    (void)ph_context_set_auto_orient(ctx, !bits(c, 0, 3, 1u));
    (void)ph_context_set_alpha_mode(ctx, (ph_alpha_mode_t)(raw ? c->b[6] : bits(c, 0, 4, 3u)));

    /* Algorithm parameters. */
    int reduction = 4 + (int)bits(c, 1, 0, 7u) % 5;                   /* 4..8 */
    int dct = reduction + (int)bits(c, 1, 3, 31u) % (33 - reduction); /* reduction..32 */
    (void)ph_context_set_phash_params(ctx, raw ? c->b[5] : dct, reduction);
    (void)ph_context_set_block_params(ctx, raw ? c->b[4] : 2 + (int)bits(c, 2, 0, 31u));
    (void)ph_context_set_whash_mode(ctx, (ph_whash_mode_t)bits(c, 2, 5, 1u));
    (void)ph_context_set_whash_remove_max_haar_ll(ctx, (int)bits(c, 2, 6, 1u));
    (void)ph_context_set_radial_params(ctx, k_radial_projections[bits(c, 3, 0, 3u)],
                                       k_radial_samples[bits(c, 3, 2, 3u)],
                                       k_radial_sigma[bits(c, 3, 4, 3u)]);
    (void)ph_context_set_gamma(ctx, k_gamma[bits(c, 3, 6, 3u)]);
    (void)ph_context_set_mhash_params(ctx, k_mhash_alpha[bits(c, 4, 0, 3u)],
                                      (float)bits(c, 4, 2, 3u), k_mhash_size[bits(c, 4, 4, 3u)]);
    (void)ph_context_set_gray_weights(ctx, (int)bits(c, 5, 0, 15u), (int)bits(c, 5, 4, 15u),
                                      (int)bits(c, 6, 0, 15u) + 1);
}

static void check(int condition) {
    if (!condition) {
        abort();
    }
}

static void hash_once(ph_context_t *ctx, unsigned which) {
    uint64_t h1 = 0, h2 = 0;
    ph_digest_t d1, d2;
    ph_error_t e1, e2;
    switch (which) {
#define U64(fn)                                        \
    e1 = fn(ctx, &h1);                                 \
    e2 = fn(ctx, &h2);                                 \
    check(e1 == e2 && (e1 != PH_SUCCESS || h1 == h2)); \
    break
#define DIGEST(fn)                                                              \
    memset(&d1, 0, sizeof(d1));                                                 \
    memset(&d2, 0, sizeof(d2));                                                 \
    e1 = fn(ctx, &d1);                                                          \
    e2 = fn(ctx, &d2);                                                          \
    check(e1 == e2 && (e1 != PH_SUCCESS || memcmp(&d1, &d2, sizeof(d1)) == 0)); \
    break
        case 0:
            U64(ph_compute_ahash);
        case 1:
            U64(ph_compute_dhash);
        case 2:
            U64(ph_compute_phash);
        case 3:
            U64(ph_compute_whash);
        case 4:
            DIGEST(ph_compute_mhash);
        case 5:
            DIGEST(ph_compute_color_hash);
        case 6:
            DIGEST(ph_compute_bmh);
        case 7:
            DIGEST(ph_compute_color_moments_hash);
        case 8:
            DIGEST(ph_compute_radial_hash);
        default: {
            /* ph_compute_multi() over any subset of the four uint64_t algorithms, including
             * the empty one, against the same algorithms one at a time. */
            uint32_t flags = which & 0xFu;
            uint64_t multi[PH_HASH_FLAGS_COUNT] = {0};
            if (ph_compute_multi(ctx, flags, multi) == PH_SUCCESS) {
                int slot = 0;
                for (unsigned bit = 0; bit < PH_HASH_FLAGS_COUNT; bit++) {
                    if (!(flags & (1u << bit))) {
                        continue;
                    }
                    static ph_error_t (*const single[])(ph_context_t *, uint64_t *) = {
                        ph_compute_ahash, ph_compute_dhash, ph_compute_phash, ph_compute_whash};
                    check(single[bit](ctx, &h1) == PH_SUCCESS && h1 == multi[slot]);
                    slot++;
                }
            }
            break;
        }
#undef U64
#undef DIGEST
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        return 0;
    }
    struct config c = {size >= 2 * CONFIG_BYTES ? data + size - CONFIG_BYTES : NULL};
    configure(ctx, &c);

    if (ph_load_from_memory(ctx, data, size) == PH_SUCCESS) {
        int w = 0, h = 0, channels = 0;
        ph_context_get_dimensions(ctx, &w, &h, &channels);
        check(w > 0 && h > 0 && (channels == 1 || channels == 3));
        /* 9 algorithms + 16 ph_compute_multi() masks: algorithms get 9 of 25 shares. */
        hash_once(ctx, (unsigned)((c.b ? c.b[7] : size) % 25u));
    }

    ph_free(ctx);
    return 0;
}
