#include "orchdata.h" // memory structure to store information

#include <stdio.h>
#include <string.h> // used for string parsing
#include <unistd.h> // used for POSIX XOPEN constants
#include <fcntl.h>	// file control, new open/close functions
#include <errno.h>	// error numbers and strings
#include <sys/utsname.h>	// kernel info
#include <cpuid.h>			// cpu information
#include <numa.h>			// numa node ident

#include "error.h"		// error and strerr print functions


#ifndef __KERNUTIL_H_
	#define __KERNUTIL_H_

	#if (defined(__i386__) || defined(__x86_64__))
		#define ARCH_HAS_SMI_COUNTER
	#endif

	#define MSR_SMI_COUNT		0x00000034
	#define MSR_SMI_COUNT_MASK	0xFFFFFFFF

	enum kernelversion {
		KV_NOT_SUPPORTED,
		KV_314,
		KV_40,
		KV_413,	// includes full EDF for the first time
		KV_416,	// includes full EDF with GRUB-PA for ARM
		KV_50	// latest releases, now 5.1 (May 8th '19)
	};

	extern prgset_t * prgset; // read only programm setings structure

	// MSR 
	int open_msr_file(int cpu);
	int get_smi_counter(int fd, unsigned long *counter);
	int has_smi_counter(void);

	// Kernel detection and values
	int check_kernel(void);
	int setkernvar(const char *prefix, const char *name, char *value);
	int getkernvar(const char *prefix, const char *name, char *value, int size);

	// affinity and cpu bitmasks
	int parse_bitmask(struct bitmask *mask, char * str);
	struct bitmask *parse_cpumask(const char *option, const int max_cpus);
#endif

