/* Code more than one algorithm in src/hashes/ calls: the median threshold that pHash
 * applies to its DCT block and wHash to its LL band. */

#include "hashes/hashes.h"
#include <stdint.h>

uint64_t ph_median_bitpack(const float *values, int n) {
    return ph_median_bitpack_from(values, n, 0);
}

uint64_t ph_median_bitpack_from(const float *values, int n, int median_from) {
    if (n <= 0 || n > 64 || median_from < 0 || median_from >= n)
        return 0;

    /* Every value gets a bit; only values[median_from..n-1] get a say in the median. */
    int m = n - median_from;
    float sorted[64];
    for (int i = 0; i < m; i++) {
        sorted[i] = values[median_from + i];
    }

    // Sort to find median (insertion sort)
    for (int i = 1; i < m; i++) {
        float key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    float median;
    if (m % 2 == 0) {
        median = (sorted[m / 2 - 1] + sorted[m / 2]) * 0.5f;
    } else {
        median = sorted[m / 2];
    }

    uint64_t hash = 0;
    for (int i = 0; i < n; i++) {
        if (values[i] > median) {
            hash |= (1ULL << i);
        }
    }

    return hash;
}
