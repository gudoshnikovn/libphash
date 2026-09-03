#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_bilinear_unit() {
    uint8_t img[9] = {0, 100, 200, 50, 150, 250, 100, 200, 255}; // 3x3 image
    // (0,0)=0, (1,0)=100, (2,0)=200
    // (0,1)=50, (1,1)=150, (2,1)=250
    // (0,2)=100, (1,2)=200, (2,2)=255

    // In a 3x3 image, max valid x/y for bilinear is < 2.0
    ASSERT_FLOAT_EQ(0.0, ph_get_pixel_bilinear(img, 3, 3, 0.0f, 0.0f), 0.1);
    ASSERT_FLOAT_EQ(150.0, ph_get_pixel_bilinear(img, 3, 3, 1.0f, 1.0f), 0.1);

    // Halfway between (0,0) and (1,0) -> 50.0
    ASSERT_FLOAT_EQ(50.0, ph_get_pixel_bilinear(img, 3, 3, 0.5f, 0.0f), 0.1);

    // Center of (0,0),(1,0),(0,1),(1,1) -> (0+100+50+150)/4 = 300/4 = 75.0
    ASSERT_FLOAT_EQ(75.0, ph_get_pixel_bilinear(img, 3, 3, 0.5f, 0.5f), 0.1);

    // Out of bounds
    ASSERT_FLOAT_EQ(-1.0, ph_get_pixel_bilinear(img, 3, 3, -0.1f, 0.0f), 0.1);
    ASSERT_FLOAT_EQ(-1.0, ph_get_pixel_bilinear(img, 3, 3, 2.0f, 0.0f),
                    0.1); // Edge point is OOB for bilinear

    PASS("test_bilinear_unit");
}

void test_projection_variance_unit() {
    uint8_t img[16];
    memset(img, 128, sizeof(img));
    // Uniform image -> variance should be 0
    double var = ph_projection_variance(img, 4, 4, 2.0, 2.0, 2.0, 1.0f, 0.0f, 4);
    ASSERT_FLOAT_EQ(0.0, var, 0.01);

    // Non-uniform image
    memset(img, 0, sizeof(img));
    img[5] = 255; // (1,1)
    img[6] = 255; // (2,1)
    var = ph_projection_variance(img, 4, 4, 2.0, 1.0, 2.0, 1.0f, 0.0f, 4);
    ASSERT(var > 0.0);

    PASS("test_projection_variance_unit");
}

typedef struct {
    uint8_t *px;
    int w, h;
} image_t_rot;

static int radial_of_file(const char *path, ph_digest_t *out) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    int ok = (ph_load_from_file(ctx, path) == PH_SUCCESS) &&
             (ph_compute_radial_hash(ctx, out) == PH_SUCCESS);
    ph_free(ctx);
    return ok;
}

/* The contract of the comparison the algorithm's source specifies. */
void test_radial_similarity_contract() {
    ph_digest_t a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.size = b.size = 8;
    for (int i = 0; i < 8; i++) {
        a.data[i] = (uint8_t)(10 * i);
        b.data[i] = (uint8_t)(10 * i);
    }

    double pcc = -9.0;
    ASSERT_OK(ph_radial_similarity(&a, &b, &pcc));
    ASSERT_FLOAT_EQ(1.0, pcc, 1e-9); /* a digest against itself */

    /* Symmetric, unlike pHash's own ph_crosscorr() -- see the note in common.c. */
    double back = -9.0;
    for (int i = 0; i < 8; i++)
        b.data[i] = (uint8_t)(200 - 7 * i);
    ASSERT_OK(ph_radial_similarity(&a, &b, &pcc));
    ASSERT_OK(ph_radial_similarity(&b, &a, &back));
    ASSERT_FLOAT_EQ(pcc, back, 1e-9);
    ASSERT(pcc >= -1.0 && pcc <= 1.0);

    /* Two flat digests -- what a blank or radially symmetric image produces -- are the
     * same picture as far as this descriptor can tell. A flat one against a varying one
     * is not a match. */
    ph_digest_t flat1, flat2;
    memset(&flat1, 0, sizeof flat1);
    memset(&flat2, 0, sizeof flat2);
    flat1.size = flat2.size = 8;
    ASSERT_OK(ph_radial_similarity(&flat1, &flat2, &pcc));
    ASSERT_FLOAT_EQ(1.0, pcc, 1e-9);
    ASSERT_OK(ph_radial_similarity(&flat1, &a, &pcc));
    ASSERT_FLOAT_EQ(0.0, pcc, 1e-9);

    /* Rejections leave the output untouched. */
    pcc = -9.0;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_radial_similarity(NULL, &a, &pcc));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_radial_similarity(&a, NULL, &pcc));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_radial_similarity(&a, &b, NULL));
    b.size = 7;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_radial_similarity(&a, &b, &pcc));
    b.size = 8;
    a.size = 0;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_radial_similarity(&a, &b, &pcc));
    a.size = PH_DIGEST_MAX_BYTES + 1;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_radial_similarity(&a, &b, &pcc));
    ASSERT_FLOAT_EQ(-9.0, pcc, 1e-9);

    PASS("test_radial_similarity_contract");
}

/* The digest is not a bit vector, so the comparison has to be the source's own.
 * Measured on these fixtures (2.0.0): an identical copy 1.0000, the same photo with its
 * colours changed 0.9980, an unrelated image 0.6908. */
void test_radial_with_real_rotation() {
    ph_digest_t orig, copy, other;

    if (!radial_of_file(TEST_DATA_DIR "/photo.jpeg", &orig) ||
        !radial_of_file(TEST_DATA_DIR "/photo_copy.jpeg", &copy) ||
        !radial_of_file(TEST_DATA_DIR "/photo_complex.png", &other)) {
        fprintf(stderr, "Skip e2e test: Could not find images.\n");
        return;
    }

    ASSERT_INT_EQ(PH_RADIAL_COEFFS, orig.size);

    double same = 0.0, unrelated = 0.0;
    ASSERT_OK(ph_radial_similarity(&orig, &copy, &same));
    ASSERT_OK(ph_radial_similarity(&orig, &other, &unrelated));

    if (same < PH_RADIAL_PCC_THRESHOLD) {
        fprintf(stderr, "FAIL: an identical copy scores only %.4f\n", same);
        exit(1);
    }
    if (unrelated >= PH_RADIAL_PCC_THRESHOLD) {
        fprintf(stderr, "FAIL: an unrelated image scores %.4f, at or above the threshold\n",
                unrelated);
        exit(1);
    }

    printf("test_radial_with_real_rotation: PASSED (copy %.4f, unrelated %.4f)\n", same, unrelated);
}

/* Rotation on real photographs, which is where the algorithm's claim has to hold.
 *
 * The source is credited with robustness to rotation, and this measures what that
 * amounts to. Measured on tests/data/photo.jpeg (2.0.0), against a 0.6924 baseline for an
 * unrelated image: 1 degree 0.9932, 2 degrees 0.9745, 3 degrees 0.9444, 5 degrees 0.8703,
 * 10 degrees 0.6892, 15 degrees 0.4371, 90 degrees 0.2434, 180 degrees 0.9927. On the
 * smoother photo_complex.png the same sweep holds to 10 degrees (0.9385).
 *
 * So: a few degrees of rotation, which is the kind a rescan or a re-encode introduces and
 * the kind the perceptual-hashing literature evaluates, and an exact half turn. Not
 * arbitrary rotation -- see the reasoning in tests/src/test_hash_properties.c. */
static image_t_rot rotate_about_centre(const uint8_t *src, int w, int h, double deg) {
    image_t_rot o;
    o.w = w;
    o.h = h;
    o.px = (uint8_t *)calloc((size_t)w * h * 3, 1);
    ASSERT_PTR_NOT_NULL(o.px);
    double a = deg * M_PI / 180.0, ca = cos(a), sa = sin(a);
    double cx = (w - 1) / 2.0, cy = (h - 1) / 2.0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double dx = x - cx, dy = y - cy;
            double sx = cx + dx * ca + dy * sa;
            double sy = cy - dx * sa + dy * ca;
            if (sx < 0 || sy < 0 || sx >= w - 1 || sy >= h - 1)
                continue;
            int x0 = (int)sx, y0 = (int)sy;
            double fx = sx - x0, fy = sy - y0;
            for (int c = 0; c < 3; c++) {
                double p = src[((size_t)y0 * w + x0) * 3 + c] * (1 - fx) * (1 - fy) +
                           src[((size_t)y0 * w + x0 + 1) * 3 + c] * fx * (1 - fy) +
                           src[((size_t)(y0 + 1) * w + x0) * 3 + c] * (1 - fx) * fy +
                           src[((size_t)(y0 + 1) * w + x0 + 1) * 3 + c] * fx * fy;
                o.px[((size_t)y * w + x) * 3 + c] = (uint8_t)(p + 0.5);
            }
        }
    return o;
}

static void radial_of_pixels(const image_t_rot *im, ph_digest_t *out) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_pixels(ctx, im->px, im->w, im->h, 3, 0));
    ASSERT_OK(ph_compute_radial_hash(ctx, out));
    ph_free(ctx);
}

static double pcc_at(const uint8_t *px, int w, int h, const ph_digest_t *ref, double deg) {
    image_t_rot r = rotate_about_centre(px, w, h, deg);
    ph_digest_t d;
    radial_of_pixels(&r, &d);
    double p = 0.0;
    ASSERT_OK(ph_radial_similarity(ref, &d, &p));
    free(r.px);
    return p;
}

void test_radial_rotation_on_a_photograph() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    if (ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg") != PH_SUCCESS) {
        fprintf(stderr, "Skip e2e test: Could not find images.\n");
        ph_free(ctx);
        return;
    }
    int w = ctx->image.width, h = ctx->image.height;
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 3);
    ASSERT_PTR_NOT_NULL(px);
    memcpy(px, ctx->image.raw_rgb, (size_t)w * h * 3);
    ph_free(ctx);

    /* The reference goes through the same resampler at zero degrees, so what follows
     * measures the rotation rather than the interpolator. */
    image_t_rot ref_img = rotate_about_centre(px, w, h, 0.0);
    ph_digest_t ref;
    radial_of_pixels(&ref_img, &ref);
    free(ref_img.px);

    ph_digest_t other;
    int have_other = radial_of_file(TEST_DATA_DIR "/photo_complex.png", &other);
    double unrelated = 0.0;
    if (have_other)
        ASSERT_OK(ph_radial_similarity(&ref, &other, &unrelated));

    double p1 = pcc_at(px, w, h, &ref, 1.0);
    double p2 = pcc_at(px, w, h, &ref, 2.0);
    double p3 = pcc_at(px, w, h, &ref, 3.0);
    double p15 = pcc_at(px, w, h, &ref, 15.0);
    double p90 = pcc_at(px, w, h, &ref, 90.0);
    double p180 = pcc_at(px, w, h, &ref, 180.0);
    free(px);

    printf("  radial vs rotation: 1deg %.4f  2deg %.4f  3deg %.4f  15deg %.4f  90deg %.4f  "
           "180deg %.4f  (unrelated %.4f)\n",
           p1, p2, p3, p15, p90, p180, unrelated);

    /* Small rotations match, and so does an exact half turn. */
    if (p1 < PH_RADIAL_PCC_THRESHOLD || p2 < PH_RADIAL_PCC_THRESHOLD ||
        p3 < PH_RADIAL_PCC_THRESHOLD || p180 < PH_RADIAL_PCC_THRESHOLD) {
        fprintf(stderr,
                "FAIL: a small rotation or a half turn no longer matches (%.4f / %.4f / %.4f / "
                "%.4f against a threshold of %.2f)\n",
                p1, p2, p3, p180, PH_RADIAL_PCC_THRESHOLD);
        exit(1);
    }
    /* Large ones do not, and that boundary is pinned: it is a property of the transform,
     * not something a different comparison could fix. */
    if (p15 >= PH_RADIAL_PCC_THRESHOLD || p90 >= PH_RADIAL_PCC_THRESHOLD) {
        fprintf(stderr,
                "FAIL: 15 or 90 degrees now scores %.4f / %.4f -- if the representation was "
                "changed to survive a shift, this test should assert invariance instead\n",
                p15, p90);
        exit(1);
    }

    PASS("test_radial_rotation_on_a_photograph");
}

int main() {
    test_bilinear_unit();
    test_projection_variance_unit();
    test_radial_similarity_contract();
    test_radial_with_real_rotation();
    test_radial_rotation_on_a_photograph();
    return 0;
}
