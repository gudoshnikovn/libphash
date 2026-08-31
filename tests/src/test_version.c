#include "libphash.h"
#include "phash_version.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ph_version() and ph_version_number() must describe the same version,
 * derived from the single source of truth (CMakeLists.txt project() VERSION). */
void test_version_string_and_number_agree() {
    const char *ver = ph_version();
    ASSERT_PTR_NOT_NULL((void *)ver);

    int major = 0, minor = 0, patch = 0;
    int n = sscanf(ver, "%d.%d.%d", &major, &minor, &patch);
    ASSERT_INT_EQ(3, n);

    int expected_number = major * 1000000 + minor * 1000 + patch;
    ASSERT_INT_EQ(expected_number, ph_version_number());
}

/* The header's compile-time macros must agree with the runtime accessors, and
 * the numbering scheme must leave room for a minor/patch above 99 -- the
 * pre-2.0.0 scheme (major*10000 + minor*100 + patch) collided there. */
void test_version_macros_agree_with_runtime() {
    ASSERT_INT_EQ(PH_VERSION_NUMBER, ph_version_number());
    ASSERT_STR_EQ(PH_VERSION_STRING, ph_version());
    ASSERT_INT_EQ(PH_VERSION_MAJOR * 1000000 + PH_VERSION_MINOR * 1000 + PH_VERSION_PATCH,
                  PH_VERSION_NUMBER);
}

/* Distinct versions must map to distinct numbers where the old scheme aliased
 * them: 1.100.0 and 2.0.0 both came out as 20000 before. */
void test_version_number_scheme_has_no_collisions() {
#define PH_TEST_VERSION_NUMBER(maj, min, pat) ((maj) * 1000000 + (min) * 1000 + (pat))
    ASSERT(PH_TEST_VERSION_NUMBER(1, 100, 0) != PH_TEST_VERSION_NUMBER(2, 0, 0));
    ASSERT(PH_TEST_VERSION_NUMBER(2, 0, 100) != PH_TEST_VERSION_NUMBER(2, 1, 0));
    /* Ordering must stay monotonic across a component boundary. */
    ASSERT(PH_TEST_VERSION_NUMBER(2, 0, 999) < PH_TEST_VERSION_NUMBER(2, 1, 0));
    ASSERT(PH_TEST_VERSION_NUMBER(2, 999, 0) < PH_TEST_VERSION_NUMBER(3, 0, 0));
#undef PH_TEST_VERSION_NUMBER
}

void test_version_stable_across_calls() {
    ASSERT_INT_EQ(0, strcmp(ph_version(), ph_version()));
    ASSERT_INT_EQ(ph_version_number(), ph_version_number());
}

int main() {
    test_version_string_and_number_agree();
    test_version_stable_across_calls();
    test_version_macros_agree_with_runtime();
    test_version_number_scheme_has_no_collisions();
    printf("test_version: PASSED\n");
    return 0;
}
