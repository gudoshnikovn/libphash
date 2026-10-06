/* Hashes two images with the four 64-bit algorithms and prints how many bits each pair
 * of hashes differs in. Build/run:
 *   cc hash_distance.c -o hash_distance $(pkg-config --cflags --libs libphash)
 *   ./hash_distance original.jpg edited.jpg
 */
#include <libphash.h>
#include <stdio.h>

#define ALL_FOUR (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH)

/* aHash, dHash, pHash and wHash of one file, in that order: ph_compute_multi() writes one
 * slot per flag, in ascending bit order, from a single grayscale conversion. */
static int hash_file(const char *path, uint64_t out[PH_HASH_FLAGS_COUNT]) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        fprintf(stderr, "ph_create failed\n");
        return 1;
    }
    ph_error_t err = ph_load_from_file(ctx, path);
    if (err == PH_SUCCESS) {
        err = ph_compute_multi(ctx, ALL_FOUR, out);
    }
    if (err != PH_SUCCESS) {
        fprintf(stderr, "%s: %s (%s)\n", path, ph_get_error_string(err),
                ph_get_last_error_message(ctx));
    }
    ph_free(ctx);
    return err != PH_SUCCESS;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <image-a> <image-b>\n", argv[0]);
        return 1;
    }
    uint64_t a[PH_HASH_FLAGS_COUNT], b[PH_HASH_FLAGS_COUNT];
    if (hash_file(argv[1], a) != 0 || hash_file(argv[2], b) != 0) {
        return 1;
    }

    static const char *const names[PH_HASH_FLAGS_COUNT] = {"aHash", "dHash", "pHash", "wHash"};
    for (int i = 0; i < PH_HASH_FLAGS_COUNT; i++) {
        printf("%s  %016llx  %016llx  %2d bits differ\n", names[i], (unsigned long long)a[i],
               (unsigned long long)b[i], ph_hamming_distance(a[i], b[i]));
    }
    return 0;
}
