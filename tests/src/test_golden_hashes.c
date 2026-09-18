// Task 13: golden-hash regression test. Computes every algorithm's hash for
// every valid fixture in tests/data/ and compares against a committed golden
// file (tests/data/golden_hashes.<backend-set>.txt) -- any unintentional
// change to hash output (e.g. an optimization that subtly changes results)
// shows up as a failing test here, instead of silently shipping.
//
// One golden file used to cover every build. It could not: TurboJPEG and the
// stb_image JPEG fallback round their IDCT differently, so the *same* pixels
// never reach the hash functions in a TurboJPEG build and a stb-only build --
// this is not decoder noise absorbable by a tolerance, it is a different
// input. On darwin-arm64 that alone put 12/72 checks (every mHash/ColorHash/
// ColorMoments entry on a JPEG fixture) outside a tolerance of 2. So instead
// of one file, the golden path is namespaced by the backend set the binary
// was actually built with -- see PH_GOLDEN_BACKEND_SET below, computed from
// the same PH_USE_* macros the loader dispatches on, never set by hand in CI.
// Each backend set gets its own committed file; a build picks its file by
// construction, so switching PHASH_USE_TURBOJPEG/PHASH_USE_LIBPNG/
// PHASH_USE_SPNG/PHASH_USE_WEBP can never compare against the wrong one.
//
// What tolerance is still for, once decoder identity is no longer the
// variable: the *same* decoder can still round its last couple of bits
// differently across CPU architectures (NEON vs. SSE4.2 in resize.c/DCT), and
// two 2.0.0 algorithms quantise a continuous value into a byte -- Radial
// (PH_DIGEST_KIND_COEFFICIENTS) rescales its 40 coefficients by their own
// per-image min/max before quantising to 0..255, so a one-ULP perturbation in
// any single coefficient can shift where every other one lands; ColorMoments
// (PH_DIGEST_KIND_VECTOR16) has a fixed 1/128-per-level scale, so the same
// perturbation moves a bounded, small number of levels. Both get a wider
// per-algorithm tolerance than the generic byte-vector default; see
// GOLDEN_TOLERANCE_LEVELS_FOR() below for the reasoning per algorithm.
//
// Run with --update to regenerate the current build's golden file after a
// verified, intentional change to an algorithm's output. Regenerating one
// backend set's file does not touch the others -- if the change is real (not
// decoder-identity noise), regenerate every backend set you can build
// locally and let CI catch any you can't.
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GOLDEN_TOLERANCE_BITS 2
/* The default allowance for digests whose bytes are numbers rather than bits: two
 * levels of same-decoder, cross-arch rounding noise per byte, not two bits over the
 * whole digest. Algorithms that amplify that noise get their own wider constant
 * below instead of a change here. */
#define GOLDEN_TOLERANCE_LEVELS 2
/* Radial's per-image min/max rescaling (src/hashes/radial.c) turns a one-ULP
 * difference in a single DCT coefficient into a shift of the quantisation range for
 * all 40 -- the generic tolerance above is sized for noise that stays local to one
 * byte, not noise an upstream normalisation step can spread across the whole
 * digest. */
#define GOLDEN_TOLERANCE_LEVELS_RADIAL 8
/* ColorMoments (src/hashes/color_moments.c) quantises at a fixed 1/128-per-level
 * scale with no data-dependent rescaling, so the same cross-arch float noise moves a
 * smaller, bounded number of levels than Radial's -- wider than the generic default,
 * but not as wide as Radial's. */
#define GOLDEN_TOLERANCE_LEVELS_COLOR_MOMENTS 4

static int golden_tolerance_levels(const char *algo) {
    if (strcmp(algo, "Radial") == 0)
        return GOLDEN_TOLERANCE_LEVELS_RADIAL;
    if (strcmp(algo, "ColorMoments") == 0)
        return GOLDEN_TOLERANCE_LEVELS_COLOR_MOMENTS;
    return GOLDEN_TOLERANCE_LEVELS;
}

/* The backend set a build actually decodes with, computed from the same PH_USE_*
 * macros src/loader.c dispatches on -- never set by hand, so it cannot drift out of
 * sync with what the binary was actually built with. */
#if defined(PH_USE_TURBOJPEG)
#define PH_GOLDEN_JPEG_TAG "turbojpeg"
#else
#define PH_GOLDEN_JPEG_TAG "stbjpeg"
#endif

#if defined(PH_USE_LIBPNG)
#define PH_GOLDEN_PNG_TAG "libpng"
#elif defined(PH_USE_SPNG)
#define PH_GOLDEN_PNG_TAG "spng"
#else
#define PH_GOLDEN_PNG_TAG "stbpng"
#endif

#if defined(PH_USE_WEBP)
#define PH_GOLDEN_WEBP_TAG "webp"
#else
#define PH_GOLDEN_WEBP_TAG "nowebp"
#endif

#define PH_GOLDEN_BACKEND_SET PH_GOLDEN_JPEG_TAG "-" PH_GOLDEN_PNG_TAG "-" PH_GOLDEN_WEBP_TAG

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

static const char *UINT64_ALGO_NAMES[PH_HASH_FLAGS_COUNT] = {"aHash", "dHash", "pHash", "wHash"};

typedef ph_error_t (*digest_fn_t)(ph_context_t *, ph_digest_t *);
static const char *DIGEST_ALGO_NAMES[] = {"BMH", "ColorMoments", "Radial", "mHash", "ColorHash"};
static const digest_fn_t DIGEST_FNS[] = {ph_compute_bmh, ph_compute_color_moments_hash,
                                         ph_compute_radial_hash, ph_compute_mhash,
                                         ph_compute_color_hash};
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

static const char *golden_path(void) {
    return TEST_DATA_DIR "/golden_hashes." PH_GOLDEN_BACKEND_SET ".txt";
}

/* The hex field width has to track PH_DIGEST_MAX_BYTES, not sit at a literal that
 * quietly stops matching it: mHash and ColorHash grew past 64 bytes (128 hex chars) in
 * 2.0.0, and a fixed "%128s" here silently truncated their lines mid-digest, which
 * desynced every fscanf() call after it in the file -- not a crash, just wrong data
 * read into unrelated fields. Stringify the same constant the buffer itself is sized
 * from (2 hex chars per byte), so the two cannot drift apart again. */
/* A literal, not `PH_DIGEST_MAX_BYTES * 2`: the preprocessor stringifies tokens, not
 * evaluated arithmetic, so `#(PH_DIGEST_MAX_BYTES * 2)` would paste the expression
 * itself into the format string, not a number. The _Static_assert below is what keeps
 * this literal from drifting out of sync instead. */
#define PH_GOLDEN_HEX_DIGITS 256
_Static_assert(PH_GOLDEN_HEX_DIGITS == PH_DIGEST_MAX_BYTES * 2,
               "PH_GOLDEN_HEX_DIGITS must track PH_DIGEST_MAX_BYTES");
#define PH_GOLDEN_STR2(x) #x
#define PH_GOLDEN_STR(x) PH_GOLDEN_STR2(x)

static void load_golden(void) {
    FILE *f = fopen(golden_path(), "r");
    if (!f) {
        fprintf(stderr, "[FAIL] test_golden_hashes - could not open %s\n", golden_path());
        exit(1);
    }
    while (g_golden_count < (int)(sizeof(g_golden) / sizeof(g_golden[0])) &&
           fscanf(f, "%63s %31s %" PH_GOLDEN_STR(PH_GOLDEN_HEX_DIGITS) "s",
                  g_golden[g_golden_count].filename, g_golden[g_golden_count].algo,
                  g_golden[g_golden_count].hex) == 3) {
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
    g_checked++;
    if (expected.size != value->size) {
        fprintf(stderr, "[FAIL] test_golden_hashes - %s/%s changed size: %d -> %d\n", filename,
                algo, expected.size, value->size);
        g_mismatches++;
        return;
    }
    expected.kind = value->kind;

    /* Only a bit vector has a Hamming distance. For the digests that are quantised
     * numbers -- the radial coefficients, the colour moments -- the analogue of "a couple
     * of bits of decoder noise" is a couple of levels per byte, and asking for a Hamming
     * distance instead gets the comparison refused and -1 returned, which a
     * `dist > tolerance` test reads as "unchanged". That is how this file passed while
     * every radial hash in it was wrong. */
    if (value->kind == (uint8_t)PH_DIGEST_KIND_BITS ||
        value->kind == (uint8_t)PH_DIGEST_KIND_UNSPECIFIED) {
        int dist = ph_hamming_distance_digest(&expected, value);
        if (dist < 0 || dist > GOLDEN_TOLERANCE_BITS) {
            fprintf(stderr,
                    "[FAIL] test_golden_hashes - %s/%s changed: golden=%s actual=%s (dist=%d, "
                    "max %d)\n",
                    filename, algo, expected_hex, hex, dist, GOLDEN_TOLERANCE_BITS);
            g_mismatches++;
        }
        return;
    }

    int worst = 0;
    for (int i = 0; i < value->size; i++) {
        int diff = (int)expected.data[i] - (int)value->data[i];
        if (diff < 0)
            diff = -diff;
        if (diff > worst)
            worst = diff;
    }
    int tolerance = golden_tolerance_levels(algo);
    if (worst > tolerance) {
        fprintf(stderr,
                "[FAIL] test_golden_hashes - %s/%s changed: golden=%s actual=%s (worst byte "
                "differs by %d, max %d)\n",
                filename, algo, expected_hex, hex, worst, tolerance);
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
    uint32_t flags = PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH;
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
    printf("test_golden_hashes (backend set: %s):\n", PH_GOLDEN_BACKEND_SET);
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
