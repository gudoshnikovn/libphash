/* Finds the pictures that appear more than once among many files: hashes every file with
 * pHash, prints each hash as it would be stored, then every pair within a threshold,
 * closest first. Build/run:
 *   cc find_duplicates.c -o find_duplicates $(pkg-config --cflags --libs libphash)
 *   ./find_duplicates 7 a.jpg b.jpg c.png ...
 * The threshold is the most bits two hashes of one picture may differ in; measure it on
 * your own images (docs/theory/comparing.md).
 */
#include <libphash.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int distance;
    size_t a, b;
} pair_t;

static int by_distance(const void *x, const void *y) {
    const pair_t *p = x, *q = y;
    return p->distance - q->distance;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <max-bits> <image>...\n", argv[0]);
        return 1;
    }
    char *end;
    long max_bits = strtol(argv[1], &end, 10);
    if (*end != '\0' || max_bits < 0 || max_bits > 64) {
        fprintf(stderr, "max-bits must be a number from 0 to 64\n");
        return 1;
    }
    size_t n = (size_t)(argc - 2);
    ph_batch_item_t *items = calloc(n, sizeof(*items));
    if (!items) {
        return 1;
    }
    for (size_t i = 0; i < n; i++) {
        items[i].path = argv[i + 2];
    }

    /* Every file hashed with pHash on all CPUs; a file that cannot be read fails alone. */
    ph_error_t err = ph_hash_files(items, n, PH_HASH_PHASH, 0);
    if (err != PH_SUCCESS) {
        fprintf(stderr, "batch failed: %s\n", ph_get_error_string(err));
        free(items);
        return 1;
    }

    /* A stored hash carries what decides its value: the algorithm, the library version,
     * and whether JPEG went through libjpeg-turbo or stb_image. The settings are the
     * defaults here; a program that changes them stores them too. */
    printf("# algorithm=%s version=%d native-jpeg=%d\n", ph_algorithm_name(PH_ALGO_PHASH),
           ph_version_number(), ph_can_use_jpeg());
    for (size_t i = 0; i < n; i++) {
        char hex[17];
        if (items[i].status != PH_SUCCESS) {
            fprintf(stderr, "%s: %s\n", items[i].path, ph_get_error_string(items[i].status));
        } else if (ph_hash_to_hex(items[i].hashes[0], hex, sizeof(hex)) == PH_SUCCESS) {
            printf("%s  %s\n", hex, items[i].path);
        }
    }

    /* Every pair compared once: n * (n - 1) / 2 Hamming distances, a linear scan per file. */
    size_t found = 0, room = 16;
    pair_t *pairs = malloc(room * sizeof(*pairs));
    for (size_t i = 0; i < n && pairs; i++) {
        for (size_t j = i + 1; j < n; j++) {
            if (items[i].status != PH_SUCCESS || items[j].status != PH_SUCCESS) {
                continue;
            }
            int d = ph_hamming_distance(items[i].hashes[0], items[j].hashes[0]);
            if (d > max_bits) {
                continue;
            }
            if (found == room) {
                pair_t *more = realloc(pairs, 2 * room * sizeof(*pairs));
                if (!more) {
                    free(pairs);
                    pairs = NULL;
                    break;
                }
                pairs = more;
                room *= 2;
            }
            pairs[found++] = (pair_t){d, i, j};
        }
    }
    if (!pairs) {
        free(items);
        return 1;
    }
    qsort(pairs, found, sizeof(*pairs), by_distance);
    for (size_t k = 0; k < found; k++) {
        printf("%2d bits  %s  %s\n", pairs[k].distance, items[pairs[k].a].path,
               items[pairs[k].b].path);
    }
    printf("pairs within %ld bits: %zu\n", max_bits, found);

    free(pairs);
    free(items);
    return 0;
}
