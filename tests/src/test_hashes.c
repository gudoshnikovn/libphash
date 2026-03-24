#include "libphash.h"
#include "test_macros.h"

void test_hamming() {
    uint64_t h1 = 0x0000000000000000ULL;
    uint64_t h2 = 0x000000000000000FULL; // 4 bits set
    ASSERT_INT_EQ(4, ph_hamming_distance(h1, h2));
    printf("test_hamming: PASSED\n");
}

int main() {
    test_hamming();
    return 0;
}
