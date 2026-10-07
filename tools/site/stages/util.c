/* site_stages: errors, loading, writing files, and the check every stage mode ends with. */
#include "stages.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int fail(const char *what, const char *detail) {
    fprintf(stderr, "site_stages: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    return 1;
}

static int load_with(ph_context_t **ctx, const char *path, int decoder_gray) {
    if (ph_create(ctx) != PH_SUCCESS) {
        return fail("ph_create failed", NULL);
    }
    ph_error_t err = ph_context_set_load_grayscale(*ctx, decoder_gray);
    if (err == PH_SUCCESS) {
        err = ph_load_from_file(*ctx, path);
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
