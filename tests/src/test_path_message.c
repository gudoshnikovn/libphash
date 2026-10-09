/* The message of a failed file load: "<verb> '<path>': <reason>".
 *
 * test_error_diagnostics checks it through ph_load_from_file(), which always writes into a
 * context's buffer of PH_LAST_ERROR_MAX bytes. This test calls ph_format_path_msg() itself,
 * with every buffer size and input the contract in fileio.h covers: no buffer at all, a
 * reason that does not fit its 127 bytes, a reason printf cannot format, a buffer too small
 * for the two ends of the path, and both cuts landing inside a UTF-8 character. Whatever
 * the input, the message is valid UTF-8 if the path and the reason are. */

#include "fileio.h"
#include "test_macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* 0 if s is well-formed UTF-8 as far as sequence lengths go: the question is only whether
 * a cut left a sequence short. */
static int utf8_truncated_sequence(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int need = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : 3;
        p++;
        for (int k = 0; k < need; k++, p++) {
            if ((*p & 0xC0) != 0x80) {
                return 1;
            }
        }
    }
    return 0;
}

static int ends_with(const char *s, const char *tail) {
    size_t m = strlen(s), t = strlen(tail);
    return m >= t && strcmp(s + m - t, tail) == 0;
}

/* A NULL buffer, or one of no bytes, is written nothing and is not touched. */
static void test_no_buffer(void) {
    ph_format_path_msg(NULL, 64, "Cannot open", "/x.png", "%s", "reason");
    char buf[4] = {'a', 'b', 'c', '\0'};
    ph_format_path_msg(buf, 0, "Cannot open", "/x.png", "%s", "reason");
    ASSERT_STR_EQ("abc", buf);
    printf("  no buffer -> nothing written\n");
}

/* A reason past 127 bytes is cut there, on a character boundary, and the message ends
 * with what is left of it. */
static void test_long_reason(void) {
    char reason[400];
    size_t off = 0;
    while (off + 2 < sizeof(reason)) {
        memcpy(reason + off, "\xD1\x84", 2); /* Cyrillic ef, two bytes */
        off += 2;
    }
    reason[off] = '\0';
    for (size_t shift = 0; shift < 2; shift++) {
        char buf[512];
        ph_format_path_msg(buf, sizeof(buf), "Cannot read", "/x.png", "%.*s%s", (int)shift, "a",
                           reason);
        ASSERT(strncmp(buf, "Cannot read '/x.png': ", 22) == 0);
        size_t kept = strlen(buf) - 22;
        ASSERT(kept <= 127 && kept >= 126);
        ASSERT(!utf8_truncated_sequence(buf));
    }
    printf("  a reason past 127 bytes -> cut on a character boundary\n");
}

/* A reason printf cannot format (a wide character the C locale has no byte for) leaves
 * the reason empty; the verb and the path are still there. */
static void test_unformattable_reason(void) {
    char buf[128];
    ph_format_path_msg(buf, sizeof(buf), "Cannot read", "/x.png", "%ls", L"\x4e2d");
    ASSERT(strncmp(buf, "Cannot read '/x.png': ", 22) == 0);
    ASSERT(!utf8_truncated_sequence(buf));
    printf("  an unformattable reason -> the verb and the path, valid UTF-8\n");
}

/* A buffer too small for the two ends of the path holds the plain message cut short,
 * never splitting a character; at every size from one byte up to the one that fits it
 * whole, the result is valid UTF-8 and the start of the full message. */
static void test_small_buffers(void) {
    const char *path = "/\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD/\xF0\x9F\x98\x80.png";
    char full[256];
    ph_format_path_msg(full, sizeof(full), "Cannot open", path, "%s", "No such file");
    for (size_t len = 1; len <= strlen(full) + 1; len++) {
        char buf[256];
        ph_format_path_msg(buf, len, "Cannot open", path, "%s", "No such file");
        ASSERT(strlen(buf) < len);
        ASSERT(!utf8_truncated_sequence(buf));
        if (!strstr(buf, "...")) {
            ASSERT(strncmp(full, buf, strlen(buf)) == 0);
        } else {
            ASSERT(ends_with(buf, "': No such file"));
        }
    }
    printf("  every buffer size -> valid UTF-8, the reason whenever the path is cut\n");
}

/* A path cut in the middle keeps its start and its end around "..." and the reason after
 * them, at every buffer size and with characters of two, three and four bytes, so each
 * cut lands at every offset inside a character. A path whose end is nothing but stray
 * continuation bytes -- not valid UTF-8, and not ours to repair -- has no character to
 * start its end at, and the message keeps only its start. */
static void test_cut_path(void) {
    static const char *const chars[] = {"\xD1\x84", "\xE4\xB8\xAD", "\xF0\x9F\x98\x80"};
    for (size_t c = 0; c < sizeof(chars) / sizeof(chars[0]); c++) {
        char path[200];
        size_t w = strlen(chars[c]), off = 0;
        path[off++] = '/';
        while (off + w + 1 < sizeof(path)) {
            memcpy(path + off, chars[c], w);
            off += w;
        }
        path[off] = '\0';
        for (size_t len = 60; len < 120; len++) {
            char buf[256];
            ph_format_path_msg(buf, len, "Cannot open", path, "%s", "too long");
            ASSERT(strlen(buf) < len);
            ASSERT(strncmp(buf, "Cannot open '/", 14) == 0);
            ASSERT(strstr(buf, "...") != NULL);
            ASSERT(ends_with(buf, "': too long"));
            ASSERT(!utf8_truncated_sequence(buf));
        }
    }

    char stray[80];
    memset(stray, 'd', 40);
    stray[0] = '/';
    memset(stray + 40, 0x84, sizeof(stray) - 41);
    stray[sizeof(stray) - 1] = '\0';
    char buf[64];
    ph_format_path_msg(buf, sizeof(buf), "Cannot open", stray, "%s", "too long");
    ASSERT(strncmp(buf, "Cannot open '/ddd", 17) == 0);
    ASSERT(ends_with(buf, "d...': too long"));
    printf("  a path cut in two -> both ends whole, \"...\" and the reason\n");
}

int main(void) {
    printf("test_path_message:\n");
    test_no_buffer();
    test_long_reason();
    test_unformattable_reason();
    test_small_buffers();
    test_cut_path();
    PASS("test_path_message");
    return 0;
}
