#ifndef __ORCHDATA_H_
	#define __ORCHDATA_H_

	#include <stdio.h>
	#include <stdlib.h>

	// Custmom includes
	#include "rt-sched.h" // temporary as libc does not include new sched yet
	#include "error.h"		// error and strerr print functions

	#define SIG_LEN 65			// increased to 64 + null -> standard lenght of container IDs for docker
	#define MAXCMDLINE 1024		// maximum commandline signature buffer
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
	#define CONT_PPID "containerd-shim"
	#define CONT_PID  "bash" // test for now :)
	#define CONT_DCKR "docker/" // default cgroup subdirectory

	#define SYSCPUS 0 // 0-> count reserved for orchestrator and system
	#define CPUGOVR	"performance" // configuration for cpu governor	

	// definition of container detection modes
	enum det_mode {
		DM_CMDLINE,	// use command line signature for detection
		DM_CNTPID,	// use container skim instances to detect pids
		DM_CGRP,	// USe cgroup to detect PIDs of processes
		DM_DLEVNT	// docker link event
	};

	enum {
		AFFINITY_UNSPECIFIED,	// use default settings
		AFFINITY_SPECIFIED,	 	// user defined settings
		AFFINITY_USEALL			// go for all!!
	};

	typedef struct sched_rscs { // resources 
		int32_t affinity; // exclusive cpu-num
		// TODO: verify data type -> rlim in gdb says unsigned long
		int32_t rt_timew; // RT execution time soft limit
		int32_t rt_time;  // RT execution time hard limit
		int32_t mem_dataw; // Data memory soft limit
		int32_t mem_data;  // Data memory time hard limit
		// TODO: fill with other values, i.e. memory bounds ecc
	} rscs_t;

	typedef struct pidc_parm {
		struct pidc_parm *next; 
		char *psig; 			// matching signatures -> container IDs
		struct sched_attr *attr;// standard linux pid attributes
		struct sched_rscs *rscs;// additional resource settings 
		struct cont_parm  *cont;// pointer to the container settings
		struct img_parm   *img; // pointer to the image settings
	} pidc_t;

	typedef struct pids_parm {
		struct pids_parm *next; // list to next entry
		struct pidc_parm *pid; 	// matching pid, one entry
	} pids_t;

	typedef struct cont_parm {
		struct cont_parm  *next; 
		char			*contid;// matching signatures -> container IDs
		struct sched_attr *attr;// container sched attributes, default
		struct sched_rscs *rscs;// container default & max resource settings 
		struct pids_parm  *pids;// linked list pointing to the pids	
		struct img_parm   *img; // pointer to the image settings
	} cont_t;

	typedef struct conts_parm {
		struct conts_parm *next;// list to next entry
		struct cont_parm  *cont;// matching container, one entry
	} conts_t;

	typedef struct img_parm {
		struct img_parm  *next; 
		char 			 *imgid;// matching signatures -> image IDs
		struct sched_attr *attr;// container sched attributes, default
		struct sched_rscs *rscs;// container default & max resource settings 
		struct pids_parm  *pids;// linked list pointing to the pids for this img	
		struct conts_parm *conts;// linked list pointing to the containers	
	} img_t;

	typedef struct containers {
		struct img_parm   *img;	// linked list of images_t
		struct cont_parm  *cont;// linked list of containers_t
		struct pidc_parm  *pids;// linked list of pidc_t
		struct sched_attr *attr;// global sched attributes, default.
		struct sched_rscs *rscs;// global resource settings, default & max
		uint32_t nthreads;		// number of configured containers pids-threads
		uint32_t num_cont;		// number of configured containers
	} containers_t;

	typedef struct resTracer { // resource tracers
		struct resTracer * next;
		int32_t affinity; 		// exclusive cpu-num
		uint64_t usedPeriod;	// amount of cputime left..
		uint64_t basePeriod;	// if a common period is set, or least common multiplier
		// TODO: fill with other values, i.e. memory amounts ecc
	} resTracer_t;

	typedef struct sched_mon { // actual values for monitoring
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
	} nodemon_t;

	typedef struct sched_pid { // pid mamagement and monitoring info
		struct sched_pid * next;
		pid_t pid;		// runtime pid number
		int det_mode;	// detection mode used for the pid 
		char * psig;	// temp char, then moves to entry in pidparam. identifying signature
		// usually at least one of two is set
		char * contid; 	// temp char, then moves to entry in pidparam. identifying container
		char * imgid; 	// temp char, then moves to entry in pidparam. identifying container
		struct sched_attr attr;
		struct sched_mon mon;
		pidc_t * param;			// points to entry in pidparam, mutliple pid-same param
	} node_t;

	typedef struct prg_settings {

		// filepaths
		char *logdir;				// TODO: path to put log data in
		char *logbasename;			// TODO: file prefix for logging data

		// signatures and folders
		char * cont_ppidc;
		char * cont_pidc;
		char * cont_cgrp; // CGroup subdirectory configuration for container detection

		// filepaths virtual file system
		char *procfileprefix;
		char *cpusetfileprefix;
		char *cpusystemfileprefix;
		char *cpusetdfileprefix; // file prefix for Docker's Cgroups, default = [CGROUP/]docker/

		// parameters
		int priority;				// priority parameter for FIFO and RR
		int clocksel;				// selected clock 
		uint32_t policy;			// default policy if not specified
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
		int use_fifo;				// TODO: use fifo buffer for output

		// runtime values
		int kernelversion; // kernel version -> opts based on this
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

	// generic push pop
	void push(void ** head, size_t size);
	void pop(void ** head);
	void qsortll(void **head, int (*compar)(const void *, const void*) );

	// Management of PID nodes - runtime - MUTEX must be acquired
	// separate, as they set init values and free subs
	void node_push(node_t ** head);
	void node_pop(node_t ** head);

	// runtime manipulation of configuration and PID nodes - MUTEX must be acquired
	int node_findParams(node_t* node, containers_t * conts);
#endif
