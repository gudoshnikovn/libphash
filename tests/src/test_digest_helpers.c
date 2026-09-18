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

/* ph_digest_t is a flat public struct that callers (notably FFI bindings)
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

/* Mismatched sizes were untested -- both comparison helpers must say -1
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
 * tested; and the round trip was only checked on a couple of fixed values. */
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
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_VECTOR16, d.kind);

    /* Decoded from text, nothing is claimed about the bytes. */
    ph_digest_t from_text;
    ASSERT_OK(ph_digest_from_hex("00ff8040", &from_text));
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_UNSPECIFIED, from_text.kind);

    ph_free(ctx);
    PASS("test_computed_digests_carry_their_kind");
}

/* The buffer-size contracts are stated to the byte ("at least d->size * 2 + 1", "at least
 * 17"), and both were only ever tested well inside the bound. An off-by-one either way is
 * the difference between a rejected call and a one-byte overflow in the caller's buffer,
 * so both sides of both bounds are pinned here -- with a guard byte after the buffer to
 * catch the overflow if the check is ever loosened. */
static void test_hex_output_buffer_bounds() {
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    d.size = 5;
    for (int i = 0; i < d.size; i++)
        d.data[i] = (uint8_t)(0x10 * i + i);

    /* Exactly d.size * 2 + 1 must succeed; one byte less must be refused. */
    char exact[5 * 2 + 1 + 1];
    exact[sizeof(exact) - 1] = '#'; /* guard, past the size handed to the function */
    ASSERT_OK(ph_digest_to_hex(&d, exact, sizeof(exact) - 1));
    ASSERT_INT_EQ((int)(d.size * 2), (int)strlen(exact));
    ASSERT_INT_EQ('#', exact[sizeof(exact) - 1]);

    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_digest_to_hex(&d, exact, d.size * 2));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_digest_to_hex(&d, exact, 0));

    /* An empty digest still needs one byte, for the terminator. */
    ph_digest_t empty;
    memset(&empty, 0, sizeof(empty));
    char one[2];
    one[1] = '#';
    ASSERT_OK(ph_digest_to_hex(&empty, one, 1));
    ASSERT_INT_EQ('\0', one[0]);
    ASSERT_INT_EQ('#', one[1]);
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_digest_to_hex(&empty, one, 0));

    /* ph_hash_to_hex is fixed-width: 17 is the exact requirement, 16 is not enough. */
    char h[18];
    h[17] = '#';
    ASSERT_OK(ph_hash_to_hex(0xdeadbeefcafef00dULL, h, 17));
    ASSERT_STR_EQ("deadbeefcafef00d", h);
    ASSERT_INT_EQ('#', h[17]);
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_to_hex(0, h, 16));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_to_hex(0, h, 0));

    /* Both encoders are documented as big-endian and lowercase; a value with a
     * distinguishable byte order and every hex digit in it pins both at once. */
    ASSERT_OK(ph_hash_to_hex(0x0102030405060708ULL, h, sizeof(h)));
    ASSERT_STR_EQ("0102030405060708", h);

    ph_digest_t bytes;
    memset(&bytes, 0, sizeof(bytes));
    bytes.size = 3;
    bytes.data[0] = 0xAB;
    bytes.data[1] = 0xCD;
    bytes.data[2] = 0xEF;
    char order[8];
    ASSERT_OK(ph_digest_to_hex(&bytes, order, sizeof(order)));
    ASSERT_STR_EQ("abcdef", order); /* data[0] first, lowercase */

    PASS("test_hex_output_buffer_bounds");
}

/* ph_digest_from_hex() writes into a struct the caller supplies, which in practice is
 * often reused across a loop. Everything it does not decode must be reset, or a short
 * digest decoded over a long one leaves the previous digest's tail behind -- invisible
 * through `size`, but not to a caller that serialises or compares the whole struct. */
static void test_digest_from_hex_clears_the_whole_struct() {
    ph_digest_t d;
    memset(&d, 0xEE, sizeof(d)); /* every byte dirty, including kind and reserved */

    ASSERT_OK(ph_digest_from_hex("0011223344", &d));
    ASSERT_INT_EQ(5, d.size);
    ASSERT_UINT8_EQ(0x00, d.data[0]);
    ASSERT_UINT8_EQ(0x44, d.data[4]);
    for (int i = d.size; i < PH_DIGEST_MAX_BYTES; i++)
        ASSERT_UINT8_EQ(0, d.data[i]);
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_UNSPECIFIED, d.kind);
    for (size_t i = 0; i < sizeof(d.reserved); i++)
        ASSERT_UINT8_EQ(0, d.reserved[i]);

    /* Decoding a shorter string over a longer digest must shrink it, tail and all. */
    ASSERT_OK(ph_digest_from_hex("ff", &d));
    ASSERT_INT_EQ(1, d.size);
    for (int i = 1; i < PH_DIGEST_MAX_BYTES; i++)
        ASSERT_UINT8_EQ(0, d.data[i]);

    /* A rejected string must leave the digest exactly as it was: a half-decoded digest
     * reported through an error code is the one thing worse than either outcome. */
    ph_digest_t before = d;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_digest_from_hex("00zz", &d));
    ASSERT_INT_EQ(before.size, d.size);

    /* Exactly PH_DIGEST_MAX_BYTES * 2 digits is the largest accepted string; two more is
     * one byte too many. */
    char max_len[PH_DIGEST_MAX_BYTES * 2 + 1];
    memset(max_len, 'f', sizeof(max_len) - 1);
    max_len[sizeof(max_len) - 1] = '\0';
    ASSERT_OK(ph_digest_from_hex(max_len, &d));
    ASSERT_INT_EQ(PH_DIGEST_MAX_BYTES, d.size);

    char over_len[PH_DIGEST_MAX_BYTES * 2 + 3];
    memset(over_len, 'f', sizeof(over_len) - 1);
    over_len[sizeof(over_len) - 1] = '\0';
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_digest_from_hex(over_len, &d));

    PASS("test_digest_from_hex_clears_the_whole_struct");
}

/* Whatever ph_digest_to_hex() emits, ph_digest_from_hex() must take back -- including for
 * digests that came out of the algorithms rather than out of a test's byte pattern. This
 * is the round trip a caller actually performs (hash, store as text, read back, compare),
 * and it is checked against the digest's own `size` rather than any hardcoded width,
 * because those widths are still moving in 2.0.0. */
static void test_digest_hex_roundtrip_on_computed_digests() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    if (ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg") != PH_SUCCESS) {
        fprintf(stderr, "Skip: fixture missing\n");
        ph_free(ctx);
        return;
    }

    ph_digest_t digests[5];
    int n = 0;
    ASSERT_OK(ph_compute_bmh(ctx, &digests[n++]));
    ASSERT_OK(ph_compute_radial_hash(ctx, &digests[n++]));
    ASSERT_OK(ph_compute_mhash(ctx, &digests[n++]));
    ASSERT_OK(ph_compute_color_moments_hash(ctx, &digests[n++]));
    ASSERT_OK(ph_compute_color_hash(ctx, &digests[n++]));

    for (int i = 0; i < n; i++) {
        ASSERT(digests[i].size > 0);
        ASSERT(digests[i].size <= PH_DIGEST_MAX_BYTES);

        char hex[PH_DIGEST_MAX_BYTES * 2 + 1];
        ASSERT_OK(ph_digest_to_hex(&digests[i], hex, sizeof(hex)));
        ASSERT_INT_EQ((int)(digests[i].size * 2), (int)strlen(hex));

        ph_digest_t back;
        ASSERT_OK(ph_digest_from_hex(hex, &back));
        ASSERT_INT_EQ(digests[i].size, back.size);
        ASSERT_INT_EQ(0, memcmp(digests[i].data, back.data, digests[i].size));

        /* The text carries no kind, so the decoded digest is untagged -- and therefore
         * still comparable against the tagged original, which is what makes storing a
         * digest as hex usable at all. */
        ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_UNSPECIFIED, back.kind);
    }

    ph_free(ctx);
    PASS("test_digest_hex_roundtrip_on_computed_digests");
}

/* ph_l2_distance() had no negative-path coverage of its own: it was only reached through
 * the shared oversized/zero-size cases. NULL on either side, and the metric's own
 * properties (zero to itself, symmetric, the value it is defined to compute). */
static void test_l2_distance_contract() {
    ph_digest_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.size = b.size = 4;
    a.data[0] = 0;
    a.data[1] = 3;
    a.data[2] = 0;
    a.data[3] = 0;
    b.data[0] = 4;
    b.data[1] = 0;
    b.data[2] = 0;
    b.data[3] = 0;

    /* sqrt(4^2 + 3^2) = 5, the one case where the arithmetic is checkable by eye. */
    ASSERT_FLOAT_EQ(5.0, ph_l2_distance(&a, &b), 1e-9);
    ASSERT_FLOAT_EQ(5.0, ph_l2_distance(&b, &a), 1e-9);
    ASSERT_FLOAT_EQ(0.0, ph_l2_distance(&a, &a), 1e-9);

    ASSERT_FLOAT_EQ(-1.0, ph_l2_distance(NULL, &b), 1e-9);
    ASSERT_FLOAT_EQ(-1.0, ph_l2_distance(&a, NULL), 1e-9);
    ASSERT_FLOAT_EQ(-1.0, ph_l2_distance(NULL, NULL), 1e-9);

    /* The maximum over a full-width digest: every byte 0 against every byte 255. */
    ph_digest_t lo, hi;
    memset(&lo, 0, sizeof(lo));
    memset(&hi, 0, sizeof(hi));
    lo.size = hi.size = PH_DIGEST_MAX_BYTES;
    memset(hi.data, 0xFF, PH_DIGEST_MAX_BYTES);
    ASSERT_FLOAT_EQ(255.0 * sqrt((double)PH_DIGEST_MAX_BYTES), ph_l2_distance(&lo, &hi), 1e-6);

    PASS("test_l2_distance_contract");
}

/* ph_similarity()/ph_hamming_distance() are the uint64_t pair, and the property that
 * matters is that the two agree: similarity is exactly 1 - distance/64 for every input,
 * not only for the handful of values spot-checked above. */
static void test_similarity_agrees_with_hamming() {
    unsigned seed = 987654321u;
    for (int i = 0; i < 1000; i++) {
        uint64_t a = 0, b = 0;
        for (int k = 0; k < 4; k++) {
            seed = seed * 1103515245u + 12345u;
            a = (a << 16) | ((seed >> 8) & 0xFFFF);
            seed = seed * 1103515245u + 12345u;
            b = (b << 16) | ((seed >> 8) & 0xFFFF);
        }
        int dist = ph_hamming_distance(a, b);
        ASSERT(dist >= 0 && dist <= 64);
        ASSERT_INT_EQ(dist, ph_hamming_distance(b, a));
        ASSERT_FLOAT_EQ(1.0 - (double)dist / 64.0, ph_similarity(a, b), 1e-12);
        ASSERT_INT_EQ(0, ph_hamming_distance(a, a));
    }
    PASS("test_similarity_agrees_with_hamming");
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
    test_hex_output_buffer_bounds();
    test_digest_from_hex_clears_the_whole_struct();
    test_digest_hex_roundtrip_on_computed_digests();
    test_l2_distance_contract();
    test_similarity_agrees_with_hamming();
    return 0;
}
