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

void test_hash_from_hex() {
    uint64_t h = 0;
    ASSERT_OK(ph_hash_from_hex("0123456789abcdef", &h));
    ASSERT(h == 0x0123456789abcdefULL);
    ASSERT_OK(ph_hash_from_hex("DEADBEEFcafeF00D", &h));
    ASSERT(h == 0xdeadbeefcafef00dULL);
    ASSERT_OK(ph_hash_from_hex("0000000000000000", &h));
    ASSERT(h == 0);

    /* Round trip through ph_hash_to_hex() for values that exercise every byte. */
    unsigned seed = 777u;
    for (int i = 0; i < 1000; i++) {
        seed = seed * 1103515245u + 12345u;
        uint64_t v = ((uint64_t)seed << 32) ^ (seed * 2654435761u);
        char hex[17];
        ASSERT_OK(ph_hash_to_hex(v, hex, sizeof(hex)));
        uint64_t back = ~v;
        ASSERT_OK(ph_hash_from_hex(hex, &back));
        ASSERT(back == v);
    }

    /* Exactly 16 hex digits and nothing else; a refused string leaves `out` alone. */
    const char *bad[] = {
        "",
        "0123456789abcde",
        "0123456789abcdef0",
        "0x0123456789abcd",
        " 0123456789abcdef",
        "0123456789abcdef ",
        "0123456789abcdeg",
        "bits:0123456789ab",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        h = 42;
        ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_from_hex(bad[i], &h));
        ASSERT(h == 42);
    }
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_from_hex(NULL, &h));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_hash_from_hex("0123456789abcdef", NULL));

    PASS("test_hash_from_hex");
}

void test_digest_hex_roundtrip() {
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    d.size = 8;
    for (int i = 0; i < d.size; i++)
        d.data[i] = (uint8_t)(i * 17 + 3);

    char hex[PH_DIGEST_HEX_BUFFER_SIZE];
    ASSERT_OK(ph_digest_to_hex(&d, hex, sizeof(hex)));
    ASSERT_STR_EQ("unspecified:031425364758697a", hex);

    ph_digest_t d2;
    memset(&d2, 0xAA, sizeof(d2));
    ASSERT_OK(ph_digest_from_hex(hex, &d2));
    ASSERT_INT_EQ(d.size, d2.size);
    ASSERT(memcmp(d.data, d2.data, d.size) == 0);
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_UNSPECIFIED, d2.kind);

    // A full PH_DIGEST_MAX_BYTES digest round-trips too.
    ph_digest_t big;
    memset(&big, 0, sizeof(big));
    big.size = PH_DIGEST_MAX_BYTES;
    for (int i = 0; i < big.size; i++)
        big.data[i] = (uint8_t)(255 - i);

    big.kind = PH_DIGEST_KIND_COEFFICIENTS; /* the longest kind name */
    char big_hex[PH_DIGEST_HEX_BUFFER_SIZE];
    ASSERT_OK(ph_digest_to_hex(&big, big_hex, sizeof(big_hex)));
    ASSERT_INT_EQ(PH_DIGEST_HEX_BUFFER_SIZE - 1, (int)strlen(big_hex));

    ph_digest_t big2;
    ASSERT_OK(ph_digest_from_hex(big_hex, &big2));
    ASSERT_INT_EQ(big.size, big2.size);
    ASSERT(memcmp(big.data, big2.data, big.size) == 0);
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_COEFFICIENTS, big2.kind);

    PASS("test_digest_hex_roundtrip");
}

void test_digest_hex_errors() {
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    d.size = 4;

    /* "unspecified:" + 8 hex digits + NUL = 21 bytes; one short must be refused. */
    char small[20];
    ASSERT(ph_digest_to_hex(&d, small, sizeof(small)) == PH_ERR_INVALID_ARGUMENT);
    char exact[21];
    ASSERT_OK(ph_digest_to_hex(&d, exact, sizeof(exact)));
    ASSERT(ph_digest_to_hex(NULL, small, sizeof(small)) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_to_hex(&d, NULL, 32) == PH_ERR_INVALID_ARGUMENT);
    /* A kind byte that names no ph_digest_kind_t value has no text form. */
    d.kind = PH_DIGEST_KIND_VECTOR16 + 1;
    ASSERT(ph_digest_to_hex(&d, exact, sizeof(exact)) == PH_ERR_INVALID_ARGUMENT);
    d.kind = 0xFF;
    ASSERT(ph_digest_to_hex(&d, exact, sizeof(exact)) == PH_ERR_INVALID_ARGUMENT);

    ph_digest_t out;
    ASSERT(ph_digest_from_hex("bits:abc", &out) == PH_ERR_INVALID_ARGUMENT); // odd length
    ASSERT(ph_digest_from_hex("bits:zz", &out) == PH_ERR_INVALID_ARGUMENT);  // invalid digit
    ASSERT(ph_digest_from_hex(NULL, &out) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_from_hex("bits:ab", NULL) == PH_ERR_INVALID_ARGUMENT);

    /* The kind prefix is required, exact and lowercase: a bare hex string must not
     * silently come back untagged. */
    ASSERT(ph_digest_from_hex("abcd", &out) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_from_hex("", &out) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_from_hex(":abcd", &out) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_from_hex("bit:abcd", &out) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_from_hex("bitsx:abcd", &out) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_from_hex("BITS:abcd", &out) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_from_hex("bits:ab:cd", &out) == PH_ERR_INVALID_ARGUMENT);
    ASSERT(ph_digest_from_hex(" bits:abcd", &out) == PH_ERR_INVALID_ARGUMENT);

    // Longer than PH_DIGEST_MAX_BYTES*2 hex digits must fail.
    char too_long[5 + PH_DIGEST_MAX_BYTES * 2 + 3];
    memcpy(too_long, "bits:", 5);
    memset(too_long + 5, 'a', sizeof(too_long) - 6);
    too_long[sizeof(too_long) - 1] = '\0';
    ASSERT(ph_digest_from_hex(too_long, &out) == PH_ERR_INVALID_ARGUMENT);

    /* A refused string leaves the output alone, even when the failure is found halfway
     * through the digits. */
    ph_digest_t keep;
    memset(&keep, 0x5A, sizeof(keep));
    ph_digest_t before = keep;
    ASSERT(ph_digest_from_hex("bits:0011zz", &keep) == PH_ERR_INVALID_ARGUMENT);
    ASSERT_INT_EQ(0, memcmp(&before, &keep, sizeof(keep)));

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
 * that instead of reading past the end of the array. Run this under ASan, which
 * catches an out-of-bounds read. */
void test_digest_oversized_size_rejected() {
    ph_digest_t big = {0};
    ph_digest_t ok = {0};
    char hex[PH_DIGEST_MAX_BYTES * 4 + 1];

    big.size = 200; /* > PH_DIGEST_MAX_BYTES (128), representable in uint8_t */
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
    char hex[PH_DIGEST_HEX_BUFFER_SIZE];

    empty.size = 0;
    other.size = 4;

    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&empty, &empty));
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&empty, &other));
    ASSERT(ph_similarity_digest(&empty, &empty) < 0.0);
    ASSERT(ph_l2_distance(&empty, &empty) < 0.0);

    /* ph_digest_to_hex still accepts it: ph_digest_from_hex("unspecified:") produces
     * exactly this digest, so rejecting it here would break the round trip. */
    ASSERT_OK(ph_digest_to_hex(&empty, hex, sizeof(hex)));
    ASSERT_STR_EQ("unspecified:", hex);

    ph_digest_t from_empty = {0};
    from_empty.size = 42; /* must be overwritten */
    ASSERT_OK(ph_digest_from_hex("unspecified:", &from_empty));
    ASSERT_INT_EQ(0, from_empty.size);

    PASS("test_digest_zero_size_not_comparable");
}

/* Mismatched sizes: both comparison helpers must say -1 rather than comparing
 * the shorter prefix. */
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

/* ph_digest_from_hex() accepts uppercase hex digits; round-trip random digests in both
 * cases. */
void test_digest_hex_roundtrip_random_and_uppercase() {
    unsigned seed = 12345u; /* fixed: a failure must be reproducible */
    for (int iter = 0; iter < 1000; iter++) {
        ph_digest_t src = {0}, back = {0};
        char lower[PH_DIGEST_HEX_BUFFER_SIZE];
        char upper[PH_DIGEST_HEX_BUFFER_SIZE];

        seed = seed * 1103515245u + 12345u;
        src.size = (uint8_t)(1 + (seed >> 16) % PH_DIGEST_MAX_BYTES);
        src.kind = (uint8_t)((seed >> 8) % (PH_DIGEST_KIND_VECTOR16 + 1));
        for (int i = 0; i < src.size; i++) {
            seed = seed * 1103515245u + 12345u;
            src.data[i] = (uint8_t)(seed >> 16);
        }

        ASSERT_OK(ph_digest_to_hex(&src, lower, sizeof(lower)));
        ASSERT_OK(ph_digest_from_hex(lower, &back));
        ASSERT_INT_EQ(src.size, back.size);
        ASSERT_INT_EQ(0, memcmp(src.data, back.data, src.size));
        ASSERT_INT_EQ(src.kind, back.kind);

        /* Same digits uppercased must parse identically; the kind name stays lowercase. */
        const char *digits = strchr(lower, ':') + 1;
        for (size_t i = 0; i < sizeof(upper); i++) {
            char c = lower[i];
            upper[i] = (&lower[i] >= digits && c >= 'a' && c <= 'f') ? (char)(c - 'a' + 'A') : c;
            if (c == 0)
                break;
        }
        ph_digest_t from_upper = {0};
        ASSERT_OK(ph_digest_from_hex(upper, &from_upper));
        ASSERT_INT_EQ(src.size, from_upper.size);
        ASSERT_INT_EQ(0, memcmp(src.data, from_upper.data, src.size));
        ASSERT_INT_EQ(src.kind, from_upper.kind);
    }
    PASS("test_digest_hex_roundtrip_random_and_uppercase");
}

/* The kind tag: it refuses a metric, it never picks one.
 *
 * Five of the nine algorithms return digests and three different metrics apply to
 * them. Comparing quantised DCT coefficients by Hamming
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
     * binding that does not set the field keeps working unchanged. */
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

    /* Decoded from text, the digest claims exactly what the text says -- and nothing
     * when the text says "unspecified". */
    ph_digest_t from_text;
    ASSERT_OK(ph_digest_from_hex("coefficients:00ff8040", &from_text));
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_COEFFICIENTS, from_text.kind);
    ASSERT_OK(ph_digest_from_hex("unspecified:00ff8040", &from_text));
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_UNSPECIFIED, from_text.kind);

    ph_free(ctx);
    PASS("test_computed_digests_carry_their_kind");
}

/* The buffer-size contracts are stated to the byte ("the kind name's length + 1 +
 * d->size * 2 + 1", "at least 17"). An off-by-one either way is the difference between a rejected
 * call and a one-byte overflow in the caller's buffer, so both sides of both bounds are pinned here
 * -- with a guard byte after the buffer to catch the overflow if the check is ever loosened. */
static void test_hex_output_buffer_bounds() {
    ph_digest_t d;
    memset(&d, 0, sizeof(d));
    d.size = 5;
    for (int i = 0; i < d.size; i++)
        d.data[i] = (uint8_t)(0x10 * i + i);

    /* "bits:" + d.size * 2 + 1 must succeed; one byte less must be refused. */
    d.kind = PH_DIGEST_KIND_BITS;
    char exact[5 + 5 * 2 + 1 + 1];
    exact[sizeof(exact) - 1] = '#'; /* guard, past the size handed to the function */
    ASSERT_OK(ph_digest_to_hex(&d, exact, sizeof(exact) - 1));
    ASSERT_INT_EQ(5 + (int)(d.size * 2), (int)strlen(exact));
    ASSERT_INT_EQ('#', exact[sizeof(exact) - 1]);

    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_digest_to_hex(&d, exact, sizeof(exact) - 2));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_digest_to_hex(&d, exact, 0));

    /* An empty digest still needs its prefix and the terminator. */
    ph_digest_t empty;
    memset(&empty, 0, sizeof(empty));
    char prefix_only[sizeof("unspecified:") + 1];
    prefix_only[sizeof(prefix_only) - 1] = '#';
    ASSERT_OK(ph_digest_to_hex(&empty, prefix_only, sizeof("unspecified:")));
    ASSERT_STR_EQ("unspecified:", prefix_only);
    ASSERT_INT_EQ('#', prefix_only[sizeof(prefix_only) - 1]);
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT,
                  ph_digest_to_hex(&empty, prefix_only, sizeof("unspecified:") - 1));

    /* PH_DIGEST_HEX_BUFFER_SIZE is enough for a full digest of every kind. */
    for (int kind = 0; kind <= PH_DIGEST_KIND_VECTOR16; kind++) {
        ph_digest_t full;
        memset(&full, 0xAB, sizeof(full));
        full.size = PH_DIGEST_MAX_BYTES;
        full.kind = (uint8_t)kind;
        char buf[PH_DIGEST_HEX_BUFFER_SIZE];
        ASSERT_OK(ph_digest_to_hex(&full, buf, sizeof(buf)));
    }

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
    bytes.kind = PH_DIGEST_KIND_HISTOGRAM;
    char order[32];
    ASSERT_OK(ph_digest_to_hex(&bytes, order, sizeof(order)));
    ASSERT_STR_EQ("histogram:abcdef", order); /* data[0] first, lowercase */

    PASS("test_hex_output_buffer_bounds");
}

/* ph_digest_from_hex() writes into a struct the caller supplies, which in practice is
 * often reused across a loop. Everything it does not decode must be reset, or a short
 * digest decoded over a long one leaves the previous digest's tail behind -- invisible
 * through `size`, but not to a caller that serialises or compares the whole struct. */
static void test_digest_from_hex_clears_the_whole_struct() {
    ph_digest_t d;
    memset(&d, 0xEE, sizeof(d)); /* every byte dirty, including kind and reserved */

    ASSERT_OK(ph_digest_from_hex("vector:0011223344", &d));
    ASSERT_INT_EQ(5, d.size);
    ASSERT_UINT8_EQ(0x00, d.data[0]);
    ASSERT_UINT8_EQ(0x44, d.data[4]);
    for (int i = d.size; i < PH_DIGEST_MAX_BYTES; i++)
        ASSERT_UINT8_EQ(0, d.data[i]);
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_VECTOR, d.kind);
    for (size_t i = 0; i < sizeof(d.reserved); i++)
        ASSERT_UINT8_EQ(0, d.reserved[i]);

    /* Decoding a shorter string over a longer digest must shrink it, tail and all. */
    ASSERT_OK(ph_digest_from_hex("bits:ff", &d));
    ASSERT_INT_EQ(1, d.size);
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_BITS, d.kind);
    for (int i = 1; i < PH_DIGEST_MAX_BYTES; i++)
        ASSERT_UINT8_EQ(0, d.data[i]);

    /* A rejected string must leave the digest exactly as it was: a half-decoded digest
     * reported through an error code is the one thing worse than either outcome. */
    ph_digest_t before = d;
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_digest_from_hex("bits:00zz", &d));
    ASSERT_INT_EQ(0, memcmp(&before, &d, sizeof(d)));

    /* Exactly PH_DIGEST_MAX_BYTES * 2 digits is the largest accepted string; two more is
     * one byte too many. */
    char max_len[5 + PH_DIGEST_MAX_BYTES * 2 + 1];
    memcpy(max_len, "bits:", 5);
    memset(max_len + 5, 'f', sizeof(max_len) - 6);
    max_len[sizeof(max_len) - 1] = '\0';
    ASSERT_OK(ph_digest_from_hex(max_len, &d));
    ASSERT_INT_EQ(PH_DIGEST_MAX_BYTES, d.size);

    char over_len[5 + PH_DIGEST_MAX_BYTES * 2 + 3];
    memcpy(over_len, "bits:", 5);
    memset(over_len + 5, 'f', sizeof(over_len) - 6);
    over_len[sizeof(over_len) - 1] = '\0';
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_digest_from_hex(over_len, &d));

    PASS("test_digest_from_hex_clears_the_whole_struct");
}

/* Whatever ph_digest_to_hex() emits, ph_digest_from_hex() must take back -- including for
 * digests that came out of the algorithms rather than out of a test's byte pattern. This
 * is the round trip a caller actually performs (hash, store as text, read back, compare),
 * and it is checked against the digest's own `size` rather than any hardcoded width, so
 * the test does not hardcode them. */
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

        char hex[PH_DIGEST_HEX_BUFFER_SIZE];
        ASSERT_OK(ph_digest_to_hex(&digests[i], hex, sizeof(hex)));
        const char *digits = strchr(hex, ':');
        ASSERT_PTR_NOT_NULL((void *)digits);
        ASSERT_INT_EQ((int)(digests[i].size * 2), (int)strlen(digits + 1));

        ph_digest_t back;
        ASSERT_OK(ph_digest_from_hex(hex, &back));
        ASSERT_INT_EQ(digests[i].size, back.size);
        ASSERT_INT_EQ(0, memcmp(digests[i].data, back.data, digests[i].size));

        /* Every algorithm tags its digest, and the text carries the tag: a digest read
         * back from storage is protected by it exactly as the live one is. */
        ASSERT(digests[i].kind != PH_DIGEST_KIND_UNSPECIFIED);
        ASSERT_INT_EQ(digests[i].kind, back.kind);
    }

    ph_free(ctx);
    PASS("test_digest_hex_roundtrip_on_computed_digests");
}

/* Both sides of a comparison read back from storage: two radial digests stored as text
 * and decoded again must still be refused by the bit metrics (which would otherwise
 * return a plausible-looking number) and accepted by the radial one. */
static void test_stored_digests_keep_their_protection() {
    const char *paths[2] = {TEST_DATA_DIR "/photo.jpeg", TEST_DATA_DIR "/photo_rotated_90.jpeg"};
    ph_digest_t stored[2];
    for (int i = 0; i < 2; i++) {
        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_load_from_file(ctx, paths[i]));
        ph_digest_t live;
        ASSERT_OK(ph_compute_radial_hash(ctx, &live));
        ph_free(ctx);

        char text[PH_DIGEST_HEX_BUFFER_SIZE];
        ASSERT_OK(ph_digest_to_hex(&live, text, sizeof(text)));
        ASSERT_OK(ph_digest_from_hex(text, &stored[i]));
    }

    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&stored[0], &stored[1]));
    ASSERT(ph_similarity_digest(&stored[0], &stored[1]) < 0.0);
    double pcc = 0.0;
    ASSERT_OK(ph_radial_similarity(&stored[0], &stored[1], &pcc));
    ASSERT(pcc >= -1.0 && pcc <= 1.0);

    PASS("test_stored_digests_keep_their_protection");
}

/* ph_l2_distance()'s own negative paths: NULL on either side, and the metric's own
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
    test_hash_from_hex();
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
    test_stored_digests_keep_their_protection();
    test_l2_distance_contract();
    test_similarity_agrees_with_hamming();
    return 0;
}
