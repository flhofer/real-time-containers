#ifndef __KERNUTIL_H_
	#define __KERNUTIL_H_

#include <stdio.h>
	#include <numa.h>			// NUMA node identification

	enum kernelversion {
		KV_NOT_SUPPORTED,
		KV_314,	// includes EDF basic
		KV_40,	// no EDF without PRT_patch?
		KV_413,	// includes full EDF for the first time
		KV_416,	// includes full EDF with GRUB-PA for ARM
		KV_50	// latest releases, now 5.1 (May 8th '19)
	};

	// MSR 
	int open_msr_file(int cpu);
	int get_smi_counter(int fd, unsigned long *counter);
	int has_smi_counter(void);

	// Kernel detection and values
	int check_kernel(void);
	int setkernvar(const char *prefix, const char *name, char *value, int dryrun);
	int getkernvar(const char *prefix, const char *name, char *value, int size);

	// affinity and CPU bit-masks
	int parse_bitmask(struct bitmask *mask, char * str);
	struct bitmask *parse_cpumask(const char *option, const int max_cpus);

	// customized pipe operations
	FILE * popen2(const char * command, const char * type, pid_t * pid);
	int pclose2(FILE * fp, pid_t pid, int dokill);
#endif

