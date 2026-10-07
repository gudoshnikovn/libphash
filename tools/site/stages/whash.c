/* site_stages: wHash. */
#include "stages.h"

#include <stdlib.h>
#include <string.h>

/* ImageHash's remove_max_haar_ll on a size x size image: Haar levels down to one LL
 * coefficient, that coefficient zeroed, and the inverse levels back up, with the
 * library's own transforms in the library's order. */
static void whash_remove_max_haar_ll(float *d, int size) {
    float ta[4096], tb[4096];
    int s = size;
    while (s > 1) {
        ph_haar_2d_level(d, s, size, ta, tb);
        s /= 2;
    }
    d[0] = 0.0f;
    while (s < size) {
        s *= 2;
        ph_haar_2d_level_inverse(d, s, size, ta, tb);
    }
}

/* The median of an 8x8 band and its bits, as ph_median_bitpack() sets them: the mean of
 * the two central values, a bit set where a value is above it, bit i for value i. */
static uint64_t whash_bits(const float ll[64], float *median) {
    float sorted[64];
    memcpy(sorted, ll, sizeof(sorted));
    sort_floats(sorted, 64);
    *median = (sorted[31] + sorted[32]) * 0.5f;
    uint64_t bits = 0;
    for (int i = 0; i < 64; i++) {
        if (ll[i] > *median) {
            bits |= 1ULL << i;
        }
    }
    return bits;
}

/* One wHash computation, recomputed from its stages and checked against
 * ph_compute_whash() in the same mode and with the same removal setting: `size` is the
 * side of the grayscale reduction (16 in PH_WHASH_FAST), `grid` that reduction,
 * `coef` its decomposition in Mallat's layout (each level's LL band in the top-left
 * quarter of the previous one, its three detail bands around it), down to 8x8. */
typedef struct {
    int size, levels, remove;
    uint8_t *grid;
    float *coef;
    float ll[64], median;
    uint64_t bits;
} whash_stages_t;

static void whash_free(whash_stages_t *s) {
    free(s->grid);
    free(s->coef);
}

static int whash_run(ph_context_t *ctx, ph_whash_mode_t mode, int remove, whash_stages_t *s) {
    int w = ctx->image.width, h = ctx->image.height, min_dim = w < h ? w : h;
    s->remove = remove;
    if (mode == PH_WHASH_FAST) {
        s->size = 2 * PH_CORE_HASH_SIZE;
    } else {
        s->size = 1;
        while (s->size * 2 <= min_dim) {
            s->size *= 2;
        }
        if (s->size < PH_CORE_HASH_SIZE) {
            s->size = PH_CORE_HASH_SIZE;
        }
    }
    if (s->size > 4096) {
        return fail("image too large for the wHash stages", NULL);
    }
    const size_t n = (size_t)s->size * (size_t)s->size;
    s->grid = malloc(n);
    s->coef = malloc(n * sizeof(float));
    if (!s->grid || !s->coef) {
        return fail("out of memory", NULL);
    }
    const uint8_t *gray = ph_get_gray(ctx);
    int ok = mode == PH_WHASH_FAST ? ph_area_downscale(ctx, s->size, s->size, s->grid)
                                   : gray && ph_resize_box(gray, w, h, s->grid, s->size, s->size);
    if (!ok) {
        return fail("cannot reduce the image for wHash", NULL);
    }
    for (size_t i = 0; i < n; i++) {
        s->coef[i] = s->grid[i] / 255.0f;
    }
    if (remove) {
        whash_remove_max_haar_ll(s->coef, s->size);
    }
    float ta[4096], tb[4096];
    s->levels = 0;
    for (int size = s->size; size > PH_CORE_HASH_SIZE; size /= 2) {
        ph_haar_2d_level(s->coef, size, s->size, ta, tb);
        s->levels++;
    }
    for (int i = 0; i < 64; i++) {
        s->ll[i] = s->coef[(i / 8) * s->size + i % 8];
    }
    s->bits = whash_bits(s->ll, &s->median);

    uint64_t lib = 0;
    if (ph_context_set_whash_mode(ctx, mode) != PH_SUCCESS ||
        ph_context_set_whash_remove_max_haar_ll(ctx, remove) != PH_SUCCESS ||
        ph_compute_whash(ctx, &lib) != PH_SUCCESS) {
        return fail("ph_compute_whash failed", NULL);
    }
    char what[48];
    snprintf(what, sizeof(what), "wHash (%s, removal %s)", mode == PH_WHASH_FAST ? "fast" : "full",
             remove ? "on" : "off");
    return check_hash64(what, s->bits, lib);
}

static void whash_json(json_t *j, const char *key, const whash_stages_t *s, int with_coef) {
    json_t o = json_object(j, key);
    json_int(&o, "size", s->size);
    json_int(&o, "levels", s->levels);
    json_int(&o, "remove_max_haar_ll", s->remove);
    if (with_coef) {
        json_u8s(&o, "grid", s->grid, s->size * s->size);
        json_floats(&o, "coef", s->coef, s->size * s->size);
    }
    json_floats(&o, "ll", s->ll, 64);
    json_float(&o, "median", s->median);
    json_bits_lsb(&o, "bits", s->bits, 64);
    json_hex64(&o, "hash", s->bits);
    json_close_object(&o);
}

/* The four ways wHash can run: both modes, each with and without remove_max_haar_ll. */
static const struct {
    ph_whash_mode_t mode;
    int remove;
    const char *key;
} kinds[4] = {{PH_WHASH_FAST, 0, "fast"},
              {PH_WHASH_FAST, 1, "fast_removed"},
              {PH_WHASH_FULL, 0, "full"},
              {PH_WHASH_FULL, 1, "full_removed"}};

/* site_stages whash <image> <outdir>
 *     original.ppm, gray.pgm, and whash.json: for each of the four kinds[], the side of
 *     the grayscale reduction, the 8x8 LL band, its median, the bits in coefficient
 *     order and the hash (without the removal, also the reduction and its whole
 *     decomposition), every hash checked against ph_compute_whash() with those
 *     settings; and the hash of the image loaded with ph_context_set_load_grayscale(),
 *     in the default mode, checked the same way. */
int mode_whash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;
    whash_stages_t runs[COUNT(kinds)] = {{0}}, dec = {0};
    int bad = !write_gray_stages(ctx, outdir, &status);
    for (size_t k = 0; k < COUNT(kinds) && !bad; k++) {
        bad = whash_run(ctx, kinds[k].mode, kinds[k].remove, &runs[k]);
    }
    ph_free(ctx);
    if (!bad) {
        bad = load_decoder_gray(&ctx, argv[0]) || whash_run(ctx, PH_WHASH_FAST, 0, &dec);
        ph_free(ctx);
    }

    FILE *f = bad ? NULL : open_out(outdir, "whash.json");
    if (f) {
        json_t j = json_begin(f);
        json_int(&j, "width", w);
        json_int(&j, "height", h);
        for (size_t k = 0; k < COUNT(kinds); k++) {
            whash_json(&j, kinds[k].key, &runs[k], !kinds[k].remove);
        }
        json_hex64(&j, "hash_load_grayscale", dec.bits);
        json_end(&j);
        status |= fclose(f) != 0;
    }
    for (size_t k = 0; k < COUNT(kinds); k++) {
        whash_free(&runs[k]);
    }
    whash_free(&dec);
    return f ? status : 1;
}

/* One image of `site_stages whash-modes`. */
static int whash_modes_row(ph_context_t *ctx, json_t *row, const char *path) {
    for (size_t k = 0; k < COUNT(kinds); k++) {
        uint64_t hash = 0;
        if (ph_context_set_whash_mode(ctx, kinds[k].mode) != PH_SUCCESS ||
            ph_context_set_whash_remove_max_haar_ll(ctx, kinds[k].remove) != PH_SUCCESS ||
            ph_compute_whash(ctx, &hash) != PH_SUCCESS) {
            return fail("ph_compute_whash failed", path);
        }
        json_hex64(row, kinds[k].key, hash);
    }
    /* aHash's 8x8 area grid thresholded at its median instead of its mean, packed as
     * wHash packs its LL band: what the LL band amounts to. */
    uint8_t grid[64];
    float values[64], median;
    if (!ph_area_downscale(ctx, 8, 8, grid)) {
        return fail("ph_area_downscale failed", path);
    }
    for (int k = 0; k < 64; k++) {
        values[k] = grid[k];
    }
    json_hex64(row, "grid_median", whash_bits(values, &median));
    uint64_t ahash = 0;
    if (ph_compute_ahash(ctx, &ahash) != PH_SUCCESS) {
        return fail("ph_compute_ahash failed", path);
    }
    json_hex64(row, "ahash", ahash);
    return 0;
}

/* site_stages whash-modes <image>...
 *     One JSON line per image: its wHash as ph_compute_whash() computes it, each of the
 *     four kinds[]; aHash's 8x8 area grid thresholded at its median and packed the same
 *     way, which the LL band amounts to; and its aHash, for the count of bits each sets. */
int mode_whash_modes(int argc, char **argv) { return for_each_image(argc, argv, whash_modes_row); }
