/* PH_ERR_IO must mean the same thing on every platform.
 *
 * ph_load_from_file() decides "this path cannot serve as an image source" before
 * any decoder sees the bytes. That verdict used to exist only on POSIX, so on
 * Windows a missing file surfaced as PH_ERR_UNSUPPORTED_FORMAT/PH_ERR_CORRUPT_DATA
 * -- an error code that means "your image is broken" for a file that was never
 * read at all. Bindings map PH_ERR_IO to their own I/O exception, so the code has
 * to be the same everywhere.
 *
 * The table below is therefore written to be meaningful on Windows too: every
 * fixture is created by the test itself, and the one case with no portable
 * spelling (a file that cannot be read) is produced per platform -- by dropping
 * the read permission on POSIX, and by holding the file open with sharing denied
 * on Windows, which is what makes an open fail with EACCES there. */

#include "test_macros.h"
#include <libphash.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#define ph_test_mkdir(p) _mkdir(p)
#define ph_test_rmdir(p) _rmdir(p)
#define ph_test_unlink(p) _unlink(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define ph_test_mkdir(p) mkdir((p), 0755)
#define ph_test_rmdir(p) rmdir(p)
#define ph_test_unlink(p) unlink(p)
#endif

#define TMP_DIR "ph_io_probe_dir"
#define TMP_EMPTY "ph_io_probe_empty.png"
#define TMP_LOCKED "ph_io_probe_locked.png"
#define TMP_MISSING "ph_io_probe_missing.png"

/* Every entry in the table must yield PH_ERR_IO *and* a diagnostic message: the
 * code says "I could not read it", the message says why. */
static void expect_io(ph_context_t *ctx, const char *path, const char *what) {
    ph_error_t err = ph_load_from_file(ctx, path);
    if (err != PH_ERR_IO) {
        fprintf(stderr, "[FAIL] %s: expected PH_ERR_IO (%d) for %s, got %d (%s)\n", what,
                (int)PH_ERR_IO, path, (int)err, ph_get_error_string(err));
        exit(1);
    }
    ASSERT(strlen(ph_get_last_error_message(ctx)) > 0);
    printf("  %-22s -> PH_ERR_IO, '%s'\n", what, ph_get_last_error_message(ctx));
}

static void write_empty_file(const char *path) {
    FILE *f = fopen(path, "wb");
    ASSERT_PTR_NOT_NULL(f);
    fclose(f);
}

static void write_bytes(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    ASSERT_PTR_NOT_NULL(f);
    ASSERT(fwrite(data, 1, len, f) == len);
    fclose(f);
}

static void test_missing_file(ph_context_t *ctx) {
    ph_test_unlink(TMP_MISSING);
    expect_io(ctx, TMP_MISSING, "missing file");
}

/* A directory opens fine on POSIX -- open(2) succeeds on it -- so without an
 * explicit regular-file check it used to sail past the probe and be reported as
 * an unrecognized image format. */
static void test_directory(ph_context_t *ctx) {
    ph_test_rmdir(TMP_DIR);
    ASSERT_INT_EQ(0, ph_test_mkdir(TMP_DIR));
    expect_io(ctx, TMP_DIR, "directory");
    ph_test_rmdir(TMP_DIR);
}

/* An empty file is an I/O-level fact ("there is nothing to read"), not a claim
 * about an image format that was never present. */
static void test_empty_file(ph_context_t *ctx) {
    write_empty_file(TMP_EMPTY);
    expect_io(ctx, TMP_EMPTY, "empty file");
    ph_test_unlink(TMP_EMPTY);
}

static void test_unreadable_file(ph_context_t *ctx) {
    static const unsigned char png_sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    write_bytes(TMP_LOCKED, png_sig, sizeof(png_sig));

#ifdef _WIN32
    /* Windows has no "remove the read permission" chmod: _chmod only toggles the
     * read-only attribute, which does not stop a reader. Denying share access
     * while the file is open is the equivalent situation -- another opener gets
     * EACCES, which is exactly the case PH_ERR_IO has to cover. */
    int holder = -1;
    if (_sopen_s(&holder, TMP_LOCKED, _O_RDONLY | _O_BINARY, _SH_DENYRD, _S_IREAD) != 0 ||
        holder < 0) {
        /* Nothing to assert if the filesystem under the test refuses to lock. */
        printf("  %-22s -> SKIPPED (cannot deny read sharing here)\n", "unreadable file");
    } else {
        expect_io(ctx, TMP_LOCKED, "unreadable file");
        _close(holder);
    }
#else
    if (geteuid() == 0) {
        /* root ignores the permission bits, so the case is not reproducible here
         * (containers and CI images commonly run as root). Say so rather than
         * asserting something the environment cannot produce. */
        printf("  %-22s -> SKIPPED (running as root)\n", "unreadable file");
    } else {
        ASSERT_INT_EQ(0, chmod(TMP_LOCKED, 0));
        expect_io(ctx, TMP_LOCKED, "unreadable file");
        ASSERT_INT_EQ(0, chmod(TMP_LOCKED, 0600));
    }
#endif
    ph_test_unlink(TMP_LOCKED);
}

/* The counter-check: the probe must reject only what it cannot read. A readable
 * fixture still loads, and a readable-but-broken one is still reported as corrupt
 * data -- otherwise "PH_ERR_IO everywhere" would be a trivially passing table. */
static void test_readable_files_are_not_io_errors(ph_context_t *ctx) {
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ph_error_t err = ph_load_from_file(ctx, TEST_DATA_DIR "/corrupted.jpg");
    ASSERT_INT_EQ(PH_ERR_CORRUPT_DATA, err);
    printf("  %-22s -> PH_SUCCESS / PH_ERR_CORRUPT_DATA\n", "readable files");
}

int main(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    printf("test_file_io_errors:\n");
    test_missing_file(ctx);
    test_directory(ctx);
    test_empty_file(ctx);
    test_unreadable_file(ctx);
    test_readable_files_are_not_io_errors(ctx);

    ph_free(ctx);
    PASS("test_file_io_errors");
    return 0;
}
