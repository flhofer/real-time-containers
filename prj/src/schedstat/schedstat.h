#define _GNU_SOURCE 

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // used for string parsing
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants
#include "pidlist.h" // memory structure to store information

#include <sched.h>
#include <linux/types.h>
#include <signal.h> // for SIGs
//#include <stdarg.h> __VA_ARGS__ does not work??
#include "rt-sched.h" // temporary as libc does not include new sched yet

//#include "rt_numa.h" // from cyclictest -> affinity of cpu and memory

// new since cgroup
#include <fcntl.h> 
#include <sys/utsname.h>
#include <sys/capability.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <dirent.h>
#include <getopt.h>
#include "error.h"
#include "rt-utils.h"

#ifndef __SCHEDSTAT_
	#define __SCHEDSTAT_

	#define PRGNAME "DC static orchestrator"
	#define VERSION 0.22
	#define MAX_PIDS 64
	#define MAX_CPUS 8

	enum kernelversion {
		KV_NOT_SUPPORTED,
		KV_26_LT18,
		KV_26_LT24,
		KV_26_33,
		KV_30,
		KV_40,
		KV_413,	// includes full EDF for the first time
		KV_416	// includes full EDF with GRUB-PA for ARM
	};

	#define DBG
	/* Debug printing to console or buffer ?? */
//	void inline vbprintf ( const char * format, ... );

	#ifdef DBG
		#define printDbg (void)printf
	#else
		#define printDbg (void)vbprintf
	#endif

	// general default
	#define KNRM  "\x1B[0m"
	#define KRED  "\x1B[31m"
	#define KGRN  "\x1B[32m"
	#define KYEL  "\x1B[33m"
	#define KBLU  "\x1B[34m"
	#define KMAG  "\x1B[35m"
	#define KCYN  "\x1B[36m"
	#define KWHT  "\x1B[37m"

	// here, as it will be changed with cli later
	#define TSCAN 100000 // scan time of updates
	#define TDETM 10	// x*TSCAN, time check new containers
	#define CONT_PPID "docker-containerd-shim"

	#define SYSCPUS 1 // 0-> count reserved for orchestrator and system

	static char *procfileprefix = "/proc/sys/kernel/";
	static char *cpusetfileprefix = "/sys/fs/cgroup/cpuset/";
	static char *cpusetdfileprefix = "/sys/fs/cgroup/cpuset/docker/";

	enum det_mode {
		DM_CMDLINE,	// use command line signature for detection
		DM_CNTPID,	// use container skim instances to detect pids
		DM_CGRP		// USe cgroup to detect PIDs of processes
	};

#endif
