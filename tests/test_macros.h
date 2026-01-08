#ifndef TEST_MACROS_H
#define TEST_MACROS_H

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

#endif /* TEST_MACROS_H */
