#include <stdio.h>
#include <stdlib.h>
#include "rt-sched.h" // temporary as libc does not include new sched yet
#include "error.h"		// error and strerr print functions

#ifndef __PIDLIST_
	#define __PIDLIST_

	#define SIG_LEN 65			// increased to 64 -> standard lenght of container IDs for docker
	// TODO: limited to 32k processors ;)
	#define SCHED_NODATA 0xFFFF	// constant for no scheduling data
	#define SCHED_FAFMSK 0xE000	// flexible affinity mask

	#define MAX(x, y) (((x) > (y)) ? (x) : (y))
	#define MIN(x, y) (((x) < (y)) ? (x) : (y))

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

	// definition of container detection modes
	enum det_mode {
		DM_CMDLINE,	// use command line signature for detection
		DM_CNTPID,	// use container skim instances to detect pids
		DM_CGRP		// USe cgroup to detect PIDs of processes
	};

	enum {
		AFFINITY_UNSPECIFIED,	// use default settings
		AFFINITY_SPECIFIED,	 	// user defined settings
		AFFINITY_USEALL			// go for all!!
	};


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
		struct sched_mon mon;
		parm_t * param;			// points to entry in pidparam, mutliple pid-same param
		struct sched_pid * next;
	} node_t;

	typedef struct prg_settings {

		// filepaths
		char *logdir;				// TODO: path to put log data in
		char *logbasename;			// TODO: file prefix for logging data

		// filepaths virtual file system
		char *procfileprefix;
		char *cpusetfileprefix;
		char *cpusystemfileprefix;
		char *cpusetdfileprefix; // file prefix for Docker's Cgroups, default = [CGROUP/]docker/

		// signatures and folders
		char * cont_ppidc;
		char * cont_pidc;
		char * cont_cgrp; // CGroup subdirectory configuration for container detection

		// parameters
		int priority;				// priority parameter for FIFO and RR
		int clocksel;				// selected clock 
		int policy;					// default policy if not specified
		int quiet;					// quiet enabled
		int affother;				// set affinity of parent as well
		int setdflag;				// set deadline overrun flag
		int interval;				// scan interval
		int update_wcet;			// wcet for sched deadline
		int loops;					// repetition loop count for container check
		int runtime;				// total orchestrator runtime, 0 is infinite
		int psigscan;				// scan for child threads, -n option only
		int trackpids;				// keep track of left pids, do not delete from list
		//int negiszero;
		int dryrun;					// test only, no changes to environment
		int lock_pages;				// memory lock on startup
		int force;					// force environment changes if needed
		int smi;					// enable smi counter check
		int rrtime;					// round robin slice time. 0=no change

		// affinity specification for system vs RT
		int setaffinity;			// affinty mode enumeration
		char * affinity; 			// default split, 0-0 SYS, Syscpus to end rest
		struct bitmask *affinity_mask; // default bitmask allocation of threads!!


		int logsize;				// TODO: limit for logsize
		int gnuplot; 				// TODO: enable gnuplot output at the end
		int ftrace; 				// TODO: enable ftrace kernel trace output at the end

		// runtime settings
		int use_cgroup;				// identify processes via cgroup

	} prgset_t;

	void push(node_t ** head, pid_t pid, char * psig, char * contid);
	void insert_after(node_t ** head, node_t ** prev, pid_t pid, char * psig, char * contid);
	pid_t pop(node_t ** head);
	pid_t drop_after(node_t ** head, node_t ** prev);

	void rpush(struct resTracer ** head);
	void ppush(parm_t ** head);

#endif
