#include "orchdata.h"	// memory structure to store information

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
	#define MAX_PIDS 64 // max containers detectable

	typedef struct pid_info {
		pid_t pid;
		char * psig; 
		char * contid;
	} pidinfo_t;

	void *thread_update (void *arg); // thread that verifies status and allocates new threads

#endif
