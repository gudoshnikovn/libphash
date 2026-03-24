#include "libphash.h"
#include "test_macros.h"
#include "../src/internal.h"
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
    ASSERT_FLOAT_EQ(0.0, ph_get_pixel_bilinear(img, 3, 3, 0.0, 0.0), 0.1);
    ASSERT_FLOAT_EQ(150.0, ph_get_pixel_bilinear(img, 3, 3, 1.0, 1.0), 0.1);

    // Halfway between (0,0) and (1,0) -> 50.0
    ASSERT_FLOAT_EQ(50.0, ph_get_pixel_bilinear(img, 3, 3, 0.5, 0.0), 0.1);

    // Center of (0,0),(1,0),(0,1),(1,1) -> (0+100+50+150)/4 = 300/4 = 75.0
    ASSERT_FLOAT_EQ(75.0, ph_get_pixel_bilinear(img, 3, 3, 0.5, 0.5), 0.1);

    // Out of bounds
    ASSERT_FLOAT_EQ(-1.0, ph_get_pixel_bilinear(img, 3, 3, -0.1, 0.0), 0.1);
    ASSERT_FLOAT_EQ(-1.0, ph_get_pixel_bilinear(img, 3, 3, 2.0, 0.0), 0.1); // Edge point is OOB for bilinear

    PASS("test_bilinear_unit");
}

void test_projection_variance_unit() {
    uint8_t img[16];
    memset(img, 128, sizeof(img));
    // Uniform image -> variance should be 0
    double var = ph_projection_variance(img, 4, 4, 2.0, 2.0, 2.0, 1.0, 0.0, 4);
    ASSERT_FLOAT_EQ(0.0, var, 0.01);

    // Non-uniform image
    memset(img, 0, sizeof(img));
    img[5] = 255; // (1,1)
    img[6] = 255; // (2,1)
    var = ph_projection_variance(img, 4, 4, 2.0, 1.0, 2.0, 1.0, 0.0, 4);
    ASSERT(var > 0.0);

    PASS("test_projection_variance_unit");
}

double calculate_rotated_l2(const ph_digest_t *a, const ph_digest_t *b) {
    if (a->size != b->size)
        return -1.0;

    int n = a->size;
    double min_l2 = DBL_MAX;

    for (int shift = 0; shift < n; shift++) {
        double current_sum_sq = 0;
        for (int i = 0; i < n; i++) {
            int b_idx = (i + shift) % n;
            double diff = (double)a->data[i] - (double)b->data[b_idx];
            current_sum_sq += diff * diff;
        }
        double current_l2 = sqrt(current_sum_sq);
        if (current_l2 < min_l2)
            min_l2 = current_l2;
    }
    return min_l2;
}

void test_radial_with_real_rotation() {
    ph_context_t *ctx_orig = NULL;
    ph_context_t *ctx_rot = NULL;
    ph_digest_t dig_orig;
    ph_digest_t dig_rot;

    ASSERT_OK(ph_create(&ctx_orig));
    ASSERT_OK(ph_create(&ctx_rot));

    ph_error_t err1 = ph_load_from_file(ctx_orig, "tests/photo.jpeg");
    ph_error_t err2 = ph_load_from_file(ctx_rot, "tests/photo_rotated_90.jpeg");

    if (err1 != PH_SUCCESS || err2 != PH_SUCCESS) {
        fprintf(stderr, "Skip e2e test: Could not find images.\n");
        goto cleanup;
    }

    ASSERT_OK(ph_compute_radial_hash(ctx_orig, &dig_orig));
    ASSERT_OK(ph_compute_radial_hash(ctx_rot, &dig_rot));

    double rotated_dist = calculate_rotated_l2(&dig_orig, &dig_rot);

    if (rotated_dist > 40.0) {
        fprintf(stderr, "FAIL: Radial hash distance too high: %.2f\n", rotated_dist);
        exit(1);
    }

    PASS("test_radial_with_real_rotation");

cleanup:
    ph_free(ctx_orig);
    ph_free(ctx_rot);
}

int main() {
    test_bilinear_unit();
    test_projection_variance_unit();
    test_radial_with_real_rotation();
    return 0;
}
