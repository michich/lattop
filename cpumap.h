#ifndef __PERF_CPUMAP_H
#define __PERF_CPUMAP_H

#define MAX_NR_CPUS 256

int read_cpu_map(void);
extern int cpumap[];

#endif /* __PERF_CPUMAP_H */
