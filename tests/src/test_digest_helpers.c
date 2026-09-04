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

/* R07/M4: ph_digest_t is a flat public struct that callers (notably FFI bindings)
 * fill in by hand. `size` is a uint8_t, so it can hold 200 while `data` is only
 * PH_DIGEST_MAX_BYTES long -- every public function reading a digest must reject
 * that instead of reading past the end of the array. Run this under ASan: before
 * the fix the oversized cases read out of bounds. */
void test_digest_oversized_size_rejected() {
    ph_digest_t big = {0};
    ph_digest_t ok = {0};
    char hex[PH_DIGEST_MAX_BYTES * 4 + 1];

    big.size = 200; /* > PH_DIGEST_MAX_BYTES (64), representable in uint8_t */
    ok.size = PH_DIGEST_MAX_BYTES;

    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_digest_to_hex(&big, hex, sizeof(hex)));
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&big, &big));
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&big, &ok));
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&ok, &big));
    ASSERT(ph_similarity_digest(&big, &big) < 0.0);
    ASSERT(ph_l2_distance(&big, &big) < 0.0);

    /* Exactly at the limit must still work -- the check is > MAX, not >= MAX. */
    ASSERT_OK(ph_digest_to_hex(&ok, hex, sizeof(hex)));
    ASSERT_INT_EQ(0, ph_hamming_distance_digest(&ok, &ok));

    /* One byte over the limit is rejected. */
    ph_digest_t over = {0};
    over.size = PH_DIGEST_MAX_BYTES + 1;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_digest_to_hex(&over, hex, sizeof(hex)));
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&over, &over));

    PASS("test_digest_oversized_size_rejected");
}

/* A zero-length digest carries no bits. Comparing two of them and reporting
 * distance 0 would read as "identical", so the comparison helpers refuse. */
void test_digest_zero_size_not_comparable() {
    ph_digest_t empty = {0};
    ph_digest_t other = {0};
    char hex[PH_DIGEST_MAX_BYTES * 2 + 1];

    empty.size = 0;
    other.size = 4;

    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&empty, &empty));
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&empty, &other));
    ASSERT(ph_similarity_digest(&empty, &empty) < 0.0);
    ASSERT(ph_l2_distance(&empty, &empty) < 0.0);

    /* ph_digest_to_hex still accepts it: ph_digest_from_hex("") produces exactly
     * this digest, so rejecting it here would break the round trip. */
    ASSERT_OK(ph_digest_to_hex(&empty, hex, sizeof(hex)));
    ASSERT_STR_EQ("", hex);

    ph_digest_t from_empty = {0};
    from_empty.size = 42; /* must be overwritten */
    ASSERT_OK(ph_digest_from_hex("", &from_empty));
    ASSERT_INT_EQ(0, from_empty.size);

    PASS("test_digest_zero_size_not_comparable");
}

/* Mismatched sizes were untested (T5) -- both comparison helpers must say -1
 * rather than comparing the shorter prefix. */
void test_digest_size_mismatch() {
    ph_digest_t a = {0}, b = {0};
    a.size = 8;
    b.size = 16;

    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&a, &b));
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&b, &a));
    ASSERT(ph_similarity_digest(&a, &b) < 0.0);
    ASSERT(ph_l2_distance(&a, &b) < 0.0);
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(NULL, &a));
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&a, NULL));

    PASS("test_digest_size_mismatch");
}

/* ph_digest_from_hex documents uppercase support (common.c:174) but it was never
 * tested; and the round trip was only checked on a couple of fixed values (T5). */
void test_digest_hex_roundtrip_random_and_uppercase() {
    unsigned seed = 12345u; /* fixed: a failure must be reproducible */
    for (int iter = 0; iter < 1000; iter++) {
        ph_digest_t src = {0}, back = {0};
        char lower[PH_DIGEST_MAX_BYTES * 2 + 1];
        char upper[PH_DIGEST_MAX_BYTES * 2 + 1];

        seed = seed * 1103515245u + 12345u;
        src.size = (uint8_t)(1 + (seed >> 16) % PH_DIGEST_MAX_BYTES);
        for (int i = 0; i < src.size; i++) {
            seed = seed * 1103515245u + 12345u;
            src.data[i] = (uint8_t)(seed >> 16);
        }

        ASSERT_OK(ph_digest_to_hex(&src, lower, sizeof(lower)));
        ASSERT_OK(ph_digest_from_hex(lower, &back));
        ASSERT_INT_EQ(src.size, back.size);
        ASSERT_INT_EQ(0, memcmp(src.data, back.data, src.size));

        /* Same string uppercased must parse identically. */
        for (size_t i = 0; i < sizeof(upper); i++) {
            char c = lower[i];
            upper[i] = (c >= 'a' && c <= 'f') ? (char)(c - 'a' + 'A') : c;
            if (c == 0)
                break;
        }
        ph_digest_t from_upper = {0};
        ASSERT_OK(ph_digest_from_hex(upper, &from_upper));
        ASSERT_INT_EQ(src.size, from_upper.size);
        ASSERT_INT_EQ(0, memcmp(src.data, from_upper.data, src.size));
    }
    PASS("test_digest_hex_roundtrip_random_and_uppercase");
}

/* The kind tag: it refuses a metric, it never picks one.
 *
 * Added in 2.0.0 because five of the nine algorithms now return digests and three
 * different metrics apply to them. Comparing quantised DCT coefficients by Hamming
 * distance, or a histogram by L2, gives a plausible number that means nothing; this makes
 * the call fail instead. */
static void test_digest_kind_refuses_the_wrong_metric(void) {
    ph_digest_t bits, coeffs, vec;
    memset(&bits, 0, sizeof(bits));
    memset(&coeffs, 0, sizeof(coeffs));
    memset(&vec, 0, sizeof(vec));
    bits.size = coeffs.size = vec.size = 8;
    for (int i = 0; i < 8; i++) {
        bits.data[i] = (uint8_t)(0x0F * i);
        coeffs.data[i] = (uint8_t)(0x0F * i);
        vec.data[i] = (uint8_t)(0x0F * i);
    }
    bits.kind = (uint8_t)PH_DIGEST_KIND_BITS;
    coeffs.kind = (uint8_t)PH_DIGEST_KIND_COEFFICIENTS;
    vec.kind = (uint8_t)PH_DIGEST_KIND_VECTOR;

    /* Each metric accepts its own kind. */
    ASSERT_INT_EQ(0, ph_hamming_distance_digest(&bits, &bits));
    ASSERT_FLOAT_EQ(0.0, ph_l2_distance(&vec, &vec), 1e-9);
    double pcc = -9.0;
    ASSERT_OK(ph_radial_similarity(&coeffs, &coeffs, &pcc));
    ASSERT_FLOAT_EQ(1.0, pcc, 1e-9);

    /* And refuses the others, rather than returning a number about nothing. */
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&coeffs, &coeffs));
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&vec, &vec));
    ASSERT_FLOAT_EQ(-1.0, ph_similarity_digest(&coeffs, &coeffs), 1e-9);
    ASSERT_FLOAT_EQ(-1.0, ph_l2_distance(&bits, &bits), 1e-9);
    ASSERT_FLOAT_EQ(-1.0, ph_l2_distance(&coeffs, &coeffs), 1e-9);
    pcc = -9.0;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_radial_similarity(&bits, &bits, &pcc));
    ASSERT_FLOAT_EQ(-9.0, pcc, 1e-9); /* untouched on refusal */

    /* A mismatched pair is refused even when one side would be acceptable. */
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&bits, &coeffs));

    /* Unspecified -- the zero a hand-filled struct holds -- is accepted everywhere, so a
     * binding that never learned about the field behaves exactly as it did before. */
    ph_digest_t plain;
    memset(&plain, 0, sizeof(plain));
    plain.size = 8;
    memcpy(plain.data, bits.data, 8);
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_UNSPECIFIED, plain.kind);
    ASSERT_INT_EQ(0, ph_hamming_distance_digest(&plain, &plain));
    ASSERT_FLOAT_EQ(0.0, ph_l2_distance(&plain, &plain), 1e-9);
    ASSERT_OK(ph_radial_similarity(&plain, &plain, &pcc));
    ASSERT_INT_EQ(0, ph_hamming_distance_digest(&plain, &bits));

    /* A tag that is not a valid kind is not treated as "unspecified". */
    ph_digest_t garbage = bits;
    garbage.kind = 0xFF;
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&garbage, &garbage));

    PASS("test_digest_kind_refuses_the_wrong_metric");
}

static void test_computed_digests_carry_their_kind(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    if (ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg") != PH_SUCCESS) {
        fprintf(stderr, "Skip: fixture missing\n");
        ph_free(ctx);
        return;
    }
    ph_digest_t d;
    ASSERT_OK(ph_compute_bmh(ctx, &d));
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_BITS, d.kind);
    ASSERT_OK(ph_compute_radial_hash(ctx, &d));
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_COEFFICIENTS, d.kind);
    ASSERT_OK(ph_compute_color_moments_hash(ctx, &d));
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_VECTOR, d.kind);

    /* Decoded from text, nothing is claimed about the bytes. */
    ph_digest_t from_text;
    ASSERT_OK(ph_digest_from_hex("00ff8040", &from_text));
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_UNSPECIFIED, from_text.kind);

    ph_free(ctx);
    PASS("test_computed_digests_carry_their_kind");
}

int main() {
    test_hash_to_hex();
    test_digest_kind_refuses_the_wrong_metric();
    test_computed_digests_carry_their_kind();
    test_digest_hex_roundtrip();
    test_digest_hex_errors();
    test_similarity();
    test_digest_oversized_size_rejected();
    test_digest_zero_size_not_comparable();
    test_digest_size_mismatch();
    test_digest_hex_roundtrip_random_and_uppercase();
    return 0;
}
