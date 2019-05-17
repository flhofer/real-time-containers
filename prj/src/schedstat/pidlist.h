#include <stdio.h>
#include <stdlib.h>
#include "rt-sched.h" // temporary as libc does not include new sched yet

#ifndef __PIDLIST_
	#define __PIDLIST_

	#define SIG_LEN 65			// increased to 64 -> standard lenght of container IDs for docker
	// TODO: limited to 32k processors ;)
	#define SCHED_NODATA 0xFFFF	// constant for no scheduling data
	#define SCHED_FAFMSK 0xE000	// flexible affinity mask

	struct sched_rscs { // resources 
		int32_t affinity; // exclusive cpu-num
		int32_t rt_timew; // RT execution time soft limit
		int32_t rt_time;  // RT execution time hard limit
		int32_t mem_dataw; // Data memory soft limit
		int32_t mem_data;  // Data memory time hard limit
		// TODO: fill with other values, i.e. memory bounds ecc
	};

	typedef struct pid_parm {
		char psig[SIG_LEN]; 	// matching signatures -> target pids
		char contid[SIG_LEN]; 	// matching signatures -> container IDs
		struct sched_attr attr; // standard linux pid attributes
		struct sched_rscs rscs;   // additional resource settings 
		struct pid_parm* next;
	} parm_t;

	struct resTracer { // resource tracers
		int32_t affinity; 		// exclusive cpu-num
		uint64_t usedPeriod;	// amount of cputime left..
		uint64_t basePeriod;	// if a common period is set, or least common multiplier
		// TODO: fill with other values, i.e. memory amounts ecc
		struct resTracer * next;
	};

	struct sched_mon { // actual values for monitoring
		int64_t rt_min;
		int64_t rt_avg;
		int64_t rt_max;
		uint64_t dl_count;		// deadline verification/change count
		uint64_t dl_scanfail;	// deadline debug scan failure (diff == period)
		uint64_t dl_overrun;	// overrun count
		uint64_t dl_deadline;	// deadline last absolute value
		int64_t  dl_rt;			// deadline last runtime value
		int64_t  dl_diff;		// overrun-GRUB handling : deadline diff sum!
		int64_t  dl_diffmin;	// overrun-GRUB handling : diff min peak, filtered
		int64_t  dl_diffavg;	// overrun-GRUB handling : diff avg sqr, filtered
		int64_t  dl_diffmax;	// overrun-GRUB handling : diff max peak, filtered
	};

	typedef struct sched_pid { // pid mamagement and monitoring info
		pid_t pid;
		// usually only one of two is set
		char * psig;	// temp char, then moves to entry in pidparam. identifying signature
		char * contid; 	// temp char, then moves to entry in pidparam. identifying container
		struct sched_attr attr;
//		struct sched_rscs rscs;
		struct sched_mon mon;
		parm_t * param;			// points to entry in pidparam, mutliple pid-same param
		struct sched_pid * next;
	} node_t;

	extern node_t * head;

	void push(node_t ** head, pid_t pid, char * psig, char * contid);
	void insert_after(node_t ** head, node_t ** prev, pid_t pid, char * psig, char * contid);
	pid_t pop(node_t ** head);
	pid_t drop_after(node_t ** head, node_t ** prev);

	void rpush(struct resTracer ** head);
	void ppush(parm_t ** head);

#endif
