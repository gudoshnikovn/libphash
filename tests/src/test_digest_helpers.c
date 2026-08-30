#include "libphash.h"
#include "test_macros.h"
#include <string.h>

void test_hash_to_hex() {
    char hex[32];
    ASSERT_OK(ph_hash_to_hex(0x0123456789abcdefULL, hex, sizeof(hex)));
    ASSERT_STR_EQ("0123456789abcdef", hex);

    // Buffer too small.
    char tiny[16];
    ASSERT(ph_hash_to_hex(0, tiny, sizeof(tiny)) == PH_ERR_INVALID_ARGUMENT);

    ASSERT(ph_hash_to_hex(0, NULL, 32) == PH_ERR_INVALID_ARGUMENT);

    PASS("test_hash_to_hex");
}

void test_digest_hex_roundtrip() {
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    d.size = 8;
    for (int i = 0; i < d.size; i++)
        d.data[i] = (uint8_t)(i * 17 + 3);

    char hex[PH_DIGEST_MAX_BYTES * 2 + 1];
    ASSERT_OK(ph_digest_to_hex(&d, hex, sizeof(hex)));
    ASSERT_INT_EQ((int)(d.size * 2), (int)strlen(hex));

    ph_digest_t d2;
    memset(&d2, 0xAA, sizeof(d2));
    ASSERT_OK(ph_digest_from_hex(hex, &d2));
    ASSERT_INT_EQ(d.size, d2.size);
    ASSERT(memcmp(d.data, d2.data, d.size) == 0);

    // Full 64-byte digest round-trips too.
    ph_digest_t big;
    memset(&big, 0, sizeof(big));
    big.size = PH_DIGEST_MAX_BYTES;
    for (int i = 0; i < big.size; i++)
        big.data[i] = (uint8_t)(255 - i);

    char big_hex[PH_DIGEST_MAX_BYTES * 2 + 1];
    ASSERT_OK(ph_digest_to_hex(&big, big_hex, sizeof(big_hex)));

    ph_digest_t big2;
    ASSERT_OK(ph_digest_from_hex(big_hex, &big2));
    ASSERT_INT_EQ(big.size, big2.size);
    ASSERT(memcmp(big.data, big2.data, big.size) == 0);

    PASS("test_digest_hex_roundtrip");
}

void test_digest_hex_errors() {
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    d.size = 4;

    char small[4]; // too small for size 4 (needs 9 bytes)
    ASSERT(ph_digest_to_hex(&d, small, sizeof(small)) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_to_hex(NULL, small, sizeof(small)) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_to_hex(&d, NULL, 32) == PH_ERR_INVALID_ARGUMENT);

    ph_digest_t out;
    ASSERT(ph_digest_from_hex("abc", &out) == PH_ERR_INVALID_ARGUMENT); // odd length
    ASSERT(ph_digest_from_hex("zz", &out) == PH_ERR_INVALID_ARGUMENT);  // invalid hex digit
    ASSERT(ph_digest_from_hex(NULL, &out) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_from_hex("ab", NULL) == PH_ERR_INVALID_ARGUMENT);

    // Longer than PH_DIGEST_MAX_BYTES*2 hex chars must fail.
    char too_long[PH_DIGEST_MAX_BYTES * 2 + 3];
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    ASSERT(ph_digest_from_hex(too_long, &out) == PH_ERR_INVALID_ARGUMENT);

    PASS("test_digest_hex_errors");
}

void test_similarity() {
    ASSERT_FLOAT_EQ(1.0, ph_similarity(0x1234ULL, 0x1234ULL), 1e-9);
    ASSERT_FLOAT_EQ(0.0, ph_similarity(0ULL, ~0ULL), 1e-9);

    // One bit differing out of 64.
    double sim = ph_similarity(0ULL, 1ULL);
    ASSERT_FLOAT_EQ(1.0 - (1.0 / 64.0), sim, 1e-9);

    ph_digest_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.size = b.size = 8;
    ASSERT_FLOAT_EQ(1.0, ph_similarity_digest(&a, &b), 1e-9);

    b.data[0] = 0xFF; // 8 bits differ out of 64.
    ASSERT_FLOAT_EQ(1.0 - (8.0 / 64.0), ph_similarity_digest(&a, &b), 1e-9);

    // Mismatched sizes -> error sentinel.
    b.size = 4;
    ASSERT_FLOAT_EQ(-1.0, ph_similarity_digest(&a, &b), 1e-9);
    ASSERT_FLOAT_EQ(-1.0, ph_similarity_digest(NULL, &a), 1e-9);

    PASS("test_similarity");
}

int main() {
    test_hash_to_hex();
    test_digest_hex_roundtrip();
    test_digest_hex_errors();
    test_similarity();
    return 0;
}
