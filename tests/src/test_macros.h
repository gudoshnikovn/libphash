#ifndef PH_TEST_MACROS_H
#define PH_TEST_MACROS_H

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same reason as src/hashes/hashes.h's copy: M_PI is POSIX, not ISO, and the tests build
 * under the same strict -std=c17 as the library. Defined here rather than in each
 * test that needs it, and separately from src/hashes/hashes.h because test_dct.c is a
 * standalone reimplementation that deliberately includes no library header. */
#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

/* Portable attribute shims: MSVC understands neither __attribute__((unused))
 * nor __attribute__((format(printf, ...))), and has no direct equivalent, so
 * these compile away to nothing there instead of failing the build. */
#if defined(__GNUC__) || defined(__clang__)
#    define PH_TEST_UNUSED __attribute__((unused))
#    define PH_TEST_PRINTF_FORMAT(fmt_idx, arg_idx) \
        __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#    define PH_TEST_UNUSED
#    define PH_TEST_PRINTF_FORMAT(fmt_idx, arg_idx)
#endif

/* Portable popcount: MSVC has no __builtin_popcount. __popcnt requires SSE4.2/POPCNT
 * to be guaranteed present, which isn't assumed for this test-only helper, so a plain
 * bit-counting loop is used there instead of __popcnt. */
#if defined(__GNUC__) || defined(__clang__)
#    define PH_TEST_POPCOUNT(x) __builtin_popcount((unsigned int)(x))
#else
static __inline int ph_test_popcount(unsigned int x) {
    int count = 0;
    while (x) {
        x &= (x - 1);
        count++;
    }
    return count;
}

#    define PH_TEST_POPCOUNT(x) ph_test_popcount((unsigned int)(x))
#endif

/* Deterministic xorshift32 generator. Tests that need pseudo-random input use this
 * instead of rand(): the C library's sequence differs between glibc, Apple libc and
 * MSVC, so a fixed srand() seed would still feed each platform different data, and a
 * data-dependent failure would reproduce on only one of them. */
typedef struct {
    uint32_t state;
} ph_test_rng_t;

static inline ph_test_rng_t ph_test_rng(uint32_t seed) {
    ph_test_rng_t rng = {seed ? seed : 1u}; /* xorshift has a fixed point at 0 */
    return rng;
}

static inline uint32_t ph_test_rng_next(ph_test_rng_t *rng) {
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

static inline uint8_t ph_test_rng_byte(ph_test_rng_t *rng) {
    return (uint8_t)ph_test_rng_next(rng);
}

/* Assertion macros.
 *
 * A failure message has to say what was compared, what came out and on which input:
 * the log is often all there is to go on (a CI leg on a platform nobody has locally).
 * The fixed-shape macros print the expression or both values; when the input matters
 * -- a loop over fixtures, algorithms or parameters -- use ASSERT_MSG and name it. */

PH_TEST_PRINTF_FORMAT(4, 5)

static inline void ph_test_fail(const char *file, int line, const char *expr, const char *fmt,
                                ...) {
    va_list ap;
    fprintf(stderr, "[FAIL] %s:%d - Assertion '%s' failed: ", file, line, expr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/* ASSERT_MSG(expr, fmt, ...): the format string is checked by the compiler. */
#define ASSERT_MSG(expr, ...)                                     \
    do {                                                          \
        if (!(expr)) {                                            \
            ph_test_fail(__FILE__, __LINE__, #expr, __VA_ARGS__); \
        }                                                         \
    } while (0)

#define ASSERT_OK(expr)                                                                        \
    do {                                                                                       \
        ph_error_t _err = (expr);                                                              \
        if (_err != PH_SUCCESS) {                                                              \
            fprintf(stderr, "[FAIL] %s:%d - Expression '%s' failed with error %d\n", __FILE__, \
                    __LINE__, #expr, _err);                                                    \
            exit(1);                                                                           \
        }                                                                                      \
    } while (0)

#define ASSERT(expr)                                                                              \
    do {                                                                                          \
        if (!(expr)) {                                                                            \
            fprintf(stderr, "[FAIL] %s:%d - Assertion '%s' failed\n", __FILE__, __LINE__, #expr); \
            exit(1);                                                                              \
        }                                                                                         \
    } while (0)

#define ASSERT_INT_EQ(expected, actual)                                                          \
    do {                                                                                         \
        int _e = (expected);                                                                     \
        int _a = (actual);                                                                       \
        if (_e != _a) {                                                                          \
            fprintf(stderr, "[FAIL] %s:%d - Expected %d, got %d\n", __FILE__, __LINE__, _e, _a); \
            exit(1);                                                                             \
        }                                                                                        \
    } while (0)

#define ASSERT_PTR_NOT_NULL(ptr)                                                                \
    do {                                                                                        \
        if ((ptr) == NULL) {                                                                    \
            fprintf(stderr, "[FAIL] %s:%d - Pointer '%s' is NULL\n", __FILE__, __LINE__, #ptr); \
            exit(1);                                                                            \
        }                                                                                       \
    } while (0)

#define ASSERT_PTR_NULL(ptr)                                                                   \
    do {                                                                                       \
        if ((ptr) != NULL) {                                                                   \
            fprintf(stderr, "[FAIL] %s:%d - Expected pointer '%s' to be NULL, but it's not\n", \
                    __FILE__, __LINE__, #ptr);                                                 \
            exit(1);                                                                           \
        }                                                                                      \
    } while (0)

#define ASSERT_UINT8_EQ(expected, actual)                                               \
    do {                                                                                \
        uint8_t _e = (uint8_t)(expected);                                               \
        uint8_t _a = (uint8_t)(actual);                                                 \
        if (_e != _a) {                                                                 \
            fprintf(stderr, "[FAIL] %s:%d - Expected %u, got %u\n", __FILE__, __LINE__, \
                    (unsigned)_e, (unsigned)_a);                                        \
            exit(1);                                                                    \
        }                                                                               \
    } while (0)

#define ASSERT_UINT64_EQ(expected, actual)                                                  \
    do {                                                                                    \
        uint64_t _e = (uint64_t)(expected);                                                 \
        uint64_t _a = (uint64_t)(actual);                                                   \
        if (_e != _a) {                                                                     \
            fprintf(stderr, "[FAIL] %s:%d - Expected 0x%016llx, got 0x%016llx\n", __FILE__, \
                    __LINE__, (unsigned long long)_e, (unsigned long long)_a);              \
            exit(1);                                                                        \
        }                                                                                   \
    } while (0)

#define ASSERT_FLOAT_EQ(expected, actual, tol)                                               \
    do {                                                                                     \
        double _e = (double)(expected);                                                      \
        double _a = (double)(actual);                                                        \
        double _t = (double)(tol);                                                           \
        if (fabs(_e - _a) > _t) {                                                            \
            fprintf(stderr, "[FAIL] %s:%d - Expected %.6f, got %.6f (tol %.6f)\n", __FILE__, \
                    __LINE__, _e, _a, _t);                                                   \
            exit(1);                                                                         \
        }                                                                                    \
    } while (0)

#define ASSERT_STR_EQ(expected, actual)                                                         \
    do {                                                                                        \
        const char *_e = (expected);                                                            \
        const char *_a = (actual);                                                              \
        if (strcmp(_e, _a) != 0) {                                                              \
            fprintf(stderr, "[FAIL] %s:%d - Expected '%s', got '%s'\n", __FILE__, __LINE__, _e, \
                    _a);                                                                        \
            exit(1);                                                                            \
        }                                                                                       \
    } while (0)

#define PASS(name) printf("[PASS] %s\n", (name))

#endif /* PH_TEST_MACROS_H */
