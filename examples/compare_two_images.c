/* Compares two images with pHash and prints a normalized similarity score. Build/run:
 *   cc compare_two_images.c -o compare_two_images $(pkg-config --cflags --libs libphash)
 *   ./compare_two_images a.jpg b.jpg
 */
#include <libphash.h>
#include <stdio.h>

static int hash_file(const char *path, uint64_t *out_hash) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        fprintf(stderr, "ph_create failed\n");
        return 1;
    }
    ph_context_set_load_grayscale(ctx, 1);

    ph_error_t err = ph_load_from_file(ctx, path);
    if (err != PH_SUCCESS) {
        fprintf(stderr, "failed to load %s: %s (%s)\n", path, ph_get_error_string(err),
                ph_get_last_error_message(ctx));
        ph_free(ctx);
        return 1;
    }

    err = ph_compute_phash(ctx, out_hash);
    ph_free(ctx);
    if (err != PH_SUCCESS) {
        fprintf(stderr, "ph_compute_phash failed for %s: %s\n", path, ph_get_error_string(err));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <image-a> <image-b>\n", argv[0]);
        return 1;
    }

    uint64_t hash_a, hash_b;
    if (hash_file(argv[1], &hash_a) != 0 || hash_file(argv[2], &hash_b) != 0)
        return 1;

    int distance = ph_hamming_distance(hash_a, hash_b);
    double similarity = ph_similarity(hash_a, hash_b);

    printf("Hamming distance: %d/64 bits\n", distance);
    printf("Similarity:       %.4f (1.0 = identical, 0.0 = every bit differs)\n", similarity);

    return 0;
}
