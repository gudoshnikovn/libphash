#include "libphash.h"
#include "test_macros.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief FFI/Logic Utility:
 * When an image is rotated, the radial variance array shifts cyclically.
 * Since we scan 0..180 degrees, a 90 degree rotation is a shift of N/2.
 */
double calculate_rotated_l2(const ph_digest_t *a, const ph_digest_t *b) {
  if (a->bits != b->bits) {
    return -1.0;
  }

  int n = (int)(a->bits / 8); /* 40 bytes */
  double min_l2 = DBL_MAX;

  /* Try all possible cyclic shifts */
  for (int shift = 0; shift < n; shift++) {
    double current_sum_sq = 0;
    for (int i = 0; i < n; i++) {
      int b_idx = (i + shift) % n;
      double diff = (double)a->data[i] - (double)b->data[b_idx];
      current_sum_sq += diff * diff;
    }

    double current_l2 = sqrt(current_sum_sq);
    if (current_l2 < min_l2) {
      min_l2 = current_l2;
    }
  }

  return min_l2;
}

void test_radial_with_real_rotation() {
  ph_context_t *ctx_orig = NULL;
  ph_context_t *ctx_rot = NULL;
  ph_digest_t *dig_orig = NULL;
  ph_digest_t *dig_rot = NULL;

  /* 1. Initialization */
  ASSERT_OK(ph_create(&ctx_orig));
  ASSERT_OK(ph_create(&ctx_rot));
  ASSERT_OK(ph_digest_create(320, &dig_orig));
  ASSERT_OK(ph_digest_create(320, &dig_rot));

  /* 2. Load Image Files */
  ph_error_t err1 = ph_load_from_file(ctx_orig, "tests/photo.jpeg");
  ph_error_t err2 = ph_load_from_file(ctx_rot, "tests/photo_rotated_90.jpeg");

  if (err1 != PH_SUCCESS || err2 != PH_SUCCESS) {
    fprintf(stderr, "Skip test: Could not find images.\n");
    goto cleanup;
  }

  /* 3. Compute Radial Variance Hash */
  ASSERT_OK(ph_compute_radial_hash(ctx_orig, dig_orig));
  ASSERT_OK(ph_compute_radial_hash(ctx_rot, dig_rot));

  /* 4. Comparison */
  double direct_dist = ph_l2_distance(dig_orig, dig_rot);
  double rotated_dist = calculate_rotated_l2(dig_orig, dig_rot);

  printf("[Radial] Direct L2 Distance: %.2f\n", direct_dist);
  printf("[Radial] Min L2 Distance (Rotation Corrected): %.2f\n", rotated_dist);

  /*
   * With Gaussian Blur and Gamma, the hash is more stable.
   * A match should be very close to 0 (typically < 10.0).
   */
  if (rotated_dist > 25.0) {
    fprintf(stderr,
            "FAIL: Radial hash distance too high for rotated image: %.2f\n",
            rotated_dist);
    exit(1);
  }

  if (rotated_dist >= direct_dist && direct_dist > 1.0) {
    fprintf(stderr, "FAIL: Rotation search did not improve distance\n");
    exit(1);
  }

  printf("test_radial_with_real_rotation: PASSED\n");

cleanup:
  ph_digest_free(dig_orig);
  ph_digest_free(dig_rot);
  ph_free(ctx_orig);
  ph_free(ctx_rot);
}

int main() {
  test_radial_with_real_rotation();
  return 0;
}
