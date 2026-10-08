/* site_stages: what the library makes of a file before any hash reads it, for the page
 * on preparing an image (docs/theory/preparation.md). Every load goes through
 * load_image(), so the settings of take_load_settings() apply. */
#include "stages.h"

#include <stdlib.h>

/* --8<-- [start:loaded] */
/* site_stages loaded <outdir> <image>...: each image as the library loaded it, written as
 * NN.ppm (NN.pgm when it holds one channel), and its grayscale as NN-gray.pgm, NN counting
 * the images from 00; one JSON line per image with its size and channels. A
 * `--load=<settings>` between the images loads the ones after it so. */
int mode_loaded(int argc, char **argv) {
    int status = 0, n = 0;
    for (int i = 1; i < argc && !status; i++) {
        if (take_load_settings(argv[i], &status)) {
            continue;
        }
        ph_context_t *ctx = NULL;
        if (load_image(&ctx, argv[i])) {
            return 1;
        }
        int w = ctx->image.width, h = ctx->image.height, ch = ctx->image.channels;
        char name[32];
        snprintf(name, sizeof(name), "%02d.%s", n, ch == 1 ? "pgm" : "ppm");
        status |= write_pnm(argv[0], name, ctx->image.raw_rgb, w, h, ch);
        const uint8_t *gray = ph_get_gray(ctx);
        snprintf(name, sizeof(name), "%02d-gray.pgm", n);
        status |= gray ? write_pnm(argv[0], name, gray, w, h, 1) : fail("no grayscale", argv[i]);
        json_t j = json_begin(stdout);
        json_string(&j, "file", argv[i]);
        json_int(&j, "index", n++);
        json_int(&j, "width", w);
        json_int(&j, "height", h);
        json_int(&j, "channels", ch);
        json_end(&j);
        ph_free(ctx);
    }
    return status;
}

/* --8<-- [end:loaded] */

/* --8<-- [start:area] */
/* site_stages area <width> <height> <image>: the image's grayscale reduced to
 * width x height by the library's area average (ph_area_downscale(), what aHash, pHash,
 * wHash and BMH reduce with), as one JSON object. */
int mode_area(int argc, char **argv) {
    (void)argc;
    int dw = atoi(argv[0]), dh = atoi(argv[1]);
    if (dw <= 0 || dh <= 0 || dw > 64 || dh > 64) {
        return fail("the grid must be 1 to 64 cells on a side", NULL);
    }
    ph_context_t *ctx = NULL;
    if (load_image(&ctx, argv[2])) {
        return 1;
    }
    uint8_t grid[64 * 64];
    int ok = ph_area_downscale(ctx, dw, dh, grid);
    json_t j = json_begin(stdout);
    json_int(&j, "width", ctx->image.width);
    json_int(&j, "height", ctx->image.height);
    json_u8s(&j, "grid", grid, ok ? dw * dh : 0);
    json_end(&j);
    ph_free(ctx);
    return ok ? 0 : fail("ph_area_downscale failed", argv[2]);
}

/* --8<-- [end:area] */
