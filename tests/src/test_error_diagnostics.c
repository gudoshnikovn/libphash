/* The public error vocabulary, pinned end to end.
 *
 * `ph_error_t` and `ph_get_error_string()` are ABI: FFI bindings map the numbers to
 * their own exception types and show the strings to users. Until now most of the
 * vocabulary was only exercised incidentally -- a code was returned somewhere in a
 * test about something else, and several codes were never produced by any test at
 * all, so a code that silently stopped being reachable, or one added without a
 * string, would not have broken anything.
 *
 * This file is the table that closes that gap, in three parts:
 *
 *   1. Every enumerator has a string, and the strings are distinct, printable and
 *      not the "Unknown error" fallback (test_error_string_covers_every_code).
 *      ph_error_kind() below is a `switch` with no `default:` and with -Wswitch
 *      promoted to an error, so appending an enumerator to the header without
 *      coming back here is a *compile* failure rather than a silent gap.
 *   2. Values that are NOT enumerators -- the retired -2 and everything past the
 *      last code -- must fall through to "Unknown error"
 *      (test_unassigned_values_are_unknown). This is what makes part 1 honest: it
 *      fails the moment a new code gets a string but no table entry.
 *   3. An input -> code -> diagnostic-message table
 *      (test_input_to_error_code_table): each row feeds a real input through the
 *      public API and pins the code it must produce.
 *
 * Three codes have no row in part 3 because nothing a caller can pass reaches them;
 * see the comment on `unreachable_by_design` below.
 */

#include "test_macros.h"
#include <libphash.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
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

/* Own prefix on every fixture this test creates, so it cannot collide with the
 * temporaries of another test running from the same working directory. */
#define TMP_DIR "ph_errdiag_dir"
#define TMP_EMPTY "ph_errdiag_empty.png"
#define TMP_LOCKED "ph_errdiag_locked.png"
#define TMP_MISSING "ph_errdiag_missing.png"

/* ------------------------------------------------------------------ part 1 */

/* Exhaustiveness anchor. No `default:` label, and -Wswitch promoted to an error:
 * a new enumerator in ph_error_t makes this function fail to compile, which is the
 * only mechanism in C that can force the table below to be updated. The return
 * value is not otherwise interesting -- it exists so the switch has to be total. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
#endif
static int ph_error_kind(ph_error_t err) {
    switch (err) {
        case PH_SUCCESS:
            return 0;
        case PH_ERR_ALLOCATION_FAILED:
        case PH_ERR_INVALID_ARGUMENT:
        case PH_ERR_NOT_IMPLEMENTED:
        case PH_ERR_EMPTY_IMAGE:
        case PH_ERR_IMAGE_TOO_LARGE:
        case PH_ERR_UNSUPPORTED_FORMAT:
        case PH_ERR_CORRUPT_DATA:
        case PH_ERR_DECODER_UNAVAILABLE:
        case PH_ERR_IO:
        case PH_ERR_REQUIRES_COLOR:
            return 1;
    }
    return -1; /* not an enumerator */
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

typedef struct {
    ph_error_t code;
    const char *name;
} error_code_entry_t;

/* Every enumerator of ph_error_t, in header order. Kept in sync with the header by
 * ph_error_kind() above (a missing entry is a compile error) and by
 * test_unassigned_values_are_unknown() below (a missing entry whose code does have
 * a string is a test failure). */
static const error_code_entry_t all_error_codes[] = {
    {PH_SUCCESS, "PH_SUCCESS"},
    {PH_ERR_ALLOCATION_FAILED, "PH_ERR_ALLOCATION_FAILED"},
    {PH_ERR_INVALID_ARGUMENT, "PH_ERR_INVALID_ARGUMENT"},
    {PH_ERR_NOT_IMPLEMENTED, "PH_ERR_NOT_IMPLEMENTED"},
    {PH_ERR_EMPTY_IMAGE, "PH_ERR_EMPTY_IMAGE"},
    {PH_ERR_IMAGE_TOO_LARGE, "PH_ERR_IMAGE_TOO_LARGE"},
    {PH_ERR_UNSUPPORTED_FORMAT, "PH_ERR_UNSUPPORTED_FORMAT"},
    {PH_ERR_CORRUPT_DATA, "PH_ERR_CORRUPT_DATA"},
    {PH_ERR_DECODER_UNAVAILABLE, "PH_ERR_DECODER_UNAVAILABLE"},
    {PH_ERR_IO, "PH_ERR_IO"},
    {PH_ERR_REQUIRES_COLOR, "PH_ERR_REQUIRES_COLOR"},
};
#define NUM_ERROR_CODES (sizeof(all_error_codes) / sizeof(*all_error_codes))

#define UNKNOWN_ERROR_STRING "Unknown error"

/* "Non-empty" is too weak a bar for a string a binding puts in front of a user: a
 * stray control byte or a truncated fragment would pass it. Require real printable
 * ASCII with no padding at either end. */
static void assert_message_is_clean(const char *what, const char *msg) {
    ASSERT_PTR_NOT_NULL(msg);
    size_t len = strlen(msg);
    if (len == 0 || len > 200) {
        fprintf(stderr, "[FAIL] %s: implausible message length %zu ('%s')\n", what, len, msg);
        exit(1);
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)msg[i];
        if (c < 0x20 || c > 0x7E) {
            fprintf(stderr, "[FAIL] %s: byte 0x%02X at offset %zu is not printable ASCII ('%s')\n",
                    what, c, i, msg);
            exit(1);
        }
    }
    if (msg[0] == ' ' || msg[len - 1] == ' ') {
        fprintf(stderr, "[FAIL] %s: message has leading/trailing space ('%s')\n", what, msg);
        exit(1);
    }
}

static void test_error_string_covers_every_code(void) {
    for (size_t i = 0; i < NUM_ERROR_CODES; i++) {
        const char *s = ph_get_error_string(all_error_codes[i].code);
        assert_message_is_clean(all_error_codes[i].name, s);
        /* Falling through to the fallback means the code has no case in
         * ph_get_error_string(), which is exactly the gap this test exists for. */
        if (strcmp(s, UNKNOWN_ERROR_STRING) == 0) {
            fprintf(stderr, "[FAIL] %s (%d) has no description of its own\n",
                    all_error_codes[i].name, (int)all_error_codes[i].code);
            exit(1);
        }
        ASSERT_INT_EQ(all_error_codes[i].code == PH_SUCCESS ? 0 : 1,
                      ph_error_kind(all_error_codes[i].code));
        printf("  %-26s %4d -> '%s'\n", all_error_codes[i].name, (int)all_error_codes[i].code, s);
    }

    /* Two codes sharing one description would make them indistinguishable to
     * anyone reading a log, which defeats the point of having separate codes. */
    for (size_t i = 0; i < NUM_ERROR_CODES; i++) {
        for (size_t j = i + 1; j < NUM_ERROR_CODES; j++) {
            const char *a = ph_get_error_string(all_error_codes[i].code);
            const char *b = ph_get_error_string(all_error_codes[j].code);
            if (strcmp(a, b) == 0) {
                fprintf(stderr, "[FAIL] %s and %s share the description '%s'\n",
                        all_error_codes[i].name, all_error_codes[j].name, a);
                exit(1);
            }
        }
    }
}

/* ------------------------------------------------------------------ part 2 */

static int is_assigned_code(int value) {
    for (size_t i = 0; i < NUM_ERROR_CODES; i++) {
        if ((int)all_error_codes[i].code == value)
            return 1;
    }
    return 0;
}

/* Everything that is not an enumerator must land on the fallback string -- including
 * -2, which is retired (it was PH_ERR_DECODE_FAILED) and must never be handed out
 * again, and every value past the last code.
 *
 * This is what keeps the table in part 1 complete: the day someone appends
 * PH_ERR_SOMETHING = -12 with a description, the sweep below sees a real string at
 * a value it expects to be unassigned and fails, which is the reminder to add the
 * row. The sweep also catches the opposite mistake -- a code renumbered into a hole. */
static void test_unassigned_values_are_unknown(void) {
    int checked = 0;
    for (int value = 8; value >= -64; value--) {
        const char *s = ph_get_error_string((ph_error_t)value);
        assert_message_is_clean("ph_get_error_string(out-of-range)", s);
        if (is_assigned_code(value))
            continue;
        if (strcmp(s, UNKNOWN_ERROR_STRING) != 0) {
            fprintf(stderr,
                    "[FAIL] %d is not in the table of known codes, but describes itself as '%s'."
                    " Was an error code added or renumbered? Update all_error_codes[].\n",
                    value, s);
            exit(1);
        }
        checked++;
    }
    ASSERT_INT_EQ(-1, ph_error_kind((ph_error_t)-2)); /* retired, never reissued */
    printf("  %d unassigned values -> '%s'\n", checked, UNKNOWN_ERROR_STRING);
}

/* ph_get_last_error_message(NULL) is documented to return an empty string, not
 * NULL: bindings call strlen()/strcmp() on it without a guard. */
static void test_last_error_message_accepts_null(void) {
    const char *msg = ph_get_last_error_message(NULL);
    ASSERT_PTR_NOT_NULL(msg);
    ASSERT_STR_EQ("", msg);
    printf("  ph_get_last_error_message(NULL) -> \"\"\n");
}

/* ------------------------------------------------------------------ part 3 */

typedef enum {
    /* The failure must come with a diagnostic message explaining it. */
    MSG_REQUIRED,
    /* The code is the contract; whether a message accompanies it depends on which
     * decoder backend the build compiled in. The only rows in this class are the
     * max_pixels rejections: the stb_image path reports "Image exceeds the
     * configured maximum pixel count", while the native backends return the same
     * code with no message at all (src/loaders/jpeg.c:36, png.c:145, webp.c:33 and
     * their siblings set *out_err without calling ph_set_err_msg()). Asserting a
     * message here would make the test pass in a Makefile build and fail in a CMake
     * one, so it is not asserted -- but the divergence is a defect in its own right,
     * not something this test endorses. */
    MSG_BACKEND_DEPENDENT,
} msg_expectation_t;

static void expect(ph_context_t *ctx, const char *what, ph_error_t observed, ph_error_t expected,
                   msg_expectation_t msg_rule) {
    if (observed != expected) {
        fprintf(stderr, "[FAIL] %s: expected %d (%s), got %d (%s)\n", what, (int)expected,
                ph_get_error_string(expected), (int)observed, ph_get_error_string(observed));
        exit(1);
    }
    const char *msg = ph_get_last_error_message(ctx);
    ASSERT_PTR_NOT_NULL(msg);
    if (msg_rule == MSG_REQUIRED)
        assert_message_is_clean(what, msg);
    printf("  %-28s -> %-26s '%s'\n", what, ph_get_error_string(observed), msg);
}

static unsigned char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    ASSERT_PTR_NOT_NULL(f);
    ASSERT_INT_EQ(0, fseek(f, 0, SEEK_END));
    long size = ftell(f);
    ASSERT(size > 0);
    rewind(f);
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    ASSERT_PTR_NOT_NULL(buf);
    ASSERT(fread(buf, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    *out_len = (size_t)size;
    return buf;
}

static void write_bytes(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    ASSERT_PTR_NOT_NULL(f);
    if (len > 0)
        ASSERT(fwrite(data, 1, len, f) == len);
    fclose(f);
}

/* --- PH_ERR_IO: the path itself cannot serve as an image source --- */

static void row_io_errors(ph_context_t *ctx) {
    static const unsigned char png_sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    ph_test_unlink(TMP_MISSING);
    expect(ctx, "missing file", ph_load_from_file(ctx, TMP_MISSING), PH_ERR_IO, MSG_REQUIRED);

    ph_test_rmdir(TMP_DIR);
    ASSERT_INT_EQ(0, ph_test_mkdir(TMP_DIR));
    expect(ctx, "directory", ph_load_from_file(ctx, TMP_DIR), PH_ERR_IO, MSG_REQUIRED);
    ph_test_rmdir(TMP_DIR);

    write_bytes(TMP_EMPTY, NULL, 0);
    expect(ctx, "empty file", ph_load_from_file(ctx, TMP_EMPTY), PH_ERR_IO, MSG_REQUIRED);
    ph_test_unlink(TMP_EMPTY);

    write_bytes(TMP_LOCKED, png_sig, sizeof(png_sig));
#ifdef _WIN32
    /* Windows has no chmod that removes read access; denying share access while the
     * file is held open is what makes another opener fail with EACCES there. */
    int holder = -1;
    if (_sopen_s(&holder, TMP_LOCKED, _O_RDONLY | _O_BINARY, _SH_DENYRD, _S_IREAD) == 0 &&
        holder >= 0) {
        expect(ctx, "unreadable file", ph_load_from_file(ctx, TMP_LOCKED), PH_ERR_IO, MSG_REQUIRED);
        _close(holder);
    } else {
        printf("  %-28s -> SKIPPED (cannot deny read sharing here)\n", "unreadable file");
    }
#else
    if (geteuid() == 0) {
        /* root ignores the permission bits, so the case cannot be produced here. */
        printf("  %-28s -> SKIPPED (running as root)\n", "unreadable file");
    } else {
        ASSERT_INT_EQ(0, chmod(TMP_LOCKED, 0));
        expect(ctx, "unreadable file", ph_load_from_file(ctx, TMP_LOCKED), PH_ERR_IO, MSG_REQUIRED);
        ASSERT_INT_EQ(0, chmod(TMP_LOCKED, 0600));
    }
#endif
    ph_test_unlink(TMP_LOCKED);
}

/* --- PH_ERR_INVALID_ARGUMENT: the call itself is malformed ---
 *
 * These rows are MSG_BACKEND_DEPENDENT for a reason worth writing down: an argument
 * check fires before ph_reset_loaded_image() clears last_error, so after one of them
 * ph_get_last_error_message() still returns whatever the *previous* failing call
 * left there. The message is stale, not absent, and it reads as if it described this
 * call. Nothing here asserts that -- pinning the current behaviour would cement it --
 * but nor can a non-empty message be required, since which text appears depends
 * entirely on what ran before. */

static void row_invalid_arguments(ph_context_t *ctx) {
    uint64_t hash = 0;
    static const unsigned char one_byte[1] = {0};

    expect(ctx, "load_from_file(NULL ctx)", ph_load_from_file(NULL, "x.png"),
           PH_ERR_INVALID_ARGUMENT, MSG_BACKEND_DEPENDENT);
    expect(ctx, "load_from_file(NULL path)", ph_load_from_file(ctx, NULL), PH_ERR_INVALID_ARGUMENT,
           MSG_BACKEND_DEPENDENT);
    expect(ctx, "load_from_memory(NULL buf)", ph_load_from_memory(ctx, NULL, 16),
           PH_ERR_INVALID_ARGUMENT, MSG_BACKEND_DEPENDENT);
    /* A zero-length buffer is rejected by the call, not by a decoder: there is
     * nothing to sniff, so it never becomes a question about an image format. */
    expect(ctx, "load_from_memory(len 0)", ph_load_from_memory(ctx, one_byte, 0),
           PH_ERR_INVALID_ARGUMENT, MSG_BACKEND_DEPENDENT);
    expect(ctx, "compute with no image", ph_compute_ahash(ctx, &hash), PH_ERR_INVALID_ARGUMENT,
           MSG_BACKEND_DEPENDENT);
}

/* --- PH_ERR_UNSUPPORTED_FORMAT / PH_ERR_CORRUPT_DATA: the bytes are the problem ---
 *
 * The distinction is the whole reason both codes exist: "this is not an image I
 * know" versus "this is a JPEG/PNG and its bitstream is broken". Truncating a real
 * fixture is what produces the second one honestly -- the magic bytes and header
 * are genuine, so a backend claims the buffer and then fails inside it. The cut is
 * deliberately far short of the first scanline (128 of 25620 JPEG bytes, 128 of 286
 * PNG bytes): stb_image will happily decode a JPEG truncated later in the entropy
 * stream and report success. */
static void row_bad_bytes(ph_context_t *ctx) {
    unsigned char junk[64];
    memset(junk, 0x5A, sizeof(junk));
    expect(ctx, "junk bytes", ph_load_from_memory(ctx, junk, sizeof(junk)),
           PH_ERR_UNSUPPORTED_FORMAT, MSG_REQUIRED);

    size_t len = 0;
    unsigned char *jpeg = read_file(TEST_DATA_DIR "/photo.jpeg", &len);
    ASSERT(len > 128);
    expect(ctx, "truncated JPEG", ph_load_from_memory(ctx, jpeg, 128), PH_ERR_CORRUPT_DATA,
           MSG_REQUIRED);
    free(jpeg);

    unsigned char *png = read_file(TEST_DATA_DIR "/photo.png", &len);
    ASSERT(len > 128);
    expect(ctx, "truncated PNG", ph_load_from_memory(ctx, png, 128), PH_ERR_CORRUPT_DATA,
           MSG_REQUIRED);
    free(png);

    /* The counter-check for this whole file: the intact fixtures still load, and a
     * successful load clears the message left by the failures above. Without it,
     * "every input is an error" would be a trivially passing table. */
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_STR_EQ("", ph_get_last_error_message(ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.png"));
    ASSERT_STR_EQ("", ph_get_last_error_message(ctx));
    printf("  %-28s -> %s\n", "intact fixtures", "PH_SUCCESS, message cleared");
}

/* --- PH_ERR_IMAGE_TOO_LARGE: the image is real but too big to accept --- */

static void row_image_too_large(ph_context_t *ctx) {
    /* decode_bomb.png declares 100000x100000 in a 65-byte file. It is refused on the
     * declared dimensions in src/loader.c, before any backend sees it, so this row
     * behaves the same in every build -- message included. */
    expect(ctx, "decompression bomb PNG", ph_load_from_file(ctx, TEST_DATA_DIR "/decode_bomb.png"),
           PH_ERR_IMAGE_TOO_LARGE, MSG_REQUIRED);

    /* The configured limit, as opposed to the hard implementation ceiling above. */
    ASSERT_OK(ph_context_set_max_pixels(ctx, 16));
    expect(ctx, "over max_pixels (PNG)", ph_load_from_file(ctx, TEST_DATA_DIR "/photo.png"),
           PH_ERR_IMAGE_TOO_LARGE, MSG_BACKEND_DEPENDENT);
    expect(ctx, "over max_pixels (JPEG)", ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"),
           PH_ERR_IMAGE_TOO_LARGE, MSG_BACKEND_DEPENDENT);

    /* ph_load_from_pixels() bypasses every decoder, so it needs its own row: this is
     * the path that had no bomb protection at all before task L6. */
    static const uint8_t probe = 0;
    expect(ctx, "raw pixels over max_pixels", ph_load_from_pixels(ctx, &probe, 16, 16, 1, 0),
           PH_ERR_IMAGE_TOO_LARGE, MSG_BACKEND_DEPENDENT);
    ASSERT_OK(ph_context_set_max_pixels(ctx, 0));
}

/* --- PH_ERR_DECODER_UNAVAILABLE: recognized format, decoder not compiled in --- */

static void row_decoder_unavailable(ph_context_t *ctx) {
    ph_error_t err = ph_load_from_file(ctx, TEST_DATA_DIR "/photo.webp");
#ifdef PH_USE_WEBP
    /* With the decoder present the very same input must succeed -- which is what
     * proves the row below is about the build configuration and not about the file. */
    expect(ctx, "WebP (PH_USE_WEBP on)", err, PH_SUCCESS, MSG_BACKEND_DEPENDENT);
#else
    /* stb_image has no WebP decoder, so src/loader.c intercepts the magic bytes and
     * says so, instead of letting the fallback report a generic "unknown image type"
     * -- a build-configuration problem must not look like a broken file. */
    expect(ctx, "WebP (no PH_USE_WEBP)", err, PH_ERR_DECODER_UNAVAILABLE, MSG_REQUIRED);
#endif
}

/* --- PH_ERR_REQUIRES_COLOR: a colour algorithm on a single-channel image --- */

static void row_requires_color(ph_context_t *ctx) {
    ph_digest_t digest;
    ASSERT_OK(ph_context_set_load_grayscale(ctx, 1));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.png"));

    /* No message is captured here: last_error carries decoder diagnostics, and this
     * failure happens after a load that succeeded. The code alone is the contract. */
    expect(ctx, "color hash on grayscale", ph_compute_color_hash(ctx, &digest),
           PH_ERR_REQUIRES_COLOR, MSG_BACKEND_DEPENDENT);
    expect(ctx, "color moments on grayscale", ph_compute_color_moments_hash(ctx, &digest),
           PH_ERR_REQUIRES_COLOR, MSG_BACKEND_DEPENDENT);

    /* And the same calls on the same image loaded in colour must succeed, so the row
     * above cannot be satisfied by a colour hash that simply never works. */
    ASSERT_OK(ph_context_set_load_grayscale(ctx, 0));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.png"));
    ASSERT_OK(ph_compute_color_hash(ctx, &digest));
    ASSERT_OK(ph_compute_color_moments_hash(ctx, &digest));
    printf("  %-28s -> %s\n", "color hash on RGB", "PH_SUCCESS");
}

/* Three codes are declared and described but cannot be produced by any input a
 * caller can construct. They are listed here rather than left unmentioned, because
 * "no test reaches it" is a fact about the code, not an oversight in this file:
 *
 *   PH_ERR_ALLOCATION_FAILED -- returned only where malloc() itself fails. Forcing
 *       that needs an allocator shim, which this build has no hook for.
 *   PH_ERR_NOT_IMPLEMENTED   -- src/batch.c returns it in the branch reached when
 *       the thread pool is absent AND more than one thread was requested; the
 *       clamp in ph_resolve_thread_count() makes that combination impossible, and
 *       the source says so on the line itself.
 *   PH_ERR_EMPTY_IMAGE       -- guarded by `is_loaded && width <= 0` in
 *       ph_compute_color_hash()/ph_compute_color_moments_hash(). Every path that
 *       sets is_loaded rejects a non-positive dimension first, so the guard is
 *       defensive only.
 *
 * They still have to describe themselves (part 1 covers that), and if one of them
 * ever becomes reachable it belongs in the table above. */
static const error_code_entry_t unreachable_by_design[] = {
    {PH_ERR_ALLOCATION_FAILED, "PH_ERR_ALLOCATION_FAILED"},
    {PH_ERR_NOT_IMPLEMENTED, "PH_ERR_NOT_IMPLEMENTED"},
    {PH_ERR_EMPTY_IMAGE, "PH_ERR_EMPTY_IMAGE"},
};

static void test_input_to_error_code_table(ph_context_t *ctx) {
    row_io_errors(ctx);
    row_invalid_arguments(ctx);
    row_bad_bytes(ctx);
    row_image_too_large(ctx);
    row_decoder_unavailable(ctx);
    row_requires_color(ctx);

    for (size_t i = 0; i < sizeof(unreachable_by_design) / sizeof(*unreachable_by_design); i++) {
        printf("  %-28s -> %s (no reachable input)\n", "(not exercised)",
               unreachable_by_design[i].name);
    }
}

int main(void) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    printf("test_error_diagnostics:\n");
    printf(" ph_get_error_string() covers every code:\n");
    test_error_string_covers_every_code();
    printf(" values outside the enumeration:\n");
    test_unassigned_values_are_unknown();
    test_last_error_message_accepts_null();
    printf(" input -> error code:\n");
    test_input_to_error_code_table(ctx);

    ph_free(ctx);
    PASS("test_error_diagnostics");
    return 0;
}
