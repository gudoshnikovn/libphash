/* What `threads = 0` resolves to: the CPUs this process may use, not the machine's.
 *
 * The parser is checked against every form cgroup's cpu.max takes. On Linux the cgroup
 * reader and the narrowing run against fake cgroup trees in a temporary directory (v2,
 * v1, none) and against an affinity mask the test narrows itself. The detection at the
 * real mount point depends on where the test runs, so it is checked against the
 * platform's own answer: never more than the online count, and on Linux never more than
 * the affinity mask. To see the container case, run this binary with
 * `docker run --cpuset-cpus=0,1` or `--cpus=2`, or under `taskset -c 0,1`, and set
 * PH_EXPECT_CPUS=2. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#    define _GNU_SOURCE
#endif
#include "batch.h"
#include "libphash.h"
#include "test_macros.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#    include <sched.h>
#    include <sys/stat.h>
#endif
#if !defined(_WIN32)
#    include <unistd.h>
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
    ASSERT_INT_EQ(INT_MAX, ph_cpu_quota_limit("9223372036854775807 1"));
    ASSERT_INT_EQ(INT_MAX, ph_cpu_quota_limit("9223372036854775807 100000"));
    PASS("test_quota_parser");
}

#if defined(__linux__)
static void write_file(const char *dir, const char *name, const char *text) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    ASSERT_PTR_NOT_NULL(f);
    ASSERT(fputs(text, f) >= 0);
    ASSERT(fclose(f) == 0);
}

static void remove_file(const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    remove(path);
}

static void test_cgroup_reader(void) {
    char root[] = "/tmp/ph_cgroupXXXXXX";
    ASSERT_PTR_NOT_NULL(mkdtemp(root));
    char v1[sizeof(root) + 4];
    snprintf(v1, sizeof(v1), "%s/cpu", root);
    ASSERT(mkdir(v1, 0700) == 0);

    ASSERT_INT_EQ(0, ph_cgroup_cpu_limit(root)); /* no cgroup files */

    /* cgroup v1: both files, or nothing. */
    write_file(v1, "cpu.cfs_quota_us", "250000\n");
    ASSERT_INT_EQ(0, ph_cgroup_cpu_limit(root)); /* quota without a period */
    write_file(v1, "cpu.cfs_period_us", "100000\n");
    ASSERT_INT_EQ(3, ph_cgroup_cpu_limit(root));
    write_file(v1, "cpu.cfs_quota_us", "-1\n");
    ASSERT_INT_EQ(0, ph_cgroup_cpu_limit(root)); /* v1's "no quota" */

    /* cgroup v2's cpu.max wins over v1 files beside it. */
    write_file(root, "cpu.max", "150000 100000\n");
    ASSERT_INT_EQ(2, ph_cgroup_cpu_limit(root));
    write_file(root, "cpu.max", "max 100000\n");
    ASSERT_INT_EQ(0, ph_cgroup_cpu_limit(root));
    write_file(root, "cpu.max", ""); /* empty: falls through to v1 */
    ASSERT_INT_EQ(0, ph_cgroup_cpu_limit(root));

    /* A root too long for a path is no cgroup. */
    char longroot[600];
    memset(longroot, 'a', sizeof(longroot) - 1);
    longroot[sizeof(longroot) - 1] = '\0';
    ASSERT_INT_EQ(0, ph_cgroup_cpu_limit(longroot));

    remove_file(v1, "cpu.cfs_quota_us");
    remove_file(v1, "cpu.cfs_period_us");
    remove_file(root, "cpu.max");
    remove(v1);
    remove(root);
    PASS("test_cgroup_reader");
}

/* The quota and the affinity mask each narrow the count; neither widens it. */
static void test_narrowing(void) {
    char root[] = "/tmp/ph_cgroupXXXXXX";
    ASSERT_PTR_NOT_NULL(mkdtemp(root));
    cpu_set_t saved;
    ASSERT(sched_getaffinity(0, sizeof(saved), &saved) == 0);
    const int allowed = CPU_COUNT(&saved);

    ASSERT_INT_EQ(allowed, ph_available_cpus_at(root)); /* no quota */
    write_file(root, "cpu.max", "100000000 100000\n");  /* 1000 CPUs: above the mask */
    ASSERT_INT_EQ(allowed, ph_available_cpus_at(root));
    write_file(root, "cpu.max", "100000 100000\n"); /* 1 CPU */
    ASSERT_INT_EQ(1, ph_available_cpus_at(root));
    remove_file(root, "cpu.max");

    if (allowed > 1) {
        cpu_set_t one;
        CPU_ZERO(&one);
        for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
            if (CPU_ISSET(cpu, &saved)) {
                CPU_SET(cpu, &one);
                break;
            }
        }
        ASSERT(sched_setaffinity(0, sizeof(one), &one) == 0);
        ASSERT_INT_EQ(1, ph_available_cpus_at(root));
        ASSERT(sched_setaffinity(0, sizeof(saved), &saved) == 0);
    } else {
        printf("[SKIP] affinity narrowing: this process may use one CPU only\n");
    }
    remove(root);
    PASS("test_narrowing");
}
#endif

static void test_detection_is_bounded(void) {
    int n = ph_available_cpus();
    ASSERT(n >= 1);
#if !defined(_WIN32)
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online > 0) {
        ASSERT(n <= online);
    }
#endif
#if defined(__linux__)
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        ASSERT(n <= CPU_COUNT(&set));
    }
#endif
    const char *expect = getenv("PH_EXPECT_CPUS");
    if (expect && *expect) {
        ASSERT_INT_EQ(atoi(expect), n);
    }
    printf("[PASS] test_detection_is_bounded (%d available)\n", n);
}

int main(void) {
    test_quota_parser();
#if defined(__linux__)
    test_cgroup_reader();
    test_narrowing();
#endif
    test_detection_is_bounded();
    return 0;
}
