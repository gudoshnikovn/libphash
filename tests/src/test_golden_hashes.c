// Golden-hash regression test. Computes every algorithm's hash for every valid fixture in
// tests/data/ and compares it, exactly, with a committed golden file. Any change to hash
// output -- an optimisation that moves a rounding, a refactor that changes an order of
// operations -- shows up here as a failing test instead of shipping.
//
// One file per JPEG decoder: tests/data/golden_hashes.<jpeg>.txt, <jpeg> being
// "libjpegturbo" or "stbjpeg". The two round their IDCT differently, so the same JPEG
// reaches the hash functions as different pixels; nothing else in the build changes a
// value. Measured across macOS and Linux on arm64 and x86-64, clang and gcc, libpng
// and stb_image for PNG: the files of one JPEG decoder are byte for byte identical. That
// holds because the library computes the same way everywhere -- -ffp-contract=off, one
// plain loop for pHash's DCT, integer area averaging and gray conversion, exact histogram
// intersection -- so there is no tolerance either: a difference of one bit or one level
// is a regression, or a platform that has stopped computing the same, and either is worth
// a failing test.
//
// The files carry the WebP fixtures too. A build without a WebP decoder skips them and
// does not count their entries; its --update keeps their lines from the existing file.
//
// Run with --update to regenerate the current build's file after a verified, intentional
// change to an algorithm's output, and regenerate the other JPEG decoder's file too.
#include "libphash.h"
#include "test_macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PH_USE_LIBJPEG_TURBO)
#    define PH_GOLDEN_JPEG_TAG "libjpegturbo"
#else
#    define PH_GOLDEN_JPEG_TAG "stbjpeg"
#endif

/* photo.png (a uniform colour) and photo_complex.png (55 of its 63 AC coefficients within
 * 0.1% of the AC range of their median) pin pHash where its median threshold is decided by
 * rounding, not where a typical photograph is; docs/algorithm-provenance.md section 3 has
 * the corpus numbers. The JPEG fixtures are the typical case. */
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
    return TEST_DATA_DIR "/golden_hashes." PH_GOLDEN_JPEG_TAG ".txt";
}

/* Fixtures this build cannot decode (WebP without a WebP decoder): their entries are not
 * expected to be checked, and --update carries them over unchanged. */
static int fixture_skipped[NUM_FIXTURES];

/* The hex field width has to track PH_DIGEST_MAX_BYTES: a shorter literal would
 * truncate long digests (mHash, ColorHash) mid-line and desynchronise every fscanf()
 * after it in the file -- not a crash, just wrong data read into unrelated fields. */
/* A literal, not `PH_DIGEST_MAX_BYTES * 2`: the preprocessor stringifies tokens, not
 * evaluated arithmetic, so `#(PH_DIGEST_MAX_BYTES * 2)` would paste the expression
 * itself into the format string, not a number. The _Static_assert below keeps this
 * literal in sync with PH_DIGEST_MAX_BYTES (2 hex chars per byte). */
#define PH_GOLDEN_HEX_DIGITS 256
_Static_assert(PH_GOLDEN_HEX_DIGITS == PH_DIGEST_MAX_BYTES * 2,
               "PH_GOLDEN_HEX_DIGITS must track PH_DIGEST_MAX_BYTES");
#define PH_GOLDEN_STR2(x) #x
#define PH_GOLDEN_STR(x)  PH_GOLDEN_STR2(x)

/* A missing file is only an error when comparing: --update creates it. */
static void load_golden(int required) {
    FILE *f = fopen(golden_path(), "r");
    if (!f) {
        if (!required) {
            return;
        }
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
        if (strcmp(g_golden[i].filename, filename) == 0 && strcmp(g_golden[i].algo, algo) == 0) {
            return g_golden[i].hex;
        }
    }
    return NULL;
}

/* One entry, compared exactly; in --update mode, written out instead. */
static void check_entry(const char *filename, const char *algo, const char *hex, FILE *update_out) {
    if (update_out) {
        fprintf(update_out, "%s %s %s\n", filename, algo, hex);
        return;
    }
    const char *expected = find_golden(filename, algo);
    if (!expected) {
        fprintf(stderr,
                "[FAIL] test_golden_hashes - no golden entry for %s/%s (run with --update "
                "after verifying this is intentional)\n",
                filename, algo);
        g_mismatches++;
        return;
    }
    g_checked++;
    if (strcmp(expected, hex) != 0) {
        fprintf(stderr, "[FAIL] test_golden_hashes - %s/%s changed: golden=%s actual=%s\n",
                filename, algo, expected, hex);
        g_mismatches++;
    }
}

static void process_fixture(size_t index, FILE *update_out) {
    const char *filename = FIXTURES[index];
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", TEST_DATA_DIR, filename);

    ph_context_t *ctx;
    ASSERT_OK(ph_create(&ctx));
    ph_error_t err = ph_load_from_file(ctx, path);
    if (err == PH_ERR_DECODER_UNAVAILABLE) {
        printf("  %s: SKIPPED (%s)\n", filename, ph_get_error_string(err));
        fixture_skipped[index] = 1;
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
    for (int i = 0; i < PH_HASH_FLAGS_COUNT; i++) {
        char hex[17];
        ASSERT_OK(ph_hash_to_hex(hashes[i], hex, sizeof(hex)));
        check_entry(filename, UINT64_ALGO_NAMES[i], hex, update_out);
    }

    /* Every algorithm succeeds on every fixture. A failure is a regression like a changed
     * value -- and in --update it must not leave a hole in the regenerated file. The files
     * store a digest's bytes without the "<kind>:" prefix of the public text form: the kind
     * comes from the algorithm, and test_digest_helpers checks it. */
    for (size_t i = 0; i < NUM_DIGEST_ALGOS; i++) {
        ph_digest_t digest;
        ph_error_t derr = DIGEST_FNS[i](ctx, &digest);
        if (derr != PH_SUCCESS) {
            fprintf(stderr, "[FAIL] test_golden_hashes - %s/%s failed: %s\n", filename,
                    DIGEST_ALGO_NAMES[i], ph_get_error_string(derr));
            g_mismatches++;
            continue;
        }
        char text[PH_DIGEST_HEX_BUFFER_SIZE];
        ASSERT_OK(ph_digest_to_hex(&digest, text, sizeof(text)));
        check_entry(filename, DIGEST_ALGO_NAMES[i], strchr(text, ':') + 1, update_out);
    }

    ph_free(ctx);
    printf("  %s: checked\n", filename);
}

static int is_skipped(const char *filename) {
    for (size_t i = 0; i < NUM_FIXTURES; i++) {
        if (fixture_skipped[i] && strcmp(FIXTURES[i], filename) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    int update = (argc > 1 && strcmp(argv[1], "--update") == 0);
    load_golden(!update);
    printf("test_golden_hashes (%s):\n", golden_path());

    if (update) {
        /* Written next to the golden file and renamed over it only when every fixture and
         * every algorithm succeeded: a failed regeneration leaves the old file intact. */
        char tmp_path[512];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", golden_path());
        FILE *out = fopen(tmp_path, "w");
        if (!out) {
            fprintf(stderr, "[FAIL] test_golden_hashes - could not open %s for writing\n",
                    tmp_path);
            return 1;
        }
        for (size_t i = 0; i < NUM_FIXTURES; i++) {
            process_fixture(i, out);
        }
        /* Fixtures this build cannot decode keep their existing lines. */
        for (int i = 0; i < g_golden_count; i++) {
            if (is_skipped(g_golden[i].filename)) {
                fprintf(out, "%s %s %s\n", g_golden[i].filename, g_golden[i].algo, g_golden[i].hex);
            }
        }
        fclose(out);
        if (g_mismatches > 0 || rename(tmp_path, golden_path()) != 0) {
            remove(tmp_path);
            fprintf(stderr, "test_golden_hashes: golden file NOT updated\n");
            return 1;
        }
        printf("test_golden_hashes: golden file updated\n");
        return 0;
    }

    for (size_t i = 0; i < NUM_FIXTURES; i++) {
        process_fixture(i, NULL);
    }

    /* Every entry of the golden file for a fixture this build decodes must have been
     * checked: a fixture or an algorithm that silently stopped being compared would
     * otherwise pass. */
    int expected = 0;
    for (int i = 0; i < g_golden_count; i++) {
        expected += !is_skipped(g_golden[i].filename);
    }
    if (g_checked != expected) {
        fprintf(stderr, "[FAIL] test_golden_hashes - %d of %d golden entries checked\n", g_checked,
                expected);
        g_mismatches++;
    }
    if (g_mismatches > 0) {
        fprintf(stderr, "test_golden_hashes: FAILED (%d mismatch(es) out of %d checked)\n",
                g_mismatches, g_checked);
        return 1;
    }
    printf("test_golden_hashes: PASSED (%d entries checked)\n", g_checked);
    return 0;
}
