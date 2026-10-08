/* Hashes many files at once on a pool of worker threads, with a configuration, a
 * progress callback and a per-file status. Build/run:
 *   cc batch_hash.c -o batch_hash $(pkg-config --cflags --libs libphash)
 *   ./batch_hash a.jpg b.png c.webp ...
 */
#include <libphash.h>
#include <stdio.h>
#include <stdlib.h>

/* Called on the worker threads, possibly several at once: it only reads its arguments
 * and writes one line, so it needs no lock. */
static void on_progress(size_t done, size_t total, void *user_data) {
    (void)user_data;
    fprintf(stderr, "%zu/%zu done\n", done, total);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <image>...\n", argv[0]);
        return 1;
    }
    size_t n = (size_t)(argc - 1);

    ph_batch_item_t *items = calloc(n, sizeof(*items));
    if (!items) {
        return 1;
    }
    for (size_t i = 0; i < n; i++) {
        items[i].path = argv[i + 1];
    }

    /* The template context: every worker copies its configuration. Here, images up to
     * 50 megapixels and JPEGs decoded at half size, which is cheaper on large photos. */
    ph_context_t *config = NULL;
    if (ph_create(&config) != PH_SUCCESS ||
        ph_context_set_max_pixels(config, 50u * 1000u * 1000u) != PH_SUCCESS ||
        ph_context_set_decode_scale(config, PH_DECODE_SCALE_HALF) != PH_SUCCESS) {
        fprintf(stderr, "could not set up the configuration\n");
        ph_free(config);
        free(items);
        return 1;
    }

    ph_batch_options_t options;
    if (ph_batch_options_init(&options) != PH_SUCCESS) {
        ph_free(config);
        free(items);
        return 1;
    }
    options.config = config;
    options.threads = 0; /* one worker per CPU this process may use */
    options.on_progress = on_progress;

    /* hashes[] holds one value per flag, in ascending bit order: dHash, then pHash. */
    uint32_t flags = PH_HASH_DHASH | PH_HASH_PHASH;
    /* --8<-- [start:statuses] */
    ph_error_t err = ph_hash_files_ex(items, n, flags, &options);
    if (err != PH_SUCCESS) {
        /* The batch as a whole failed (bad arguments, no memory for the pool). */
        fprintf(stderr, "batch failed: %s\n", ph_get_error_string(err));
        ph_free(config);
        free(items);
        return 1;
    }

    /* A file that could not be read or decoded fails on its own; the others are hashed. */
    int failed = 0;
    for (size_t i = 0; i < n; i++) {
        if (items[i].status == PH_SUCCESS) {
            char dhex[17], phex[17];
            if (ph_hash_to_hex(items[i].hashes[0], dhex, sizeof(dhex)) == PH_SUCCESS &&
                ph_hash_to_hex(items[i].hashes[1], phex, sizeof(phex)) == PH_SUCCESS) {
                printf("%s  dHash %s  pHash %s\n", items[i].path, dhex, phex);
            }
        } else {
            printf("%s  %s\n", items[i].path, ph_get_error_string(items[i].status));
            failed++;
        }
    }
    printf("%zu hashed, %d failed\n", n - (size_t)failed, failed);
    /* --8<-- [end:statuses] */

    ph_free(config);
    free(items);
    return 0;
}
