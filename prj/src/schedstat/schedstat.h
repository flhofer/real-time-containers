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
	#define VERSION 0.25
	#define MAX_PIDS 64
	#define MAX_CPUS 8

	enum kernelversion {
		KV_NOT_SUPPORTED,
		KV_314,
		KV_40,
		KV_413,	// includes full EDF for the first time
		KV_416,	// includes full EDF with GRUB-PA for ARM
		KV_50	// latest release 
	};

// 	#define DBG
	/* Debug printing to console or buffer ?? */
//	void inline vbprintf ( const char * format, ... );

	#ifdef DBG
		#define printDbg (void)printf
	#else
		#define printDbg //
	#endif

	// here, as it will be changed with cli later
	#define TSCAN 10000 // scan time of updates
	#define TWCET 1200 	// default WCET for deadline scheduling
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
