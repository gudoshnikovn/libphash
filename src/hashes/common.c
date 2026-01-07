#include "../internal.h"
#include <stdint.h>

PH_API int ph_hamming_distance(uint64_t hash1, uint64_t hash2) {
  uint64_t x = hash1 ^ hash2;
#if defined(_MSC_VER)
  return (int)__popcnt64(x);
#elif defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(x);
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
  if (!a || !b || a->bits != b->bits) {
    return -1;
  }

  int total_distance = 0;
  size_t byte_count = (a->bits + 7) / 8;

  for (size_t i = 0; i < byte_count; i++) {
    uint8_t x = a->data[i] ^ b->data[i];

    /* Use CPU-optimized population count where available */
#if defined(_MSC_VER)
    total_distance += (int)__popcnt16((uint16_t)x);
#elif defined(__GNUC__) || defined(__clang__)
    total_distance += __builtin_popcount(x);
#else
    /* Fallback: Kernighan's bit counting algorithm */
    while (x) {
      x &= (x - 1);
      total_distance++;
    }
#endif
  }

  return total_distance;
}
