#include "../src/internal.h"
#include "test_macros.h"
#include <stdint.h>
#include <string.h>

/* Test grayscale conversion math */
void test_grayscale_conversion() {
  // RGB: 255, 0, 0 (Red) -> Luminance ~76
  // RGB: 0, 255, 0 (Green) -> Luminance ~149
  uint8_t rgb[] = {255, 0, 0, 0, 255, 0};
  uint8_t gray[2];

  ph_to_grayscale(rgb, 2, 1, 3, gray);

  if (gray[0] == 0 || gray[1] == 0) {
    fprintf(stderr, "Grayscale conversion produced black pixels\n");
    exit(1);
  }
  printf("test_grayscale_conversion: PASSED\n");
}

/* Test resizing logic (Downsampling) */
void test_image_resizing() {
  uint8_t src[16] = {255, 255, 0,   0,   255, 255, 0,   0,
                     0,   0,   255, 255, 0,   0,   255, 255};
  uint8_t dst[4]; // Resize 4x4 to 2x2

  ph_resize_grayscale(src, 4, 4, dst, 2, 2);

  // Top-left quadrant of src is all 255, so dst[0] should be 255
  ASSERT_INT_EQ(255, dst[0]);
  // Top-right quadrant is 0
  ASSERT_INT_EQ(0, dst[1]);

  printf("test_image_resizing: PASSED\n");
}

/* Test Hamming Distance for Digests (Large Hashes) */
void test_digest_hamming() {
  ph_digest_t *d1, *d2;
  ph_digest_create(256, &d1);
  ph_digest_create(256, &d2);

  memset(d1->data, 0, 32);
  memset(d2->data, 0, 32);

  d1->data[0] = 0x01; // 1 bit set
  d2->data[0] = 0x03; // 2 bits set (1 overlapping)

  // Distance between 00000001 and 00000011 is 1 bit
  ASSERT_INT_EQ(1, ph_hamming_distance_digest(d1, d2));

  ph_digest_free(d1);
  ph_digest_free(d2);
  printf("test_digest_hamming: PASSED\n");
}

int main() {
  test_grayscale_conversion();
  test_image_resizing();
  test_digest_hamming();
  return 0;
}
