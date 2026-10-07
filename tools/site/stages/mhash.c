/* site_stages: mHash. */
#include "stages.h"

#include <stdlib.h>
#include <string.h>

/* mHash's response to its kernel at every pixel of the n x n image, edges replicated, the
 * taps summed in raster order: in double, as the definition reads; or in float, as a
 * direct implementation of it would compute. */
static void mhash_response(const uint8_t *img, int n, const float *kernel, int side, int in_double,
                           float *out) {
    const int half = side / 2;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            double accd = 0.0;
            float accf = 0.0f;
            for (int ky = 0; ky < side; ky++) {
                int sy = y + ky - half < 0 ? 0 : y + ky - half >= n ? n - 1 : y + ky - half;
                for (int kx = 0; kx < side; kx++) {
                    int sx = x + kx - half < 0 ? 0 : x + kx - half >= n ? n - 1 : x + kx - half;
                    float k = kernel[ky * side + kx];
                    uint8_t v = img[(size_t)sy * (size_t)n + (size_t)sx];
                    if (in_double) {
                        accd += (double)k * (double)v;
                    } else {
                        accf += k * (float)v;
                    }
                }
            }
            out[(size_t)y * (size_t)n + (size_t)x] = in_double ? (float)accd : accf;
        }
    }
}

/* The 576 bits of mHash from its 31x31 block grid, packed as ph_compute_mhash() packs
 * them: nine per 3x3 window at stride 4, each against its window's mean, MSB first. */
static void mhash_bits(const float *blocks, uint8_t digest[PH_MH_BYTES]) {
    memset(digest, 0, PH_MH_BYTES);
    int bit = 0;
    for (int wy = 0; wy < PH_MH_WINDOWS_PER_AXIS; wy++) {
        for (int wx = 0; wx < PH_MH_WINDOWS_PER_AXIS; wx++) {
            const float *w =
                &blocks[wy * PH_MH_WINDOW_STRIDE * PH_MH_GRID + wx * PH_MH_WINDOW_STRIDE];
            float sum = 0.0f;
            for (int y = 0; y < PH_MH_WINDOW; y++) {
                for (int x = 0; x < PH_MH_WINDOW; x++) {
                    sum += w[y * PH_MH_GRID + x];
                }
            }
            float mean = sum / (float)(PH_MH_WINDOW * PH_MH_WINDOW);
            for (int y = 0; y < PH_MH_WINDOW; y++) {
                for (int x = 0; x < PH_MH_WINDOW; x++, bit++) {
                    if (w[y * PH_MH_GRID + x] > mean) {
                        digest[bit / 8] |= (uint8_t)(0x80u >> (bit % 8));
                    }
                }
            }
        }
    }
}

/* The block grid summed from a per-pixel response, in the response's precision. */
static void mhash_blocks_from(const float *response, int n, int block, int in_double,
                              float *blocks) {
    for (int by = 0; by < PH_MH_GRID; by++) {
        for (int bx = 0; bx < PH_MH_GRID; bx++) {
            double sd = 0.0;
            float sf = 0.0f;
            for (int y = by * block; y < (by + 1) * block; y++) {
                for (int x = bx * block; x < (bx + 1) * block; x++) {
                    float v = response[(size_t)y * (size_t)n + (size_t)x];
                    sd += v;
                    sf += v;
                }
            }
            blocks[by * PH_MH_GRID + bx] = in_double ? (float)sd : sf;
        }
    }
}

/* The digest the definition gives, evaluated pixel by pixel in double or in float:
 * `response` (n x n) is scratch. */
static void mhash_direct_digest(const uint8_t *norm, int n, const float *kernel, int side,
                                int block, int in_double, float *response,
                                uint8_t digest[PH_MH_BYTES]) {
    float blocks[PH_MH_GRID * PH_MH_GRID];
    mhash_response(norm, n, kernel, side, in_double, response);
    mhash_blocks_from(response, n, block, in_double, blocks);
    mhash_bits(blocks, digest);
}

/* One mHash computation with the given parameters, recomputed from its stages and checked
 * against ph_compute_mhash() with the same parameters: `blurred` is the grayscale image
 * after the sigma-1 blur, which no parameter changes; `resized` the image normalized to
 * size x size, and `norm` the same equalized. */
typedef struct {
    float alpha, level;
    int size, block, side;
    float kernel[PH_MH_MAX_KERNEL_SIDE * PH_MH_MAX_KERNEL_SIDE];
    float blocks[PH_MH_GRID * PH_MH_GRID];
    uint8_t digest[PH_MH_BYTES];
    uint8_t *norm, *resized;
} mhash_stages_t;

static void mhash_free(mhash_stages_t *s) {
    free(s->norm);
    free(s->resized);
}

static int mhash_run(ph_context_t *ctx, const uint8_t *blurred, float alpha, float level, int size,
                     mhash_stages_t *s) {
    s->alpha = alpha;
    s->level = level;
    s->size = size;
    s->block = size / PH_MH_GRID;
    const size_t npix = (size_t)size * (size_t)size;
    s->norm = malloc(npix);
    s->resized = malloc(npix);
    if (!s->norm || !s->resized) {
        return fail("out of memory", NULL);
    }
    if (!ph_resize_mitchell(blurred, ctx->image.width, ctx->image.height, s->resized, size, size)) {
        return fail("ph_resize_mitchell failed", NULL);
    }
    memcpy(s->norm, s->resized, npix);
    ph_equalize_histogram(s->norm, npix, PH_MH_EQUALIZE_LEVELS);
    s->side = ph_mh_kernel(alpha, level, s->kernel, PH_MH_MAX_KERNEL_SIDE);
    if (s->side <= 0) {
        return fail("ph_mh_kernel refused the parameters", NULL);
    }
    uint8_t *scratch = malloc(ph_mh_block_sums_scratch(size, s->side / 2));
    if (!scratch) {
        return fail("out of memory", NULL);
    }
    ph_mh_block_sums(s->norm, size, s->block, s->kernel, s->side, scratch, s->blocks);
    free(scratch);
    mhash_bits(s->blocks, s->digest);

    ph_digest_t lib;
    if (ph_context_set_mhash_params(ctx, alpha, level, size) != PH_SUCCESS ||
        ph_compute_mhash(ctx, &lib) != PH_SUCCESS) {
        return fail("ph_compute_mhash failed", NULL);
    }
    char what[64];
    snprintf(what, sizeof(what), "mHash (alpha %g, level %g, size %d)", (double)alpha,
             (double)level, size);
    return check_digest(what, s->digest, PH_MH_BYTES, &lib);
}

/* One run as a JSON object, under `key` or as an array element. */
static void mhash_json(json_t *j, const char *key, const mhash_stages_t *s, int with_kernel) {
    json_t o = json_object(j, key);
    json_double(&o, "alpha", s->alpha);
    json_double(&o, "level", s->level);
    json_int(&o, "size", s->size);
    json_int(&o, "block", s->block);
    json_int(&o, "side", s->side);
    if (with_kernel) {
        json_floats(&o, "kernel", s->kernel, s->side * s->side);
    }
    json_floats(&o, "blocks", s->blocks, PH_MH_GRID * PH_MH_GRID);
    json_hexbytes(&o, "digest", s->digest, PH_MH_BYTES);
    json_close_object(&o);
}

/* The grayscale image of `ctx` blurred at mHash's sigma, as ph_compute_mhash() blurs it;
 * malloc'd. */
static uint8_t *mhash_blur(ph_context_t *ctx) {
    size_t n = (size_t)ctx->image.width * (size_t)ctx->image.height;
    const uint8_t *gray = ph_get_gray(ctx);
    uint8_t *out = malloc(n);
    float *scratch = malloc(n * sizeof(float));
    if (gray && out && scratch) {
        ph_gaussian_blur_sigma(gray, ctx->image.width, ctx->image.height, PH_MH_BLUR_SIGMA, scratch,
                               out);
    } else {
        free(out);
        out = NULL;
    }
    free(scratch);
    return out;
}

/* The scales and sizes `site_stages mhash` shows, the default last in each list. */
static const float levels[] = {0.0f, 2.0f, PH_MH_LEVEL};
static const int sizes[] = {256, 1024, PH_MH_IMAGE_SIZE};

/* site_stages mhash <image> <outdir>
 *     original.ppm, gray.pgm, blurred.pgm (the sigma-1 blur), resized.pgm (normalized to
 *     512x512), equalized.pgm, response.f32 (the response to the kernel at every pixel,
 *     raw floats) and mhash.json: the kernel, the 31x31 block grid and the digest, for
 *     the defaults and for the other kernel scales and sizes, each checked against
 *     ph_compute_mhash() with those parameters; the digest the definition gives
 *     evaluated pixel by pixel, in double and in float; and the digest of the image
 *     loaded with ph_context_set_load_grayscale(), checked the same way. */
int mode_mhash(int argc, char **argv) {
    (void)argc;
    const char *outdir = argv[1];
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[0])) {
        return 1;
    }
    int w = ctx->image.width, h = ctx->image.height;
    int status = 0;
    mhash_stages_t by_level[COUNT(levels)] = {{0}}, by_size[COUNT(sizes)] = {{0}}, dec = {0};
    uint8_t *blurred = NULL, *dec_blurred = NULL;
    float *response = NULL, *scratch = NULL;
    int bad = !write_gray_stages(ctx, outdir, &status) || !(blurred = mhash_blur(ctx));
    for (size_t k = 0; k < COUNT(levels) && !bad; k++) {
        bad = mhash_run(ctx, blurred, PH_MH_ALPHA, levels[k], PH_MH_IMAGE_SIZE, &by_level[k]);
    }
    for (size_t k = 0; k < COUNT(sizes) && !bad; k++) {
        bad = mhash_run(ctx, blurred, PH_MH_ALPHA, PH_MH_LEVEL, sizes[k], &by_size[k]);
    }
    ph_free(ctx);
    const mhash_stages_t *def = &by_level[COUNT(levels) - 1];
    const int n = PH_MH_IMAGE_SIZE;

    /* The response at every pixel, as the definition computes it, in double and in float;
     * and the bits each gives when summed over the blocks directly. */
    uint8_t digest_double[PH_MH_BYTES], digest_float[PH_MH_BYTES];
    if (!bad) {
        response = malloc((size_t)n * n * sizeof(float));
        scratch = malloc((size_t)n * n * sizeof(float));
        bad = !response || !scratch;
    }
    if (!bad) {
        mhash_direct_digest(def->norm, n, def->kernel, def->side, def->block, 1, response,
                            digest_double);
        mhash_direct_digest(def->norm, n, def->kernel, def->side, def->block, 0, scratch,
                            digest_float);
        status |= write_pnm(outdir, "blurred.pgm", blurred, w, h, 1);
        status |= write_pnm(outdir, "resized.pgm", def->resized, n, n, 1);
        status |= write_pnm(outdir, "equalized.pgm", def->norm, n, n, 1);
        status |= write_f32(outdir, "response.f32", response, (size_t)n * n);
    }
    if (!bad) {
        bad = load_decoder_gray(&ctx, argv[0]) || !(dec_blurred = mhash_blur(ctx)) ||
              mhash_run(ctx, dec_blurred, PH_MH_ALPHA, PH_MH_LEVEL, PH_MH_IMAGE_SIZE, &dec);
        ph_free(ctx);
    }

    FILE *f = bad ? NULL : open_out(outdir, "mhash.json");
    if (f) {
        json_t j = json_begin(f);
        json_int(&j, "width", w);
        json_int(&j, "height", h);
        json_int(&j, "grid", PH_MH_GRID);
        json_int(&j, "window", PH_MH_WINDOW);
        json_int(&j, "stride", PH_MH_WINDOW_STRIDE);
        mhash_json(&j, "default", def, 1);
        json_t a = json_array(&j, "levels");
        for (size_t k = 0; k < COUNT(levels); k++) {
            mhash_json(&a, NULL, &by_level[k], 1);
        }
        json_close_array(&a);
        a = json_array(&j, "sizes");
        for (size_t k = 0; k < COUNT(sizes); k++) {
            mhash_json(&a, NULL, &by_size[k], 0);
        }
        json_close_array(&a);
        json_hexbytes(&j, "digest_direct_double", digest_double, PH_MH_BYTES);
        json_hexbytes(&j, "digest_direct_float", digest_float, PH_MH_BYTES);
        json_hexbytes(&j, "digest_load_grayscale", dec.digest, PH_MH_BYTES);
        json_end(&j);
        status |= fclose(f) != 0;
    }
    for (size_t k = 0; k < COUNT(levels); k++) {
        mhash_free(&by_level[k]);
    }
    for (size_t k = 0; k < COUNT(sizes); k++) {
        mhash_free(&by_size[k]);
    }
    mhash_free(&dec);
    free(blurred);
    free(dec_blurred);
    free(response);
    free(scratch);
    return f ? status : 1;
}

/* One image of `site_stages mhash-direct`. */
static int mhash_direct_row(ph_context_t *ctx, json_t *row, const char *path) {
    (void)path;
    const int n = PH_MH_IMAGE_SIZE;
    mhash_stages_t s = {0};
    uint8_t *blurred = mhash_blur(ctx);
    float *response = malloc((size_t)n * n * sizeof(float));
    int bad = !blurred || !response || mhash_run(ctx, blurred, PH_MH_ALPHA, PH_MH_LEVEL, n, &s);
    for (int in_double = 1; in_double >= 0 && !bad; in_double--) {
        uint8_t digest[PH_MH_BYTES];
        mhash_direct_digest(s.norm, n, s.kernel, s.side, s.block, in_double, response, digest);
        json_int(row, in_double ? "double" : "float", bits_apart(digest, s.digest, PH_MH_BYTES));
    }
    mhash_free(&s);
    free(blurred);
    free(response);
    return bad;
}

/* site_stages mhash-direct <image>...
 *     One JSON line per image: how many bits of its mHash the definition, evaluated
 *     pixel by pixel in double and in float, gives differently from ph_compute_mhash(). */
int mode_mhash_direct(int argc, char **argv) {
    return for_each_image(argc, argv, mhash_direct_row);
}
