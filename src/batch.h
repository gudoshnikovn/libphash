#ifndef PH_BATCH_H
#define PH_BATCH_H

/* CPU detection behind the batch API's `threads = 0`. */

/* How many CPUs this process may use (src/batch.c): the online count, narrowed by the
 * affinity mask (Linux, Windows) and by a cgroup CPU quota (Linux). Always >= 1. This is
 * what `threads = 0` means in the batch API. */
int ph_available_cpus(void);

/* ceil(quota / period) from a "<quota> <period>" string in the form of cgroup v2's
 * cpu.max; 0 for "max ..." (no quota) or anything unparseable. */
int ph_cpu_quota_limit(const char *cpu_max);

#if defined(__linux__)
/* The CPU quota of the cgroup mounted at `root` (cgroup v2's cpu.max, else v1's
 * cpu/cpu.cfs_quota_us and cpu/cpu.cfs_period_us); 0 when there is none or it cannot be
 * read. */
int ph_cgroup_cpu_limit(const char *root);

/* ph_available_cpus() with the cgroup mounted at `cgroup_root`; ph_available_cpus()
 * passes /sys/fs/cgroup. */
int ph_available_cpus_at(const char *cgroup_root);
#endif

#endif /* PH_BATCH_H */
