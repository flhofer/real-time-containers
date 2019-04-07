#include "pidparm.h"

#ifndef __UPDATE_H_
	#define __UPDATE_H_

	// Included in kernel 4.13
	#ifndef SCHED_FLAG_RECLAIM
		#define SCHED_FLAG_RECLAIM		0x02
	#endif

	// Included in kernel 4.16
	#ifndef SCHED_FLAG_DL_OVERRUN
		#define SCHED_FLAG_DL_OVERRUN		0x04
	#endif

	#define USEC_PER_SEC		1000000
	#define NSEC_PER_SEC		1000000000
	#define TIMER_RELTIME		0
	#define PID_BUFFER			4096

	static int clocksources[] = {
		CLOCK_MONOTONIC,
		CLOCK_REALTIME,
		CLOCK_PROCESS_CPUTIME_ID,
		CLOCK_THREAD_CPUTIME_ID
	};

	typedef struct pid_info {
		pid_t pid;
		char * psig; 
		char * contid;
	} pidinfo_t;

	// settings from schedstat.h
	// Parameters and runtime values ----- 
	// values set at startup in main thread, never changed there anymore
	extern int use_cgroup; // processes identificatiom mode, written before startup of thread
	extern int interval; // settting, default to SCAN
	extern int update_wcet; // worst case execution time for deadline scheduled task
	extern int loops; // once every x interval the containers are checked again
	extern int priority; // priority for eventual RT policy
	extern int policy;	/* default policy if not specified */
	extern int clocksel; // clock selection for intervals
	extern char * cont_ppidc; // container pid signature to look for
	extern char * cont_pidc; // command line pid signature to look for
	extern int kernelversion; // using kernel version.. 
	extern int setdflag; // set deadline overrun flag? for DL processes?
	extern int runtime; // test runtime -> 0 = infinite
//	extern int negiszero; // treat negative deadline difference as zero

	extern pthread_mutex_t dataMutex;
	extern node_t * head;

	void *thread_update (void *arg); // thread that verifies status and allocates new threads

#endif
