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

/* Rotation, on a real photograph and its exact 90-degree rotation.
 *
 * A rotation cyclically shifts the radial variance vector, and the source undoes that
 * shift by comparing with the peak of the cross-correlation. libphash does not implement
 * that comparison yet (docs/algorithm-provenance.md, defect 2), so this test does not
 * claim invariance. What it does claim is the part the DCT delivers on its own: a
 * 90-degree rotation is a shift by exactly half of the 180-element vector, and half the
 * DCT coefficients survive such a shift up to sign, so the rotated image lands much
 * closer than an unrelated one -- which is the property that makes the digest usable at
 * all before the cross-correlation lands.
 *
 * Measured on these fixtures (2.0.0): photo vs its 90-degree rotation 95.6, photo vs an
 * unrelated image 332.7, a ratio of 0.29. Before the DCT was applied the same pair gave
 * 255.9 against 675.3, a ratio of 0.38. The bounds below sit well outside both numbers.
 */
static void radial_of_file(const char *path, ph_digest_t *out, int *ok) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    *ok = (ph_load_from_file(ctx, path) == PH_SUCCESS) &&
          (ph_compute_radial_hash(ctx, out) == PH_SUCCESS);
    ph_free(ctx);
}

void test_radial_with_real_rotation() {
    ph_digest_t dig_orig, dig_rot, dig_other;
    int ok1 = 0, ok2 = 0, ok3 = 0;

    radial_of_file(TEST_DATA_DIR "/photo.jpeg", &dig_orig, &ok1);
    radial_of_file(TEST_DATA_DIR "/photo_rotated_90.jpeg", &dig_rot, &ok2);
    radial_of_file(TEST_DATA_DIR "/photo_complex.png", &dig_other, &ok3);

    if (!ok1 || !ok2 || !ok3) {
        fprintf(stderr, "Skip e2e test: Could not find images.\n");
        return;
    }

    ASSERT_INT_EQ(PH_RADIAL_COEFFS, dig_orig.size);

    double rotated = ph_l2_distance(&dig_orig, &dig_rot);
    double unrelated = ph_l2_distance(&dig_orig, &dig_other);

    if (rotated > 150.0 || rotated >= unrelated / 2.0) {
        fprintf(stderr,
                "FAIL: rotation is not absorbed: 90-degree rotation is %.2f away, an "
                "unrelated image only %.2f\n",
                rotated, unrelated);
        exit(1);
    }

    printf("test_radial_with_real_rotation: PASSED (rotated %.1f, unrelated %.1f)\n", rotated,
           unrelated);
}

int main() {
    test_bilinear_unit();
    test_projection_variance_unit();
    test_radial_with_real_rotation();
    return 0;
}
