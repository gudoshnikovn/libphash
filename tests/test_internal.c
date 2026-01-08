#include "../src/internal.h"
#include "test_macros.h"
#include <stdint.h>
#include <string.h>

void test_grayscale_conversion() {
    uint8_t rgb[] = {255, 0, 0, 0, 255, 0};
    uint8_t gray[2];
    ph_to_grayscale(rgb, 2, 1, 3, gray);
    if (gray[0] == 0 || gray[1] == 0) {
        fprintf(stderr, "Grayscale conversion produced black pixels\n");
        exit(1);
    }
    printf("test_grayscale_conversion: PASSED\n");
}

void test_digest_hamming() {
    ph_digest_t d1, d2;
    // Initialize manually for test
    memset(&d1, 0, sizeof(d1));
    memset(&d2, 0, sizeof(d2));
    d1.size = 32;
    d2.size = 32;

    d1.data[0] = 0x01;
    d2.data[0] = 0x03;

    ASSERT_INT_EQ(1, ph_hamming_distance_digest(&d1, &d2));
    printf("test_digest_hamming: PASSED\n");
}

int main() {
    test_grayscale_conversion();
    test_digest_hamming();
    return 0;
}
