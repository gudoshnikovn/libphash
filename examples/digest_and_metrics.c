/* Hashes two images with every digest algorithm, compares each pair with the function
 * that digest's kind calls for, shows what a mismatched comparison returns, and stores a
 * digest as text and reads it back. Build/run:
 *   cc digest_and_metrics.c -o digest_and_metrics $(pkg-config --cflags --libs libphash)
 *   ./digest_and_metrics a.jpg b.jpg
 */
#include <libphash.h>
#include <stdio.h>
#include <string.h>

/* Loads `path` into `ctx` in color, so the color hashes can run on it too. */
static int load(ph_context_t *ctx, const char *path) {
    ph_error_t err = ph_load_from_file(ctx, path);
    if (err != PH_SUCCESS) {
        fprintf(stderr, "failed to load %s: %s (%s)\n", path, ph_get_error_string(err),
                ph_get_last_error_message(ctx));
        return 0;
    }
    return 1;
}

/* Compares two digests of one algorithm with the function its kind calls for. */
static void compare(const char *name, const ph_digest_t *a, const ph_digest_t *b) {
    double score = 0.0;
    switch ((ph_digest_kind_t)a->kind) {
        case PH_DIGEST_KIND_BITS:
            printf("%-13s %3d of %d bits differ, similarity %.3f\n", name,
                   ph_hamming_distance_digest(a, b), a->size * 8, ph_similarity_digest(a, b));
            break;
        case PH_DIGEST_KIND_COEFFICIENTS:
            if (ph_radial_similarity(a, b, &score) == PH_SUCCESS) {
                printf("%-13s peak correlation %.3f (same image at %.1f and above)\n", name, score,
                       PH_RADIAL_PCC_THRESHOLD);
            } else {
                printf("%-13s no angular structure to compare\n", name);
            }
            break;
        case PH_DIGEST_KIND_HISTOGRAM:
            if (ph_histogram_intersection(a, b, &score) == PH_SUCCESS) {
                printf("%-13s histogram intersection %.3f\n", name, score);
            }
            break;
        case PH_DIGEST_KIND_VECTOR16:
        case PH_DIGEST_KIND_VECTOR:
            printf("%-13s L2 distance %.2f\n", name, ph_l2_distance(a, b));
            break;
        default:
            printf("%-13s kind %d: no comparison\n", name, a->kind);
            break;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <image-a> <image-b>\n", argv[0]);
        return 1;
    }

    static const ph_algorithm_t algos[] = {PH_ALGO_MHASH, PH_ALGO_BMH, PH_ALGO_RADIAL,
                                           PH_ALGO_COLOR_HASH, PH_ALGO_COLOR_MOMENTS};

    enum {
        N_ALGOS = sizeof(algos) / sizeof(algos[0]),
    };

    ph_digest_t da[N_ALGOS], db[N_ALGOS];

    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        fprintf(stderr, "ph_create failed\n");
        return 1;
    }

    /* One context, reused: each load replaces the previous image and its cached work. */
    for (int img = 0; img < 2; img++) {
        if (!load(ctx, argv[1 + img])) {
            ph_free(ctx);
            return 1;
        }
        for (int i = 0; i < N_ALGOS; i++) {
            ph_error_t err = ph_compute_digest(ctx, algos[i], img == 0 ? &da[i] : &db[i]);
            if (err != PH_SUCCESS) {
                fprintf(stderr, "%s failed: %s\n", ph_algorithm_name(algos[i]),
                        ph_get_error_string(err));
                ph_free(ctx);
                return 1;
            }
        }
    }

    for (int i = 0; i < N_ALGOS; i++) {
        compare(ph_algorithm_name(algos[i]), &da[i], &db[i]);
    }

    /* The wrong metric is refused, not answered with a meaningless number: a radial
     * digest is quantized coefficients, not a bit vector. */
    printf("hamming distance of two radial digests: %d (refused)\n",
           ph_hamming_distance_digest(&da[2], &db[2]));

    /* Store a digest as text and read it back; the text carries the kind. */
    char hex[PH_DIGEST_HEX_BUFFER_SIZE];
    ph_digest_t back;
    if (ph_digest_to_hex(&da[1], hex, sizeof(hex)) != PH_SUCCESS ||
        ph_digest_from_hex(hex, &back) != PH_SUCCESS) {
        fprintf(stderr, "hex round trip failed\n");
        ph_free(ctx);
        return 1;
    }
    printf("BMH as text: %s\n", hex);
    printf("read back: %s\n", (back.size == da[1].size && back.kind == da[1].kind &&
                               memcmp(back.data, da[1].data, back.size) == 0)
                                  ? "identical"
                                  : "DIFFERENT");

    ph_free(ctx);
    return 0;
}
