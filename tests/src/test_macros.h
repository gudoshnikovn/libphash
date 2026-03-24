#ifndef TEST_MACROS_H
#define TEST_MACROS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Simple assertion macros for testing */

#define ASSERT_OK(expr)                                                                            \
    do {                                                                                           \
        ph_error_t _err = (expr);                                                                  \
        if (_err != PH_SUCCESS) {                                                                  \
            fprintf(stderr, "[FAIL] %s:%d - Expression '%s' failed with error %d\n", __FILE__,     \
                    __LINE__, #expr, _err);                                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT(expr)                                                                               \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "[FAIL] %s:%d - Assertion '%s' failed\n", __FILE__, __LINE__, #expr);  \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT_INT_EQ(expected, actual)                                                            \
    do {                                                                                           \
        int _e = (expected);                                                                       \
        int _a = (actual);                                                                         \
        if (_e != _a) {                                                                            \
            fprintf(stderr, "[FAIL] %s:%d - Expected %d, got %d\n", __FILE__, __LINE__, _e, _a);   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT_PTR_NOT_NULL(ptr)                                                                   \
    do {                                                                                           \
        if ((ptr) == NULL) {                                                                       \
            fprintf(stderr, "[FAIL] %s:%d - Pointer '%s' is NULL\n", __FILE__, __LINE__, #ptr);    \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT_UINT8_EQ(expected, actual)                                                          \
    do {                                                                                           \
        uint8_t _e = (uint8_t)(expected);                                                          \
        uint8_t _a = (uint8_t)(actual);                                                            \
        if (_e != _a) {                                                                            \
            fprintf(stderr, "[FAIL] %s:%d - Expected %u, got %u\n", __FILE__, __LINE__,            \
                    (unsigned)_e, (unsigned)_a);                                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT_UINT64_EQ(expected, actual)                                                         \
    do {                                                                                           \
        uint64_t _e = (uint64_t)(expected);                                                        \
        uint64_t _a = (uint64_t)(actual);                                                          \
        if (_e != _a) {                                                                            \
            fprintf(stderr, "[FAIL] %s:%d - Expected 0x%016llx, got 0x%016llx\n", __FILE__,        \
                    __LINE__, (unsigned long long)_e, (unsigned long long)_a);                     \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT_FLOAT_EQ(expected, actual, tol)                                                     \
    do {                                                                                           \
        double _e = (double)(expected);                                                            \
        double _a = (double)(actual);                                                              \
        double _t = (double)(tol);                                                                 \
        if (fabs(_e - _a) > _t) {                                                                  \
            fprintf(stderr, "[FAIL] %s:%d - Expected %.6f, got %.6f (tol %.6f)\n", __FILE__,       \
                    __LINE__, _e, _a, _t);                                                         \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define PASS(name) printf("[PASS] %s\n", (name))

#endif /* TEST_MACROS_H */
