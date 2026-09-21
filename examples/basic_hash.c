/* Loads an image and prints its pHash. Build/run:
 *   cc basic_hash.c -o basic_hash $(pkg-config --cflags --libs libphash)
 *   ./basic_hash path/to/image.jpg
 */
#include <libphash.h>
#include <stdio.h>

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

    /* Fast grayscale loading: skips the RGB-to-gray conversion pass. Fine here since
     * pHash only ever operates on grayscale; skip this call to load in color if you
     * also want a color hash (ph_compute_color_hash(), ph_compute_color_moments_hash())
     * from the same load. */
    ph_context_set_load_grayscale(ctx, 1);

    ph_error_t err = ph_load_from_file(ctx, argv[1]);
    if (err != PH_SUCCESS) {
        fprintf(stderr, "failed to load %s: %s (%s)\n", argv[1], ph_get_error_string(err),
                ph_get_last_error_message(ctx));
        ph_free(ctx);
        return 1;
    }

    uint64_t hash = 0;
    err = ph_compute_phash(ctx, &hash);
    if (err != PH_SUCCESS) {
        fprintf(stderr, "ph_compute_phash failed: %s\n", ph_get_error_string(err));
        ph_free(ctx);
        return 1;
    }

    char hex[17];
    if (ph_hash_to_hex(hash, hex, sizeof(hex)) != PH_SUCCESS) {
        fprintf(stderr, "ph_hash_to_hex failed\n");
        ph_free(ctx);
        return 1;
    }
    printf("pHash: %s\n", hex);

    ph_free(ctx);
    return 0;
}
