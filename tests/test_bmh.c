#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>

void test_bmh_logic() {
  ph_context_t *ctx1 = NULL;
  ph_context_t *ctx2 = NULL;
  ph_digest_t *digest1 = NULL;
  ph_digest_t *digest2 = NULL;

  ASSERT_OK(ph_create(&ctx1));
  ASSERT_OK(ph_create(&ctx2));

  /* Create 256-bit digests for BMH (Block Mean Hash) */
  ASSERT_OK(ph_digest_create(256, &digest1));
  ASSERT_OK(ph_digest_create(256, &digest2));

  ASSERT_OK(ph_load_from_file(ctx1, "tests/photo.jpeg"));
  ASSERT_OK(ph_load_from_file(ctx2, "tests/photo_copy.jpeg"));

  ASSERT_OK(ph_compute_bmh(ctx1, digest1));
  ASSERT_OK(ph_compute_bmh(ctx2, digest2));

  int dist = ph_hamming_distance_digest(digest1, digest2);
  printf("[BMH] Distance: %d bits\n", dist);

  /* For identical/copied images, distance should be very low.
     Threshold for 256-bit is typically higher than for 64-bit. */
  if (dist > 20) {
    fprintf(stderr, "BMH distance too high: %d\n", dist);
    exit(1);
  }

  ph_digest_free(digest1);
  ph_digest_free(digest2);
  ph_free(ctx1);
  ph_free(ctx2);

  printf("test_bmh_logic: PASSED\n");
}

int main() {
  test_bmh_logic();
  return 0;
}
