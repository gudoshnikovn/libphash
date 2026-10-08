/* A context configured in one function, a refused value that changes nothing, the one
 * setting that reads back differently, and when a setting takes effect: at the next hash
 * or at the next load. Build/run:
 *   cc configure.c -o configure $(pkg-config --cflags --libs libphash)
 *   ./configure image.jpg
 */
#include <libphash.h>
#include <stdio.h>

/* Every context that computes hashes to be compared with each other is configured by
 * this one function, so that they all compute them alike. Each setter returns
 * PH_ERR_INVALID_ARGUMENT for a value out of range and leaves the context as it was. */
static ph_error_t configure(ph_context_t *ctx) {
    ph_error_t err;
    if ((err = ph_context_set_max_pixels(ctx, 50u * 1000u * 1000u)) != PH_SUCCESS ||
        (err = ph_context_set_alpha_mode(ctx, PH_ALPHA_BLEND_WHITE)) != PH_SUCCESS ||
        (err = ph_context_set_gray_weights(ctx, 1, 1, 1)) != PH_SUCCESS ||
        (err = ph_context_set_block_params(ctx, 8)) != PH_SUCCESS) {
        return err;
    }
    return PH_SUCCESS;
}

static int fail(ph_context_t *ctx, const char *what, ph_error_t err) {
    fprintf(stderr, "%s: %s (%s)\n", what, ph_get_error_string(err),
            ph_get_last_error_message(ctx));
    ph_free(ctx);
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <image>\n", argv[0]);
        return 1;
    }
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        fprintf(stderr, "ph_create failed\n");
        return 1;
    }
    ph_error_t err = configure(ctx);
    if (err != PH_SUCCESS) {
        return fail(ctx, "configure", err);
    }

    /* The grayscale weights are stored normalized to a sum of 128, so they are the one
     * setting with a getter. A digest's size follows from the settings too. */
    int r, g, b;
    size_t bmh_size;
    if (ph_context_get_gray_weights(ctx, &r, &g, &b) != PH_SUCCESS ||
        ph_digest_info(ctx, PH_ALGO_BMH, &bmh_size, NULL) != PH_SUCCESS) {
        return fail(ctx, "read back", PH_ERR_INVALID_ARGUMENT);
    }
    printf("gray weights (1, 1, 1) are stored as (%d, %d, %d)\n", r, g, b);
    printf("BMH digest at block_size 8: %zu bytes\n", bmh_size);

    if ((err = ph_load_from_file(ctx, argv[1])) != PH_SUCCESS) {
        return fail(ctx, argv[1], err);
    }
    uint64_t before, after;
    if ((err = ph_compute_phash(ctx, &before)) != PH_SUCCESS) {
        return fail(ctx, "pHash", err);
    }

    /* A 9x9 block does not fit 64 bits: refused, and pHash is computed as before. */
    err = ph_context_set_phash_params(ctx, 32, 9);
    printf("set_phash_params(32, 9): %s\n", ph_get_error_string(err));
    if ((err = ph_compute_phash(ctx, &after)) != PH_SUCCESS) {
        return fail(ctx, "pHash", err);
    }
    int failed = 0;
    if (after != before) {
        printf("a refused value changed the hash\n");
        failed = 1;
    }

    /* A hash setting is read by the next hash: no reload. */
    ph_context_set_phash_params(ctx, 32, 6);
    if ((err = ph_compute_phash(ctx, &after)) != PH_SUCCESS) {
        return fail(ctx, "pHash", err);
    }
    printf("pHash, 8x8 block: %016llx\npHash, 6x6 block: %016llx\n", (unsigned long long)before,
           (unsigned long long)after);

    /* A load setting is read by the next load: the image in the context stays in color
     * until it is loaded again. */
    ph_context_set_load_grayscale(ctx, 1);
    ph_digest_t color;
    ph_error_t before_reload = ph_compute_color_hash(ctx, &color);
    if ((err = ph_load_from_file(ctx, argv[1])) != PH_SUCCESS) {
        return fail(ctx, argv[1], err);
    }
    ph_error_t after_reload = ph_compute_color_hash(ctx, &color);
    printf("ColorHash before the reload: %s\n", ph_get_error_string(before_reload));
    printf("ColorHash after the reload:  %s\n", ph_get_error_string(after_reload));

    ph_free(ctx);
    if (r + g + b != 128 || bmh_size != 8 || after == before || before_reload != PH_SUCCESS ||
        after_reload != PH_ERR_REQUIRES_COLOR) {
        printf("unexpected result\n");
        failed = 1;
    }
    return failed;
}
