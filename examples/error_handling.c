/* Tries to hash each path it is given and says what went wrong, by error code, for the
 * ones that fail -- the decisions a caller makes on each code. Build/run:
 *   cc error_handling.c -o error_handling $(pkg-config --cflags --libs libphash)
 *   ./error_handling photo.jpg missing.jpg notes.txt truncated.jpg
 */
#include <libphash.h>
#include <stdio.h>

/* What a caller does with each outcome of a load or a hash. */
static const char *what_to_do(ph_error_t err) {
    switch (err) {
        case PH_SUCCESS:
            return "hashed";
        case PH_ERR_IO:
            return "path unusable (missing, unreadable, not a file, empty): skip it";
        case PH_ERR_UNSUPPORTED_FORMAT:
            return "not an image format the library reads: skip it";
        case PH_ERR_CORRUPT_DATA:
            return "a damaged image: skip it, retrying will not help";
        case PH_ERR_DECODER_UNAVAILABLE:
            return "a format this build has no decoder for: use a build with it";
        case PH_ERR_IMAGE_TOO_LARGE:
            return "over the max_pixels limit: raise it only for trusted input";
        case PH_ERR_ALLOCATION_FAILED:
            return "out of memory: retry later or with less in flight";
        case PH_ERR_REQUIRES_COLOR:
            return "a color hash of a grayscale load: load in color";
        case PH_ERR_EMPTY_IMAGE:
            return "no image loaded -- none yet, or the last load failed: load before hashing";
        case PH_ERR_INVALID_ARGUMENT:
            return "an invalid argument: a bug in the calling code";
        default:
            return "unexpected";
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path>...\n", argv[0]);
        return 1;
    }

    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        fprintf(stderr, "ph_create failed\n");
        return 1;
    }
    /* A low limit, so that an ordinary photo shows PH_ERR_IMAGE_TOO_LARGE as well. */
    if (ph_context_set_max_pixels(ctx, 100u * 1000u) != PH_SUCCESS) {
        ph_free(ctx);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        ph_error_t err = ph_load_from_file(ctx, argv[i]);
        uint64_t hash = 0;
        if (err == PH_SUCCESS) {
            err = ph_compute_phash(ctx, &hash);
        }
        printf("%s\n  %s: %s\n", argv[i], ph_get_error_string(err), what_to_do(err));
        /* The last load's own words: the decoder's complaint, the limit it hit. */
        const char *detail = ph_get_last_error_message(ctx);
        if (err != PH_SUCCESS && detail[0] != '\0') {
            printf("  detail: %s\n", detail);
        }
        if (err == PH_SUCCESS) {
            int width = 0, height = 0, channels = 0;
            ph_context_get_dimensions(ctx, &width, &height, &channels);
            printf("  %dx%d, %d channels, pHash %016llx\n", width, height, channels,
                   (unsigned long long)hash);
        }
    }

    /* A hash on a context whose last load failed is refused, not computed from a
     * previous image. */
    if (!ph_is_loaded(ctx)) {
        uint64_t hash = 0;
        ph_error_t err = ph_compute_phash(ctx, &hash);
        printf("hash with no image loaded\n  %s: %s\n", ph_get_error_string(err), what_to_do(err));
    }

    ph_free(ctx);
    return 0;
}
