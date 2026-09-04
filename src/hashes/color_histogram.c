/* ColorHash -- colour histogram with histogram intersection.
 *
 * After M. Swain and D. Ballard, "Color Indexing", International Journal of Computer
 * Vision 7(1):11-32, 1991: quantise the colour space, count pixels per bin, and compare
 * two histograms by their intersection, sum(min(a_i, b_i)) over the normalised bins.
 *
 * IMPLEMENTED FROM SECONDARY DESCRIPTIONS. The paper is paywalled and could not be
 * obtained -- IJCV is closed, OpenAlex reports oa_status closed and no repository holds
 * the full text, and Swain's Rochester technical report (TR 360, 1990) is not freely
 * available either. The intersection formula is confirmed by several independent
 * restatements, but by this project's own rule that leaves the basis at rank 4, and no
 * claim of conformance is made. What can be said honestly: this is a colour histogram
 * with histogram intersection, after Swain & Ballard (1991).
 *
 * Since the paper cannot supply the quantisation, it was chosen by measurement and the
 * measurement is the justification -- the same footing wHash is on. The axes are the
 * opponent ones Swain and Ballard use, which secondary descriptions do agree on:
 *
 *     rg = R - G          red against green
 *     by = 2B - R - G     blue against yellow
 *     wb = R + G + B      light against dark
 *
 * at 6 x 6 x 3 = 108 bins. On the property corpus that separates at 3.95 against 1.89 for
 * the ImageHash port it replaces, and the number holds on a second corpus at a different
 * resolution (3.87). Fifteen other quantisations were measured -- RGB cubes from 3x3x3 to
 * 5x5x5, HSV at 8x4x4 and 12x3x3, and the opponent axes at nine resolutions; the full
 * table is in docs/algorithm-provenance.md.
 *
 * Two of those scored higher and were rejected on evidence the corpus cannot show. Both
 * 6x6x1 (4.28) and 9x9x1 (3.94) drop the intensity axis, which makes them invariant to
 * exposure -- and makes a black image and a white image produce the same hash, along with
 * every other pair of flat greys. A corpus of colourful pictures never notices. The
 * flat-colour check in tests/src/test_color_hash.c is there so that no future tuning
 * repeats that trade silently.
 *
 * What 6x6x3 still cannot separate, measured by the same check: flat colours whose chroma
 * matches and whose total intensity falls in the same third -- black against dark grey,
 * light grey against white. Three intensity bins is what fits beside 6x6 chroma inside
 * PH_DIGEST_MAX_BYTES, and chroma resolution is worth more here than intensity resolution
 * (5x5x5 has no such collisions and separates at 2.77).
 *
 * Before 2.0.0 this was a port of ImageHash's `colorhash`, for which ImageHash cites
 * nothing at all: 14 fractions of PIL's HSV categories quantised to 3 bits each, 42 bits
 * inside a uint64_t, with thresholds of 32, 85 and 170 that appear in no source.
 */
#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int ph_color_histogram_bin(int r, int g, int b) {
    /* Opponent axes. The ranges are exact: rg in [-255, 255], by in [-510, 510] and
     * wb in [0, 765], so each axis is mapped from its own full span. */
    int rg = r - g;
    int by = 2 * b - r - g;
    int wb = r + g + b;

    int a = (rg + 255) * PH_COLOR_BINS_RG / 511;
    if (a >= PH_COLOR_BINS_RG)
        a = PH_COLOR_BINS_RG - 1;
    int c = (by + 510) * PH_COLOR_BINS_BY / 1021;
    if (c >= PH_COLOR_BINS_BY)
        c = PH_COLOR_BINS_BY - 1;
    int w = wb * PH_COLOR_BINS_WB / 766;
    if (w >= PH_COLOR_BINS_WB)
        w = PH_COLOR_BINS_WB - 1;

    return (a * PH_COLOR_BINS_BY + c) * PH_COLOR_BINS_WB + w;
}

PH_API ph_error_t ph_compute_color_hash(ph_context_t *ctx, ph_digest_t *out_digest) {
    if (!ctx || !ctx->image.is_loaded || !out_digest)
        return PH_ERR_INVALID_ARGUMENT;

    if (ctx->image.width <= 0 || ctx->image.height <= 0 || !ctx->image.raw_rgb)
        return PH_ERR_EMPTY_IMAGE;

    /* R08/M13: a grayscale image carries no colour to bin. Replicating the single channel
     * into r/g/b would put every pixel on the grey axis and still report PH_SUCCESS, so
     * refuse instead of returning a hash that means nothing. */
    if (ctx->image.channels < 3)
        return PH_ERR_REQUIRES_COLOR;

    memset(out_digest, 0, sizeof(ph_digest_t));
    out_digest->size = (uint8_t)PH_COLOR_BINS;
    out_digest->kind = (uint8_t)PH_DIGEST_KIND_HISTOGRAM;

    /* size_t and uint64_t throughout: width * height overflows int above ~46340x46340,
     * and so does a per-bin counter on an image that large (R03/H6). */
    size_t total_pixels = (size_t)ctx->image.width * (size_t)ctx->image.height;
    size_t channels = (size_t)ctx->image.channels;
    const uint8_t *src = ctx->image.raw_rgb;

    uint64_t counts[PH_COLOR_BINS] = {0};
    for (size_t i = 0; i < total_pixels; i++) {
        counts[ph_color_histogram_bin(src[i * channels], src[i * channels + 1],
                                      src[i * channels + 2])]++;
    }

    /* Scaled by the largest bin rather than by the pixel count. With 108 bins the average
     * bin holds under 1% of the image, which as a fraction of the total would quantise to
     * two or three of the 255 levels and throw away most of the shape; against the
     * maximum the whole byte range is used. The comparison renormalises each digest by its
     * own sum, so nothing downstream depends on which scale was chosen here. */
    uint64_t max_count = 0;
    for (int i = 0; i < PH_COLOR_BINS; i++)
        if (counts[i] > max_count)
            max_count = counts[i];

    if (max_count == 0)
        return PH_SUCCESS; /* no pixels; an all-zero histogram is the honest answer */

    for (int i = 0; i < PH_COLOR_BINS; i++) {
        /* +max_count/2 rounds to nearest without leaving integer arithmetic. */
        out_digest->data[i] = (uint8_t)((counts[i] * 255 + max_count / 2) / max_count);
    }

    return PH_SUCCESS;
}

PH_API ph_error_t ph_histogram_intersection(const ph_digest_t *a, const ph_digest_t *b,
                                            double *out_similarity) {
    if (!out_similarity || !ph_digests_comparable_as(a, b, PH_DIGEST_KIND_HISTOGRAM))
        return PH_ERR_INVALID_ARGUMENT;

    /* Swain and Ballard normalise by the reference histogram, which makes the score
     * asymmetric -- H(t,r) and H(r,t) differ whenever the two hold different pixel counts.
     * Each side is normalised by its own sum here instead, which is the same quantity
     * whenever the two images have the same number of pixels and is symmetric when they
     * do not. A comparison that depends on the order of its arguments is a defect. */
    double sum_a = 0.0, sum_b = 0.0;
    for (int i = 0; i < a->size; i++) {
        sum_a += (double)a->data[i];
        sum_b += (double)b->data[i];
    }
    if (sum_a <= 0.0 || sum_b <= 0.0) {
        /* An empty histogram intersects nothing -- unless the other is empty too, in
         * which case the two images are equally devoid of pixels. */
        *out_similarity = (sum_a <= 0.0 && sum_b <= 0.0) ? 1.0 : 0.0;
        return PH_SUCCESS;
    }

    double intersection = 0.0;
    for (int i = 0; i < a->size; i++) {
        double pa = (double)a->data[i] / sum_a;
        double pb = (double)b->data[i] / sum_b;
        intersection += pa < pb ? pa : pb;
    }

    /* Both sides sum to one, so the intersection is in [0, 1] up to rounding. */
    if (intersection > 1.0)
        intersection = 1.0;
    if (intersection < 0.0)
        intersection = 0.0;
    *out_similarity = intersection;
    return PH_SUCCESS;
}
