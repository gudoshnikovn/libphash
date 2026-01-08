#include "libphash.h"
#include "test_macros.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

    ph_context_set_gamma(ctx_rot, 2.2f);

    ph_error_t err1 = ph_load_from_file(ctx_orig, "tests/photo.jpeg");
    ph_error_t err2 = ph_load_from_file(ctx_rot, "tests/photo_rotated_90.jpeg");

    if (err1 != PH_SUCCESS || err2 != PH_SUCCESS) {
        fprintf(stderr, "Skip test: Could not find images.\n");
        goto cleanup;
    }

    ASSERT_OK(ph_compute_radial_hash(ctx_orig, &dig_orig));
    ASSERT_OK(ph_compute_radial_hash(ctx_rot, &dig_rot));

    double direct_dist = ph_l2_distance(&dig_orig, &dig_rot);
    double rotated_dist = calculate_rotated_l2(&dig_orig, &dig_rot);

    printf("[Radial] Direct L2 Distance: %.2f\n", direct_dist);
    printf("[Radial] Min L2 Distance (Rotation Corrected): %.2f\n", rotated_dist);

    if (rotated_dist > 25.0) {
        fprintf(stderr, "FAIL: Radial hash distance too high: %.2f\n", rotated_dist);
        exit(1);
    }

    printf("test_radial_with_real_rotation: PASSED\n");

cleanup:
    ph_free(ctx_orig);
    ph_free(ctx_rot);
}

int main() {
    test_radial_with_real_rotation();
    return 0;
}
