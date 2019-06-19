#define _GNU_SOURCE 

// Default stuff, needed form main operation
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // used for string parsing
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants

#include <sched.h>			// scheduler functions
#include <linux/sched.h>	// linux specific scheduling
#include <linux/types.h>	// data structure types, short names and linked list
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <fcntl.h>			// file control, new open/close functions
#include <dirent.h>			// dir enttry structure and expl
#include <errno.h>			// error numbers and strings

#include "pidlist.h"	// memory structure to store information
#include "rt-utils.h"	// trace and other utils
#include "error.h"		// error and strerr print functions
#include "kernutil.h"	// generic kernel utilities

#ifndef __SCHEDSTAT_
	#define __SCHEDSTAT_

	#define PRGNAME "DC static orchestrator"
	#define VERSION "0.61c"

	enum kernelversion {
		KV_NOT_SUPPORTED,
		KV_314,
		KV_40,
		KV_413,	// includes full EDF for the first time
		KV_416,	// includes full EDF with GRUB-PA for ARM
		KV_50	// latest releases, now 5.1 (May 8th '19)
	};

	// Uncomment this line to enable high debug output
 	//#define DBG

	#ifdef DBG
		#define printDbg (void)printf
	#else
		#define printDbg //
	#endif

	// default values, changeable via cli
	#define TSCAN 5000	// scan time of updates
	#define TWCET 100	// default WCET for deadline scheduling, min-value
	#define TDETM 100	// x*TSCAN, time check new containers
	#define TSCHS 1024  // scheduler minimum granularity
	#define BUFRD 1024  // buffer read size
	#define CONT_PPID "docker-containerd-shim"
	#define CONT_PID  "bash" // test for now :)
	#define CONT_DCKR "docker/" // default cgroup subdirectory

	#define SYSCPUS 0 // 0-> count reserved for orchestrator and system
	#define CPUGOVR	"performance" // configuration for cpu governor	

	// procfs and sysfs path constants
	static char *procfileprefix = "/proc/sys/kernel/";
	static char *cpusetfileprefix = "/sys/fs/cgroup/cpuset/";
	static char *cpusystemfileprefix = "/sys/devices/system/cpu/";
	int setkernvar_ex(const char *prefix, const char *name, char *value);

	// definition of container detection modes
	enum det_mode {
		DM_CMDLINE,	// use command line signature for detection
		DM_CNTPID,	// use container skim instances to detect pids
		DM_CGRP		// USe cgroup to detect PIDs of processes
	};

	#define MAX(x, y) (((x) > (y)) ? (x) : (y))
	#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif
