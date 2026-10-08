/* site_stages: errors, loading, writing files, and the check every stage mode ends with. */
#include "stages.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int fail(const char *what, const char *detail) {
    fprintf(stderr, "site_stages: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    return 1;
}

/* --8<-- [start:load-settings] */
/* How load_image() loads: the library's defaults, unless a mode that takes a list of
 * images met `--load=<settings>` before an image (take_load_settings()). */
typedef struct {
    int alpha;      /* a ph_alpha_mode_t, or -1 for the default */
    int scale;      /* a ph_decode_scale_t */
    int orient;     /* ph_context_set_auto_orient() */
    int gray;       /* ph_context_set_load_grayscale(): the decoder converts to grayscale */
    int weights[4]; /* r, g, b and a shift: grayscale computed here, (r R + g G + b B) >> shift,
                     * and loaded as a one-channel image; a shift of 0 leaves it to the library */
} load_settings_t;

static const load_settings_t defaults = {-1, PH_DECODE_SCALE_FULL, 1, 0, {0, 0, 0, 0}};
static load_settings_t settings = {-1, PH_DECODE_SCALE_FULL, 1, 0, {0, 0, 0, 0}};

/* One `name=value` of a --load option; 0 when it is not one this tool knows. */
static int set_one(load_settings_t *s, const char *item) {
    static const char *const alphas[] = {"grey", "white", "black", "ignore"};
    static const char *const scales[] = {"full", "half", "quarter", "eighth"};
    for (int k = 0; k < 4; k++) {
        char want[32];
        snprintf(want, sizeof(want), "alpha=%s", alphas[k]);
        if (strcmp(item, want) == 0) {
            s->alpha = k; /* PH_ALPHA_BLEND_GREY … PH_ALPHA_IGNORE, in that order */
            return 1;
        }
        snprintf(want, sizeof(want), "scale=%s", scales[k]);
        if (strcmp(item, want) == 0) {
            s->scale = k; /* PH_DECODE_SCALE_FULL … PH_DECODE_SCALE_EIGHTH */
            return 1;
        }
    }
    if (strcmp(item, "orient=off") == 0) {
        s->orient = 0;
        return 1;
    }
    if (strcmp(item, "gray=decoder") == 0) {
        s->gray = 1;
        return 1;
    }
    int *w = s->weights;
    return sscanf(item, "weights=%d/%d/%d/%d", &w[0], &w[1], &w[2], &w[3]) == 4 && w[0] >= 0 &&
           w[1] >= 0 && w[2] >= 0 && w[3] > 0 && w[3] < 16 && w[0] + w[1] + w[2] <= 1 << w[3];
}

int take_load_settings(const char *arg, int *status) {
    static const char prefix[] = "--load=";
    if (strncmp(arg, prefix, sizeof(prefix) - 1) != 0) {
        return 0;
    }
    load_settings_t s = defaults;
    char list[256];
    snprintf(list, sizeof(list), "%s", arg + sizeof(prefix) - 1);
    for (char *item = strtok(list, ","); item; item = strtok(NULL, ",")) {
        if (strcmp(item, "default") != 0 && !set_one(&s, item)) {
            *status = fail("unknown load setting", item);
            return 1;
        }
    }
    settings = s;
    return 1;
}

/* The settings' own grayscale, (r R + g G + b B) >> shift per pixel, replaces the loaded
 * color image. */
static ph_error_t own_grayscale(ph_context_t *ctx) {
    const int *wt = settings.weights;
    size_t n = (size_t)ctx->image.width * (size_t)ctx->image.height;
    uint8_t *gray = malloc(n ? n : 1);
    if (!gray) {
        return PH_ERR_ALLOCATION_FAILED;
    }
    const uint8_t *p = ctx->image.raw_rgb;
    for (size_t i = 0; i < n; i++, p += 3) {
        gray[i] = (uint8_t)((wt[0] * p[0] + wt[1] * p[1] + wt[2] * p[2]) >> wt[3]);
    }
    ph_error_t err =
        ph_load_from_pixels(ctx, gray, ctx->image.width, ctx->image.height, 1, ctx->image.width);
    free(gray);
    return err;
}

static int load_with(ph_context_t **ctx, const char *path, int decoder_gray) {
    if (ph_create(ctx) != PH_SUCCESS) {
        return fail("ph_create failed", NULL);
    }
    decoder_gray |= settings.gray;
    ph_error_t err = ph_context_set_load_grayscale(*ctx, decoder_gray);
    if (err == PH_SUCCESS && settings.alpha >= 0) {
        err = ph_context_set_alpha_mode(*ctx, (ph_alpha_mode_t)settings.alpha);
    }
    if (err == PH_SUCCESS) {
        err = ph_context_set_decode_scale(*ctx, (ph_decode_scale_t)settings.scale);
    }
    if (err == PH_SUCCESS) {
        err = ph_context_set_auto_orient(*ctx, settings.orient);
    }
    if (err == PH_SUCCESS) {
        err = ph_load_from_file(*ctx, path);
    }
    if (err == PH_SUCCESS && settings.weights[3] && (*ctx)->image.channels == 3) {
        err = own_grayscale(*ctx);
    }
    if (err != PH_SUCCESS) {
        fprintf(stderr, "site_stages: cannot load %s%s: %s (%s)\n", path,
                decoder_gray ? " in grayscale" : "", ph_get_error_string(err),
                ph_get_last_error_message(*ctx));
        ph_free(*ctx);
        *ctx = NULL;
        return 1;
    }
    return 0;
}

/* --8<-- [end:load-settings] */

int load_image(ph_context_t **ctx, const char *path) { return load_with(ctx, path, 0); }

int load_decoder_gray(ph_context_t **ctx, const char *path) { return load_with(ctx, path, 1); }

/* <dir>/<name> opened in `mode`, or NULL after saying why. */
static FILE *open_in(const char *dir, const char *name, const char *mode) {
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) {
        fail("path too long", name);
        return NULL;
    }
    FILE *f = fopen(path, mode);
    if (!f) {
        fail(path, strerror(errno));
    }
    return f;
}

FILE *open_out(const char *dir, const char *name) { return open_in(dir, name, "w"); }

/* Writes `n` bytes after `header` (may be empty) and closes the file. */
static int write_raw(const char *dir, const char *name, const char *header, const void *data,
                     size_t size, size_t n) {
    FILE *f = open_in(dir, name, "wb");
    if (!f) {
        return 1;
    }
    int ok = fputs(header, f) >= 0 && fwrite(data, size, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    return ok ? 0 : fail("cannot write", name);
}

int write_pnm(const char *dir, const char *name, const uint8_t *px, int w, int h, int channels) {
    char header[64];
    snprintf(header, sizeof(header), "P%d\n%d %d\n255\n", channels == 3 ? 6 : 5, w, h);
    return write_raw(dir, name, header, px, 1, (size_t)w * (size_t)h * (size_t)channels);
}

int write_f32(const char *dir, const char *name, const float *v, size_t n) {
    return write_raw(dir, name, "", v, sizeof(float), n);
}

const uint8_t *write_gray_stages(ph_context_t *ctx, const char *outdir, int *status) {
    int w = ctx->image.width, h = ctx->image.height;
    if (ctx->image.channels == 3) {
        *status |= write_pnm(outdir, "original.ppm", ctx->image.raw_rgb, w, h, 3);
    }
    const uint8_t *gray = ph_get_gray(ctx);
    if (!gray) {
        fail("ph_get_gray failed", NULL);
        return NULL;
    }
    *status |= write_pnm(outdir, "gray.pgm", gray, w, h, 1);
    return gray;
}

void sort_floats(float *v, int n) {
    for (int i = 1; i < n; i++) {
        for (int k = i; k > 0 && v[k - 1] > v[k]; k--) {
            float t = v[k];
            v[k] = v[k - 1];
            v[k - 1] = t;
        }
    }
}

int bits_apart(const uint8_t *a, const uint8_t *b, int bytes) {
    int n = 0;
    for (int i = 0; i < bytes; i++) {
        n += __builtin_popcount((unsigned)(a[i] ^ b[i]));
    }
    return n;
}

int check_hash64(const char *what, uint64_t stages, uint64_t lib) {
    if (stages == lib) {
        return 0;
    }
    fprintf(stderr,
            "site_stages: %s recomputed from its stages (%016llx) differs from the "
            "library's (%016llx)\n",
            what, (unsigned long long)stages, (unsigned long long)lib);
    return 1;
}

int check_digest(const char *what, const uint8_t *stages, int bytes, const ph_digest_t *lib) {
    if (lib->size == bytes && memcmp(lib->data, stages, (size_t)bytes) == 0) {
        return 0;
    }
    if (lib->size != bytes) {
        fprintf(stderr,
                "site_stages: %s recomputed from its stages has %d bytes, the library's "
                "%d\n",
                what, bytes, (int)lib->size);
    } else {
        fprintf(stderr,
                "site_stages: %s recomputed from its stages differs from the library's in "
                "%d bits\n",
                what, bits_apart(lib->data, stages, bytes));
    }
    return 1;
}

int for_each_image(int argc, char **argv, image_row_fn row) {
    for (int i = 0; i < argc; i++) {
        ph_context_t *ctx = NULL;
        if (load_image(&ctx, argv[i])) {
            return 1;
        }
        json_t j = json_begin(stdout);
        json_string(&j, "file", argv[i]);
        int bad = row(ctx, &j, argv[i]);
        ph_free(ctx);
        if (bad) {
            fputc('\n', stdout);
            return 1;
        }
        json_end(&j);
    }
    return 0;
}
