#include "libphash.h"
#include "test_macros.h"

#include <stdio.h>
#include <string.h>

/* The build-introspection answers are checked against the same PH_USE_* / PH_ENABLE_*
 * macros that selected the code: the tests are compiled with the library's own
 * definitions (the Makefile shares CFLAGS; CMake copies phash's COMPILE_DEFINITIONS), so
 * a mismatch here means the library reports a backend it does not have. The spng build
 * matters most: png=spng there, with no libpng linked at all. */

#if defined(PH_USE_LIBPNG)
#    define EXPECT_PNG "libpng"
#elif defined(PH_USE_SPNG)
#    define EXPECT_PNG "spng"
#else
#    define EXPECT_PNG "stb"
#endif

#if defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
#    define EXPECT_NATIVE_PNG 1
#else
#    define EXPECT_NATIVE_PNG 0
#endif

#if defined(PH_USE_LIBJPEG_TURBO)
#    define EXPECT_JPEG        "libjpeg-turbo"
#    define EXPECT_NATIVE_JPEG 1
#else
#    define EXPECT_JPEG        "stb"
#    define EXPECT_NATIVE_JPEG 0
#endif

#if defined(PH_USE_WEBP)
#    define EXPECT_WEBP        "libwebp"
#    define EXPECT_NATIVE_WEBP 1
#else
#    define EXPECT_WEBP        "none"
#    define EXPECT_NATIVE_WEBP 0
#endif

#if defined(PH_ENABLE_THREADS)
#    define EXPECT_THREADS "on"
#else
#    define EXPECT_THREADS "off"
#endif

#if defined(PH_ENABLE_MOCK_BACKEND)
#    define EXPECT_MOCK "on"
#else
#    define EXPECT_MOCK "off"
#endif

/* Finds `key=` as a whole token and copies its value into `out`. */
static int build_info_value(const char *info, const char *key, char *out, size_t out_size) {
    size_t key_len = strlen(key);
    const char *p = info;
    while (*p) {
        const char *end = strchr(p, ' ');
        size_t token_len = end ? (size_t)(end - p) : strlen(p);
        if (token_len > key_len && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            size_t value_len = token_len - key_len - 1;
            if (value_len + 1 > out_size) {
                return 0;
            }
            memcpy(out, p + key_len + 1, value_len);
            out[value_len] = '\0';
            return 1;
        }
        p += token_len;
        if (*p == ' ') {
            p++;
        }
    }
    return 0;
}

static void expect_value(const char *info, const char *key, const char *expected) {
    char value[64];
    if (!build_info_value(info, key, value, sizeof(value))) {
        fprintf(stderr, "[FAIL] build info has no '%s': \"%s\"\n", key, info);
        exit(1);
    }
    if (strcmp(value, expected) != 0) {
        fprintf(stderr, "[FAIL] build info says %s=%s, the build has %s=%s\n", key, value, key,
                expected);
        exit(1);
    }
}

static void test_capability_checks_match_the_build(void) {
    ASSERT_INT_EQ(EXPECT_NATIVE_JPEG, ph_can_use_jpeg());
    ASSERT_INT_EQ(EXPECT_NATIVE_PNG, ph_can_use_png());
    ASSERT_INT_EQ(EXPECT_NATIVE_WEBP, ph_can_use_webp());
    PASS("test_capability_checks_match_the_build");
}

static void test_build_info_matches_the_build(void) {
    const char *info = ph_get_build_info();
    ASSERT_PTR_NOT_NULL((void *)info);
    printf("  %s\n", info);

    expect_value(info, "version", ph_version());
    expect_value(info, "jpeg", EXPECT_JPEG);
    expect_value(info, "png", EXPECT_PNG);
    expect_value(info, "webp", EXPECT_WEBP);
    expect_value(info, "threads", EXPECT_THREADS);
    expect_value(info, "mock", EXPECT_MOCK);

    /* The keys whose values are not derived from a PH_* macro still have to be there, and
     * one of their documented values. */
    char value[64];
    ASSERT(build_info_value(info, "zlib", value, sizeof(value)));
    ASSERT(strcmp(value, "zlib-ng") == 0 || strcmp(value, "zlib") == 0 ||
           strcmp(value, "none") == 0);
    ASSERT(build_info_value(info, "simd", value, sizeof(value)));
    ASSERT(strcmp(value, "avx2") == 0 || strcmp(value, "sse4.2") == 0 ||
           strcmp(value, "neon") == 0 || strcmp(value, "none") == 0);

    /* One line of single-space-separated key=value tokens: what a log parser relies on. */
    ASSERT(strchr(info, '\n') == NULL);
    ASSERT(strstr(info, "  ") == NULL);
    ASSERT(info[0] != ' ' && info[strlen(info) - 1] != ' ');

    /* Static storage: the same pointer every time. */
    ASSERT(info == ph_get_build_info());
    PASS("test_build_info_matches_the_build");
}

int main(void) {
    test_capability_checks_match_the_build();
    test_build_info_matches_the_build();
    printf("test_build_info: PASSED\n");
    return 0;
}
