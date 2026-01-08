#include "../internal.h"
#include <math.h>
#include <stdint.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#elif defined(__SSE4_2__)
#include <nmmintrin.h>
#endif

PH_API const char *ph_version(void) { return "1.2.0"; }

PH_API int ph_hamming_distance(uint64_t hash1, uint64_t hash2) {
  uint64_t x = hash1 ^ hash2;
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(x);
#elif defined(_MSC_VER)
  return (int)__popcnt64(x);
#else
  int count = 0;
  while (x) {
    x &= (x - 1);
    count++;
  }
  return count;
#endif
}

PH_API int ph_hamming_distance_digest(const ph_digest_t *a,
                                      const ph_digest_t *b) {
  if (!a || !b || a->bits != b->bits)
    return -1;
  size_t len = (a->bits + 7) / 8;
  int total = 0;
  size_t i = 0;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  uint16x8_t v_sum = vdupq_n_u16(0);

  for (; i + 16 <= len; i += 16) {
    uint8x16_t va = vld1q_u8(&a->data[i]);
    uint8x16_t vb = vld1q_u8(&b->data[i]);
    uint8x16_t vxor = veorq_u8(va, vb);
    uint8x16_t vcnt = vcntq_u8(vxor);
    // Accumulate 8-bit counts into 16-bit lanes to prevent overflow
    v_sum = vpadalq_u8(v_sum, vcnt);
  }
  // vaddlvq_u16 sums all lanes in the vector into a single 64-bit value
  total = (int)vaddlvq_u16(v_sum);
#endif

  for (; i < len; i++) {
    uint8_t x = a->data[i] ^ b->data[i];
#ifdef __GNUC__
    total += __builtin_popcount(x);
#else
    while (x) {
      x &= (x - 1);
      total++;
    }
#endif
  }
  return total;
}
PH_API double ph_l2_distance(const ph_digest_t *a, const ph_digest_t *b) {
  if (!a || !b || a->bits != b->bits)
    return -1.0;

  double sum = 0;
  size_t byte_count = (a->bits + 7) / 8;
  for (size_t i = 0; i < byte_count; i++) {
    double diff = (double)a->data[i] - (double)b->data[i];
    sum += diff * diff;
  }
  return sqrt(sum);
}
