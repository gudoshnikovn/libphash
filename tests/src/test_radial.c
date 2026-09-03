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

/* On real photographs, with the comparison the source specifies.
 *
 * The one thing this test does not assert is rotation invariance, because the algorithm
 * does not deliver it: the invariance lives in the vector of per-angle variances, and the
 * DCT that turns that vector into the hash does not survive a cyclic shift. The
 * measurement and the reasoning are in tests/src/test_hash_properties.c and in
 * docs/algorithm-provenance.md section 7. Measured here (2.0.0): an identical copy 1.0000,
 * the same photo with its colours changed 0.9980, an unrelated image 0.6908, and the same
 * photo rotated by 90 degrees 0.2468 -- worse than the unrelated one. */
void test_radial_with_real_rotation() {
    ph_digest_t orig, rot, copy, other;

    if (!radial_of_file(TEST_DATA_DIR "/photo.jpeg", &orig) ||
        !radial_of_file(TEST_DATA_DIR "/photo_rotated_90.jpeg", &rot) ||
        !radial_of_file(TEST_DATA_DIR "/photo_copy.jpeg", &copy) ||
        !radial_of_file(TEST_DATA_DIR "/photo_complex.png", &other)) {
        fprintf(stderr, "Skip e2e test: Could not find images.\n");
        return;
    }

    ASSERT_INT_EQ(PH_RADIAL_COEFFS, orig.size);

    double same = 0.0, unrelated = 0.0, rotated = 0.0;
    ASSERT_OK(ph_radial_similarity(&orig, &copy, &same));
    ASSERT_OK(ph_radial_similarity(&orig, &other, &unrelated));
    ASSERT_OK(ph_radial_similarity(&orig, &rot, &rotated));

    if (same < PH_RADIAL_PCC_THRESHOLD) {
        fprintf(stderr, "FAIL: an identical copy scores only %.4f\n", same);
        exit(1);
    }
    if (unrelated >= PH_RADIAL_PCC_THRESHOLD) {
        fprintf(stderr, "FAIL: an unrelated image scores %.4f, at or above the threshold\n",
                unrelated);
        exit(1);
    }
    if (rotated >= PH_RADIAL_PCC_THRESHOLD) {
        fprintf(stderr,
                "FAIL: a 90-degree rotation now scores %.4f -- if the representation was "
                "changed to survive a shift, this test should assert invariance instead\n",
                rotated);
        exit(1);
    }

    printf("test_radial_with_real_rotation: PASSED (copy %.4f, unrelated %.4f, rotated %.4f -- "
           "rotation divergence pinned)\n",
           same, unrelated, rotated);
}

int main() {
    test_bilinear_unit();
    test_projection_variance_unit();
    test_radial_similarity_contract();
    test_radial_with_real_rotation();
    return 0;
}
