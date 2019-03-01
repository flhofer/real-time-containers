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

#ifndef __SCHEDSTAT_H_
	#define __SCHEDSTAT_H_

	#define PRGNAME "DC static orchestrator"
	#define VERSION 0.1
	#define MAX_PIDS 64
	#define MAX_CPUS 8

	#define DBG

	/* Debug printing to console or buffer ?? */
	#ifdef DBG
		#define printDbg (void)printf

	#else
		#define printDbg
	#endif

#endif
