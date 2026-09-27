/* What `threads = 0` resolves to: the CPUs this process may use, not the machine's.
 *
 * The parser is checked against every form cgroup's cpu.max takes. The detection itself
 * depends on where the test runs, so it is checked against the platform's own answer:
 * never more than the online count, and on Linux never more than the affinity mask. To
 * see the container case, run this binary with `docker run --cpuset-cpus=0,1` or
 * `--cpus=2`, or under `taskset -c 0,1`, and set PH_EXPECT_CPUS=2. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(__linux__)
#include <sched.h>
#endif
#if !defined(_WIN32)
#include <unistd.h>
#endif

static void test_quota_parser(void) {
    ASSERT_INT_EQ(2, ph_cpu_quota_limit("200000 100000\n"));
    ASSERT_INT_EQ(2, ph_cpu_quota_limit("150000 100000")); /* 1.5 CPUs: round up */
    ASSERT_INT_EQ(1, ph_cpu_quota_limit("50000 100000"));
    ASSERT_INT_EQ(1, ph_cpu_quota_limit("1 100000"));
    ASSERT_INT_EQ(0, ph_cpu_quota_limit("max 100000\n")); /* no quota */
    ASSERT_INT_EQ(0, ph_cpu_quota_limit("-1 100000"));    /* cgroup v1: no quota */
    ASSERT_INT_EQ(0, ph_cpu_quota_limit("200000 0"));
    ASSERT_INT_EQ(0, ph_cpu_quota_limit(""));
    ASSERT_INT_EQ(0, ph_cpu_quota_limit("garbage"));
    ASSERT_INT_EQ(0, ph_cpu_quota_limit(NULL));
    PASS("test_quota_parser");
}

static void test_detection_is_bounded(void) {
    int n = ph_available_cpus();
    ASSERT(n >= 1);
#if !defined(_WIN32)
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online > 0)
        ASSERT(n <= online);
#endif
#if defined(__linux__)
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof(set), &set) == 0)
        ASSERT(n <= CPU_COUNT(&set));
#endif
    const char *expect = getenv("PH_EXPECT_CPUS");
    if (expect && *expect)
        ASSERT_INT_EQ(atoi(expect), n);
    printf("[PASS] test_detection_is_bounded (%d available)\n", n);
}

int main(void) {
    test_quota_parser();
    test_detection_is_bounded();
    return 0;
}
