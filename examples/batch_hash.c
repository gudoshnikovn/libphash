/* Hashes many files at once on a pool of worker threads, with a per-file status. By
 * default through ph_hash_files_ex(): a configuration template, a progress line, and
 * Ctrl-C to stop. With --defaults, through the plain ph_hash_files(), which hashes every
 * file as a freshly created context would. Build/run:
 *   cc batch_hash.c -o batch_hash $(pkg-config --cflags --libs libphash)
 *   ./batch_hash [--defaults] a.jpg b.png c.webp ...
 */
#include <libphash.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --8<-- [start:callbacks] */
/* Set by Ctrl-C and read by the workers. A signal handler may store to a lock-free atomic,
 * and an atomic needs no lock between threads. */
static atomic_int stop_requested;

static void on_interrupt(int sig) {
    (void)sig;
    atomic_store(&stop_requested, 1);
}

/* Called on the worker threads before each item; returning 0 stops the batch. */
static int should_continue(void *user_data) {
    (void)user_data;
    return !atomic_load(&stop_requested);
}

/* Called on the worker threads after each item, possibly several at once: it only reads
 * its arguments and writes one line, so it needs no lock. */
static void on_progress(size_t done, size_t total, void *user_data) {
    (void)user_data;
    fprintf(stderr, "%zu/%zu done\n", done, total);
}

/* --8<-- [end:callbacks] */

int main(int argc, char **argv) {
    int defaults = argc > 1 && strcmp(argv[1], "--defaults") == 0;
    char **paths = argv + 1 + defaults;
    size_t n = (size_t)(argc - 1 - defaults);
    if (n == 0) {
        fprintf(stderr, "usage: %s [--defaults] <image>...\n", argv[0]);
        return 1;
    }

    /* --8<-- [start:items] */
    /* One item per file: the path in, the hashes and a status out. */
    ph_batch_item_t *items = calloc(n, sizeof(*items));
    if (!items) {
        return 1;
    }
    for (size_t i = 0; i < n; i++) {
        items[i].path = paths[i];
    }
    /* hashes[] holds one value per flag, in ascending bit order: dHash, then pHash. */
    uint32_t flags = PH_HASH_DHASH | PH_HASH_PHASH;
    /* --8<-- [end:items] */

    ph_context_t *config = NULL;
    ph_error_t err;
    if (defaults) {
        /* --8<-- [start:plain] */
        /* Every file as a freshly created context hashes it, on one worker per CPU this
         * process may use; the call returns after the last file. */
        err = ph_hash_files(items, n, flags, 0);
        /* --8<-- [end:plain] */
    } else {
        /* --8<-- [start:options] */
        /* The template context: every worker copies its configuration. Here, images up
         * to 50 megapixels and JPEGs decoded at half size: cheaper on large photos, and
         * other hashes than the defaults give, in a build whose JPEG decoder is
         * libjpeg-turbo. */
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
        options.threads = 4; /* four workers at most, and so four decoded images */
        options.should_continue = should_continue;
        options.on_progress = on_progress;

        signal(SIGINT, on_interrupt);
        err = ph_hash_files_ex(items, n, flags, &options);
        /* --8<-- [end:options] */
    }

    /* --8<-- [start:statuses] */
    if (err != PH_SUCCESS && err != PH_ERR_CANCELLED) {
        /* The batch as a whole failed (bad arguments, no memory for the pool). */
        fprintf(stderr, "batch failed: %s\n", ph_get_error_string(err));
        ph_free(config);
        free(items);
        return 1;
    }

    /* A file that could not be read or decoded fails on its own; the others are hashed.
     * After a cancellation, the files never started carry PH_ERR_CANCELLED. */
    int failed = 0, skipped = 0;
    for (size_t i = 0; i < n; i++) {
        if (items[i].status == PH_SUCCESS) {
            char dhex[17], phex[17];
            if (ph_hash_to_hex(items[i].hashes[0], dhex, sizeof(dhex)) == PH_SUCCESS &&
                ph_hash_to_hex(items[i].hashes[1], phex, sizeof(phex)) == PH_SUCCESS) {
                printf("%s  dHash %s  pHash %s\n", items[i].path, dhex, phex);
            }
        } else if (items[i].status == PH_ERR_CANCELLED) {
            skipped++;
        } else {
            printf("%s  %s\n", items[i].path, ph_get_error_string(items[i].status));
            failed++;
        }
    }
    printf("%zu hashed, %d failed, %d not started\n", n - (size_t)(failed + skipped), failed,
           skipped);
    /* --8<-- [end:statuses] */

    ph_free(config);
    free(items);
    return 0;
}
