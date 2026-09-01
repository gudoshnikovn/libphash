/* How many times does one ph_load_from_file() open the file?
 *
 * It used to be up to six times for a single load: a probe open() to classify
 * PH_ERR_IO, an fopen() to sniff the WebP magic, an open()+mmap() for the native
 * decoders, stbi_info(path) for the pixel-count pre-check, stbi_load(path) for
 * the decode itself, and finally an fopen() plus a full read of the file into the
 * heap just to scan for an EXIF orientation tag. Besides the pure I/O cost on the
 * hottest path in the library, that meant the bytes that were checked and the
 * bytes that were decoded came from different reads of a path that another
 * process is free to replace in between.
 *
 * The load path opens the file exactly once now, and this test holds it to that
 * number. It counts by defining open() and fopen() in the test binary itself: the
 * static linker resolves the library's calls to the definitions below rather than
 * to libc, which makes the count a property of the library under test and not of
 * a tracing tool that may or may not exist on the machine (dtruss needs to
 * disable SIP on macOS; strace is Linux-only). The real work is delegated to
 * openat(), which nothing here intercepts.
 *
 * Windows has no such interposition, so there the test reports itself skipped
 * rather than pretending to check something. */

#include "test_macros.h"
#include <libphash.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

int main(void) {
    printf("test_file_open_count: SKIPPED (no symbol interposition on Windows)\n");
    return 0;
}

#else

#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>

/* Only opens of the file under test are counted, so an unrelated open anywhere
 * else in the process (the C library's own, the sanitizers') cannot affect the
 * verdict. */
static const char *g_watched_path = NULL;
static int g_open_count = 0;

static void note_open(const char *path) {
    if (g_watched_path && path && strcmp(path, g_watched_path) == 0)
        g_open_count++;
}

int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    note_open(path);
    return openat(AT_FDCWD, path, flags, mode);
}

/* stdio has to be counted too: three of the six openings this test exists to
 * prevent were fopen()s, and an fopen() inside libc would not go through the
 * open() above. */
FILE *fopen(const char *path, const char *mode) {
    note_open(path);

    int flags;
    if (strchr(mode, 'w'))
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (strchr(mode, 'a'))
        flags = O_WRONLY | O_CREAT | O_APPEND;
    else
        flags = O_RDONLY;
    if (strchr(mode, '+'))
        flags = (flags & ~(O_RDONLY | O_WRONLY)) | O_RDWR;

    int fd = openat(AT_FDCWD, path, flags, 0666);
    if (fd < 0)
        return NULL;
    FILE *f = fdopen(fd, mode);
    if (!f)
        close(fd);
    return f;
}

static int count_opens_for_load(ph_context_t *ctx, const char *path, ph_error_t *out_err) {
    g_watched_path = path;
    g_open_count = 0;
    *out_err = ph_load_from_file(ctx, path);
    int n = g_open_count;
    g_watched_path = NULL;
    return n;
}

static void expect_one_open(ph_context_t *ctx, const char *path, ph_error_t expected_err,
                            const char *what) {
    ph_error_t err = PH_SUCCESS;
    int opens = count_opens_for_load(ctx, path, &err);
    if (err != expected_err) {
        fprintf(stderr, "[FAIL] %s: expected %d (%s), got %d (%s)\n", what, (int)expected_err,
                ph_get_error_string(expected_err), (int)err, ph_get_error_string(err));
        exit(1);
    }
    if (opens != 1) {
        fprintf(stderr, "[FAIL] %s: expected exactly 1 open of '%s', counted %d\n", what, path,
                opens);
        exit(1);
    }
    printf("  %-28s -> 1 open, %s\n", what, ph_get_error_string(err));
}

/* The interposition has to actually be in effect; otherwise every count would be
 * zero and the assertions above would be vacuous. */
static void test_interposition_is_live(void) {
    g_watched_path = TEST_DATA_DIR "/photo.jpeg";
    g_open_count = 0;
    FILE *f = fopen(TEST_DATA_DIR "/photo.jpeg", "rb");
    ASSERT_PTR_NOT_NULL(f);
    ASSERT_INT_EQ(1, (int)fread((char[1]){0}, 1, 1, f));
    fclose(f);
    int fd = open(TEST_DATA_DIR "/photo.jpeg", O_RDONLY);
    ASSERT(fd >= 0);
    close(fd);
    ASSERT_INT_EQ(2, g_open_count);
    g_watched_path = NULL;
    printf("  %-28s -> open() and fopen() are intercepted\n", "self-check");
}

int main(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    printf("test_file_open_count:\n");
    test_interposition_is_live();

    /* Both loading modes and both auto-orientation settings: the EXIF scan used
     * to be a separate full re-read of the file, and the grayscale request used
     * to pick a different decoder entry point. */
    expect_one_open(ctx, TEST_DATA_DIR "/photo.jpeg", PH_SUCCESS, "jpeg, defaults");
    expect_one_open(ctx, TEST_DATA_DIR "/photo_complex.png", PH_SUCCESS, "png, defaults");

    ph_context_set_load_grayscale(ctx, 1);
    expect_one_open(ctx, TEST_DATA_DIR "/photo.jpeg", PH_SUCCESS, "jpeg, grayscale");
    ph_context_set_load_grayscale(ctx, 0);

    ph_context_set_auto_orient(ctx, 0);
    expect_one_open(ctx, TEST_DATA_DIR "/photo_rotated_90.jpeg", PH_SUCCESS,
                    "jpeg, no auto-orient");
    ph_context_set_auto_orient(ctx, 1);
    expect_one_open(ctx, TEST_DATA_DIR "/photo_rotated_90.jpeg", PH_SUCCESS, "jpeg, auto-orient");

    /* A failing load must not open the file more than once either -- the WebP
     * magic sniff and the pixel-limit pre-check were both extra openings that
     * only existed on the way to an error. */
    expect_one_open(ctx, TEST_DATA_DIR "/corrupted.jpg", PH_ERR_CORRUPT_DATA, "corrupt file");
    if (!ph_can_use_webp())
        expect_one_open(ctx, TEST_DATA_DIR "/photo.webp", PH_ERR_DECODER_UNAVAILABLE,
                        "webp without a decoder");
    else
        expect_one_open(ctx, TEST_DATA_DIR "/photo.webp", PH_SUCCESS, "webp");

    ph_context_set_max_pixels(ctx, 16);
    expect_one_open(ctx, TEST_DATA_DIR "/photo.jpeg", PH_ERR_IMAGE_TOO_LARGE, "over max_pixels");
    ph_context_set_max_pixels(ctx, 0);

    /* A path that cannot be read at all is still one open: the classification
     * happens on the descriptor that the single open produced. */
    ph_error_t err = PH_SUCCESS;
    int opens = count_opens_for_load(ctx, TEST_DATA_DIR "/no_such_file.png", &err);
    ASSERT_INT_EQ(PH_ERR_IO, err);
    ASSERT_INT_EQ(1, opens);
    printf("  %-28s -> 1 open, %s\n", "missing file", ph_get_error_string(err));

    ph_free(ctx);
    PASS("test_file_open_count");
    return 0;
}

#endif /* !_WIN32 */
