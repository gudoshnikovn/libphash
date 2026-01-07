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
