/* site_stages: comparing images, the measurement behind every robustness and
 * separability chart, and the synthetic corpus they are measured over. */
#include "stages.h"
#include "synthetic_corpus.h"

#include <stdlib.h>

/* --8<-- [start:compare] */
/* One algorithm's comparison of two digests by its own metric; 0 when it does not apply. */
static int compare(const ph_digest_t *a, const ph_digest_t *b, double *out) {
    switch (a->kind) {
        case PH_DIGEST_KIND_BITS:
            *out = ph_similarity_digest(a, b);
            return *out >= 0.0;
        case PH_DIGEST_KIND_COEFFICIENTS:
            return ph_radial_similarity(a, b, out) == PH_SUCCESS;
        case PH_DIGEST_KIND_HISTOGRAM:
            return ph_histogram_intersection(a, b, out) == PH_SUCCESS;
        case PH_DIGEST_KIND_VECTOR16:
            *out = ph_l2_distance(a, b);
            return *out >= 0.0;
        default:
            return 0;
    }
}

/* --8<-- [end:compare] */

/* Every algorithm's comparison of two images' digests, under its name, null where an
 * image has no digest of that kind or the comparison does not apply. */
static void compare_all(json_t *j, const ph_digest_t *a, const int *have_a, const ph_digest_t *b,
                        const int *have_b) {
    for (int k = 0; k < PH_ALGORITHM_COUNT; k++) {
        const char *name = ph_algorithm_name((ph_algorithm_t)k);
        double value = 0.0;
        if (have_a[k] && have_b[k] && compare(&a[k], &b[k], &value)) {
            json_double(j, name, value);
        } else {
            json_null(j, name);
        }
    }
}

/* Every algorithm's digest of a loaded image; have[k] says whether algorithm k gave one. */
static void digest_all(ph_context_t *ctx, ph_digest_t d[PH_ALGORITHM_COUNT],
                       int have[PH_ALGORITHM_COUNT]) {
    for (int k = 0; k < PH_ALGORITHM_COUNT; k++) {
        have[k] = ph_compute_digest(ctx, (ph_algorithm_t)k, &d[k]) == PH_SUCCESS;
    }
}

/* --8<-- [start:measure] */
/* site_stages measure <reference> <variant>...: every algorithm's digest of the reference,
 * then one JSON line per variant with its comparison to the reference, per algorithm (the
 * similarity in [0, 1] for a bit hash, the peak correlation for Radial, the histogram
 * intersection for ColorHash, the L2 distance for ColorMoments). A `--load=<settings>`
 * between the images loads the ones after it so (take_load_settings()). */
int mode_measure(int argc, char **argv) {
    ph_digest_t ref[PH_ALGORITHM_COUNT], d[PH_ALGORITHM_COUNT];
    int have_ref[PH_ALGORITHM_COUNT], have[PH_ALGORITHM_COUNT];
    int status = 0, images = 0;
    for (int v = 0; v < argc && !status; v++) {
        if (take_load_settings(argv[v], &status)) {
            continue;
        }
        ph_context_t *ctx = NULL;
        if (load_image(&ctx, argv[v])) {
            return 1;
        }
        digest_all(ctx, images ? d : ref, images ? have : have_ref);
        ph_free(ctx);
        if (images++ == 0) {
            continue;
        }
        json_t j = json_begin(stdout);
        json_string(&j, "file", argv[v]);
        compare_all(&j, ref, have_ref, d, have);
        json_end(&j);
    }
    return status;
}

/* --8<-- [end:measure] */

/* --8<-- [start:pairs] */
/* site_stages pairs <image>...: every image's digests, then one JSON line per pair of
 * distinct images (i < j, in the order given, as "a" and "b") with their comparison,
 * per algorithm. A `--load=<settings>` before the images loads them so. */
int mode_pairs(int argc, char **argv) {
    ph_digest_t(*dig)[PH_ALGORITHM_COUNT] = calloc((size_t)argc, sizeof(*dig));
    int (*have)[PH_ALGORITHM_COUNT] = calloc((size_t)argc, sizeof(*have));
    int bad = (!dig || !have) && fail("out of memory", NULL);
    int n = 0;
    for (int i = 0; i < argc && !bad; i++) {
        if (take_load_settings(argv[i], &bad)) {
            continue;
        }
        ph_context_t *ctx = NULL;
        bad = load_image(&ctx, argv[i]);
        if (!bad) {
            digest_all(ctx, dig[n], have[n]);
            n++;
            ph_free(ctx);
        }
    }
    for (int i = 0; i < n && !bad; i++) {
        for (int k = i + 1; k < n; k++) {
            json_t j = json_begin(stdout);
            json_int(&j, "a", i);
            json_int(&j, "b", k);
            compare_all(&j, dig[i], have[i], dig[k], have[k]);
            json_end(&j);
        }
    }
    free(dig);
    free(have);
    return bad;
}

/* --8<-- [end:pairs] */

/* site_stages corpus <outdir>
 *     Writes the synthetic corpus of the tests (tests/src/synthetic_corpus.h) as
 *     00.ppm ... 23.ppm, so the site measures the very images the tests do. */
int mode_corpus(int argc, char **argv) {
    (void)argc;
    int status = 0;
    for (int i = 0; i < NUM_BASE; i++) {
        char name[16];
        snprintf(name, sizeof(name), "%02d.ppm", i);
        image_t im = make_base(i);
        status |= write_pnm(argv[0], name, im.px, im.w, im.h, 3);
        image_free(&im);
    }
    return status;
}
