/*
 * test_internal.c
 *
 * Strengthened version of the original test_internal.c:
 * - Exact expected values for grayscale conversion (pure R, G, B)
 * - Extended Hamming tests: self-distance=0, full-flip=64
 */

#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <stdint.h>
#include <string.h>

/* =========================================================
 * ph_to_grayscale
 * ========================================================= */

static void test_grayscale_pure_red(void) {
    /* (255*38 + 0*75 + 0*15) >> 7 = 9690 >> 7 = 75 */
    uint8_t rgb[] = {255, 0, 0};
    uint8_t gray[1];
    ph_to_grayscale(NULL, rgb, 1, 1, 3, gray);
    ASSERT_UINT8_EQ(75, gray[0]);
    PASS("test_grayscale_pure_red");
}

static void test_grayscale_pure_green(void) {
    /* (0*38 + 255*75 + 0*15) >> 7 = 19125 >> 7 = 149 */
    uint8_t rgb[] = {0, 255, 0};
    uint8_t gray[1];
    ph_to_grayscale(NULL, rgb, 1, 1, 3, gray);
    ASSERT_UINT8_EQ(149, gray[0]);
    PASS("test_grayscale_pure_green");
}

static void test_grayscale_pure_blue(void) {
    /* (0*38 + 0*75 + 255*15) >> 7 = 3825 >> 7 = 29 */
    uint8_t rgb[] = {0, 0, 255};
    uint8_t gray[1];
    ph_to_grayscale(NULL, rgb, 1, 1, 3, gray);
    ASSERT_UINT8_EQ(29, gray[0]);
    PASS("test_grayscale_pure_blue");
}

static void test_grayscale_white(void) {
    /* (255*(38+75+15)) >> 7 = (255*128) >> 7 = 255 */
    uint8_t rgb[] = {255, 255, 255};
    uint8_t gray[1];
    ph_to_grayscale(NULL, rgb, 1, 1, 3, gray);
    ASSERT_UINT8_EQ(255, gray[0]);
    PASS("test_grayscale_white");
}

static void test_grayscale_1ch_passthrough(void) {
    uint8_t src[] = {0, 50, 100, 200, 255};
    uint8_t dst[5];
    ph_to_grayscale(NULL, src, 5, 1, 1, dst);
    for (int i = 0; i < 5; i++)
        ASSERT_UINT8_EQ(src[i], dst[i]);
    PASS("test_grayscale_1ch_passthrough");
}

/* =========================================================
 * ph_hamming_distance_digest
 * ========================================================= */

static void test_digest_hamming_known(void) {
    /* Original test: 0x01 vs 0x03 → 1 bit difference */
    ph_digest_t d1, d2;
    memset(&d1, 0, sizeof(d1));
    memset(&d2, 0, sizeof(d2));
    d1.size = 32;
    d2.size = 32;
    d1.data[0] = 0x01;
    d2.data[0] = 0x03;
    ASSERT_INT_EQ(1, ph_hamming_distance_digest(&d1, &d2));
    PASS("test_digest_hamming_known");
}

static void test_digest_hamming_self_zero(void) {
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    d.size = 8;
    d.data[0] = 0xAB;
    d.data[1] = 0xCD;
    ASSERT_INT_EQ(0, ph_hamming_distance_digest(&d, &d));
    PASS("test_digest_hamming_self_zero");
}

static void test_digest_hamming_full_flip(void) {
    /* All bits different across 8 bytes → distance = 64 */
    ph_digest_t d1, d2;
    memset(&d1, 0xFF, sizeof(d1));
    memset(&d2, 0x00, sizeof(d2));
    d1.size = d2.size = 8;
    /* The 0xFF fill also lands in `kind`, which is not a valid tag and is refused.
     * Say what these bytes are. */
    d1.kind = d2.kind = (uint8_t)PH_DIGEST_KIND_BITS;
    ASSERT_INT_EQ(64, ph_hamming_distance_digest(&d1, &d2));
    PASS("test_digest_hamming_full_flip");
}

/* =========================================================
 * ph_hamming_distance (uint64)
 * ========================================================= */

static void test_hamming_uint64_self(void) {
    ASSERT_INT_EQ(0, ph_hamming_distance(0xDEADBEEFCAFEBABEULL, 0xDEADBEEFCAFEBABEULL));
    PASS("test_hamming_uint64_self");
}

static void test_hamming_uint64_full_flip(void) {
    ASSERT_INT_EQ(64, ph_hamming_distance(0ULL, UINT64_MAX));
    PASS("test_hamming_uint64_full_flip");
}

/* =========================================================
 * main
 * ========================================================= */

int main(void) {
    test_grayscale_pure_red();
    test_grayscale_pure_green();
    test_grayscale_pure_blue();
    test_grayscale_white();
    test_grayscale_1ch_passthrough();

    test_digest_hamming_known();
    test_digest_hamming_self_zero();
    test_digest_hamming_full_flip();

    test_hamming_uint64_self();
    test_hamming_uint64_full_flip();

    printf("\nAll internal tests passed.\n");
    return 0;
}
