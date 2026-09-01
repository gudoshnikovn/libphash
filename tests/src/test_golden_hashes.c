// Task 13: golden-hash regression test. Computes every algorithm's hash for
// every valid fixture in tests/data/ and compares against a committed golden
// file (tests/data/golden_hashes.txt) -- any unintentional change to hash
// output (e.g. an optimization that subtly changes results) shows up as a
// failing test here, instead of silently shipping.
//
// Comparison is by Hamming distance with a small tolerance
// (GOLDEN_TOLERANCE_BITS), not exact byte equality: lossy formats (JPEG,
// WebP) can legitimately decode to very slightly different pixels across
// decoder backends (e.g. TurboJPEG vs. the stb_image fallback have different
// IDCT rounding) without anything being wrong. A real algorithm regression
// moves many bits, not one or two -- the tolerance is tight enough to still
// catch that.
//
// Run with --update to regenerate the golden file after a verified,
// intentional change to an algorithm's output.
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GOLDEN_TOLERANCE_BITS 2

static const char *FIXTURES[] = {
    "photo.jpeg",
    "photo_copy.jpeg",
    "photo_color_changed.jpeg",
    "photo_rotated_90.jpeg",
    "photo.png",
    "photo_complex.png",
    "photo.webp",
    "photo_complex.webp",
};
#define NUM_FIXTURES (sizeof(FIXTURES) / sizeof(FIXTURES[0]))

static const char *UINT64_ALGO_NAMES[PH_HASH_FLAGS_COUNT] = {"aHash", "dHash", "pHash",
                                                             "wHash", "mHash", "ColorHash"};

typedef ph_error_t (*digest_fn_t)(ph_context_t *, ph_digest_t *);
static const char *DIGEST_ALGO_NAMES[] = {"BMH", "ColorMoments", "Radial"};
static const digest_fn_t DIGEST_FNS[] = {ph_compute_bmh, ph_compute_color_moments_hash,
                                         ph_compute_radial_hash};
#define NUM_DIGEST_ALGOS (sizeof(DIGEST_ALGO_NAMES) / sizeof(DIGEST_ALGO_NAMES[0]))

typedef struct {
    char filename[64];
    char algo[32];
    char hex[PH_DIGEST_MAX_BYTES * 2 + 1];
} golden_entry_t;

static golden_entry_t g_golden[NUM_FIXTURES * (PH_HASH_FLAGS_COUNT + NUM_DIGEST_ALGOS)];
static int g_golden_count = 0;
static int g_mismatches = 0;
static int g_checked = 0;

static const char *golden_path(void) { return TEST_DATA_DIR "/golden_hashes.txt"; }

static void load_golden(void) {
    FILE *f = fopen(golden_path(), "r");
    if (!f) {
        fprintf(stderr, "[FAIL] test_golden_hashes - could not open %s\n", golden_path());
        exit(1);
    }
    while (g_golden_count < (int)(sizeof(g_golden) / sizeof(g_golden[0])) &&
           fscanf(f, "%63s %31s %128s", g_golden[g_golden_count].filename,
                  g_golden[g_golden_count].algo, g_golden[g_golden_count].hex) == 3) {
        g_golden_count++;
    }
    fclose(f);
}

static const char *find_golden(const char *filename, const char *algo) {
    for (int i = 0; i < g_golden_count; i++) {
        if (strcmp(g_golden[i].filename, filename) == 0 && strcmp(g_golden[i].algo, algo) == 0)
            return g_golden[i].hex;
    }
    return NULL;
}

static void check_uint64(const char *filename, const char *algo, uint64_t value, FILE *update_out) {
    char hex[17];
    ASSERT_OK(ph_hash_to_hex(value, hex, sizeof(hex)));
    if (update_out) {
        fprintf(update_out, "%s %s %s\n", filename, algo, hex);
        return;
    }
    const char *expected_hex = find_golden(filename, algo);
    if (!expected_hex) {
        fprintf(stderr,
                "[FAIL] test_golden_hashes - no golden entry for %s/%s (run with --update "
                "after verifying this is intentional)\n",
                filename, algo);
        g_mismatches++;
        return;
    }
    uint64_t expected = strtoull(expected_hex, NULL, 16);
    int dist = ph_hamming_distance(expected, value);
    g_checked++;
    if (dist > GOLDEN_TOLERANCE_BITS) {
        fprintf(
            stderr,
            "[FAIL] test_golden_hashes - %s/%s changed: golden=%s actual=%s (dist=%d, max %d)\n",
            filename, algo, expected_hex, hex, dist, GOLDEN_TOLERANCE_BITS);
        g_mismatches++;
    }
}

static void check_digest(const char *filename, const char *algo, const ph_digest_t *value,
                         FILE *update_out) {
    char hex[PH_DIGEST_MAX_BYTES * 2 + 1];
    ASSERT_OK(ph_digest_to_hex(value, hex, sizeof(hex)));
    if (update_out) {
        fprintf(update_out, "%s %s %s\n", filename, algo, hex);
        return;
    }
    const char *expected_hex = find_golden(filename, algo);
    if (!expected_hex) {
        fprintf(stderr,
                "[FAIL] test_golden_hashes - no golden entry for %s/%s (run with --update "
                "after verifying this is intentional)\n",
                filename, algo);
        g_mismatches++;
        return;
    }
    ph_digest_t expected;
    ASSERT_OK(ph_digest_from_hex(expected_hex, &expected));
    int dist = ph_hamming_distance_digest(&expected, value);
    g_checked++;
    if (dist > GOLDEN_TOLERANCE_BITS) {
        fprintf(
            stderr,
            "[FAIL] test_golden_hashes - %s/%s changed: golden=%s actual=%s (dist=%d, max %d)\n",
            filename, algo, expected_hex, hex, dist, GOLDEN_TOLERANCE_BITS);
        g_mismatches++;
    }
}

static void process_fixture(const char *filename, FILE *update_out) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", TEST_DATA_DIR, filename);

    ph_context_t *ctx;
    ASSERT_OK(ph_create(&ctx));
    ph_error_t err = ph_load_from_file(ctx, path);
    if (err == PH_ERR_DECODER_UNAVAILABLE) {
        // e.g. WebP fixtures on a build without PH_USE_WEBP -- not a failure,
        // just nothing to check on this build.
        printf("  %s: SKIPPED (%s)\n", filename, ph_get_error_string(err));
        ph_free(ctx);
        return;
    }
    if (err != PH_SUCCESS) {
        fprintf(stderr, "[FAIL] test_golden_hashes - could not load %s: %s\n", filename,
                ph_get_error_string(err));
        exit(1);
    }

    uint64_t hashes[PH_HASH_FLAGS_COUNT];
    uint32_t flags = PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH | PH_HASH_MHASH |
                     PH_HASH_COLOR_HASH;
    ASSERT_OK(ph_compute_multi(ctx, flags, hashes));
    for (int i = 0; i < PH_HASH_FLAGS_COUNT; i++)
        check_uint64(filename, UINT64_ALGO_NAMES[i], hashes[i], update_out);

    for (size_t i = 0; i < NUM_DIGEST_ALGOS; i++) {
        ph_digest_t digest;
        if (DIGEST_FNS[i](ctx, &digest) == PH_SUCCESS)
            check_digest(filename, DIGEST_ALGO_NAMES[i], &digest, update_out);
    }

    ph_free(ctx);
    printf("  %s: checked\n", filename);
}

int main(int argc, char **argv) {
    int update = (argc > 1 && strcmp(argv[1], "--update") == 0);

    if (update) {
        FILE *out = fopen(golden_path(), "w");
        if (!out) {
            fprintf(stderr, "[FAIL] test_golden_hashes - could not open %s for writing\n",
                    golden_path());
            return 1;
        }
        printf("test_golden_hashes: regenerating %s\n", golden_path());
        for (size_t i = 0; i < NUM_FIXTURES; i++)
            process_fixture(FIXTURES[i], out);
        fclose(out);
        printf("test_golden_hashes: golden file updated\n");
        return 0;
    }

    load_golden();
    printf("test_golden_hashes:\n");
    for (size_t i = 0; i < NUM_FIXTURES; i++)
        process_fixture(FIXTURES[i], NULL);

    if (g_mismatches > 0) {
        fprintf(stderr, "test_golden_hashes: FAILED (%d mismatch(es) out of %d checked)\n",
                g_mismatches, g_checked);
        return 1;
    }
    printf("test_golden_hashes: PASSED (%d entries checked)\n", g_checked);
    return 0;
}
