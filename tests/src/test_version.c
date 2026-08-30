#include "libphash.h"
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

    int expected_number = major * 10000 + minor * 100 + patch;
    ASSERT_INT_EQ(expected_number, ph_version_number());
}

void test_version_stable_across_calls() {
    ASSERT_INT_EQ(0, strcmp(ph_version(), ph_version()));
    ASSERT_INT_EQ(ph_version_number(), ph_version_number());
}

int main() {
    test_version_string_and_number_agree();
    test_version_stable_across_calls();
    printf("test_version: PASSED\n");
    return 0;
}
