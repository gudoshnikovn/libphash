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

#endif /* PH_BATCH_H */
