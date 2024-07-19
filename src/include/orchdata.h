#ifndef __ORCHDATA_H_
	#define __ORCHDATA_H_

	#include <stdio.h>
	#include <stdlib.h>

	// Custom includes
	#include "runstats.h"	// statistics functions for curve fitting and prob estimation
	#include "rt-sched.h"	// temporary as libc does not include new sched yet
	#include "error.h"		// error and stderr print functions

	#define CMD_LEN 255			// Size for PID command signatures and compositions, stack variables
	#define MAXCMD_LEN 1024		// maximum command line signature buffer
	// limited to 32k processors ;)
	#define SCHED_NODATA 0xFFFF	// constant for no scheduling data

	// masks for the status of PIDs (node_t)
	#define MSK_STATUPD			0x1	// scheduling parameters update done
	#define	MSK_STATNMTCH		0x2 // no parameter match
	#define MSK_STATWCUD		0x4	// WCET changed for PID
	#define MSK_STATSIBL		0x8 // PID has siblings in container
	#define MSK_STATNAFF		0x10 // PID has no affinity yet
	#define MSK_STATHERR		0x20 // HIST CDF initialization error
	#define MSK_STATNRSCH		0x40 // Running task has requested reschedule
	#define MSK_STATNPRD		0x80 // Running task has ended a Period

	// masks for the status of configurations, PID/CNT/IMG
	#define MSK_STATCFIX		0x1	// CPU affinity configuration is fixed
	#define	MSK_STATCCRT		0x2 // Configuration created from Runtime

	#define MSK_STATSHAT		0x10// shared attribute configuration
	#define MSK_STATSHRC		0x20// shared resource configuration

	// masks for the status of orchestrator (prgset_t)
	#define MSK_STATTRTL		0x1	// setting RT throttle was successful done
	#define	MSK_STATRUNC		0x2 // startup running containers present

	// masks fot the status of a resource (resTracer_t)
	#define MSK_STATHRMC		0x1	// resource allocation periods are harmonic

	// masks for dry-run selective environment preparation
	#define MSK_DRYNOSMTOFF		0x1 // Do not disable SMT
	#define MSK_DRYNOCPUGOV		0x2 // Do not change CPU governor
	#define MSK_DRYNOCPUQOS		0x2	// Do not change CPU QoS setting -> same as above
	#define MSK_DRYNORTTHRT		0x4	// Do not change RT throttle
	#define MSK_DRYNORTSLCE		0x4	// Do not change RT RR slice
	#define MSK_DRYNOKTRDAF		0x8	// Do not change kernel thread affinity

	#define MSK_DRYNOCGRPRT		0x10	// Do not set Exclusive Control Groups ('root' for CGroup v2)
	#define MSK_DRYNOAFTY		0x20	// Do not set affinity of container/task, CPU and memory
	#define MSK_DRYNOCPUPSH		0x40	// Do not Hot-plug CPUs for IRQ push
	#define MSK_DRYNOTSKPSH		0x80	// Do not push tasks into new system group (only CGroup v1)

	#define MSK_DRYNOTRCNG		0x100	// Do not set tracing kernel debug -! expect malfunction

	#define MSK_DRYALL			0xFFFF	// Disable ALL

	// default values, changeable via cli
	#define TSCAN 5000	// scan time of updates
	#define TWCET 100	// default WCET for deadline scheduling, min-value
	#define TDETM 100	// x*TSCAN, time check new containers
	#define TSCHS 1024  // scheduler minimum granularity
	#define BUFRD 1024  // buffer read size
	#define CONT_PID  "bash" 	// default program signature (test)
#ifdef CGROUP2
	#define CONT_PPID "containerd-shim-runc-v2"
									  // Default shim for CGroupV2
	#define CGRP_SYS  "system.slice/" // default CGroup slice for system (default for docker daemon)
	#define CGRP_USER "user.slice/"   // default CGroup slice for user
	#define CGRP_DCKR "docker.slice/" // default CGroup slice for docker containers as configured in daemon.json (suggested)
	#define CGRP_CSET "" 			  // CGroup v1 value, unused
	#define CGRP_PIDS "cgroup.threads"  // CGv2 file for running PIDs
	#define CGRP_DCKP "docker-"		  // CGv2 docker container prefix
	#define CGRP_DCKS ".scope"		  // CGv2 docker container suffix
#else
	#define CONT_PPID "containerd-shim"
								// Default shim for CGroupV1
	#define CGRP_SYS  "system/" // default CGroup slice for system (default for docker daemon)
	#define CGRP_DCKR "docker/" // default CGroup sub-directory for containers
	#define CGRP_CSET "cpuset/" // Cgroupv1 tree for cpuset controller
	#define CGRP_PIDS "tasks"	// CGv1 file for running PIDs
#endif

	#define SYSCPUS 1 // 1 -> count of min CPUS reserved for orchestrator and system
	#define CPUGOVR	"performance" // desired configuration for CPU governor

	#define MINCHNGL	95		// Minimum change for PDF update in percent
	#define MINCHNGH	105		// Minimum change for PDF update in percent

	// definition of container detection modes
	enum det_mode {
		DM_CMDLINE = 0,	// use command line signature for detection
		DM_CNTPID,	// use container skim instances to detect PIDs
		DM_CGRP,	// Use CGroup to detect PIDs of processes
	};

	enum aff_mode {
		AFFINITY_UNSPECIFIED 	= 0x00,	// use default settings
		AFFINITY_USERSPECIFIED,	 		// user defined settings
		AFFINITY_NUMASEPARATED 	= 0x10,	// divide RT/non-RT according to NUMA separation (two groups)
		AFFINITY_NUMABALANCED,			// use multiple NUMA groups to create multiple RT-groups
		AFFINITY_USEALL			= 0x20,	// go for all!! - uses no exclusive Docker partition!
	};

	// definition of container detection modes
	enum sched_mode {
		SM_STATIC = 0,	// use static allocation only (NO RESCHEDULING)
		SM_ADAPTIVE,	// use adaptive slot allocation at startup (NO RESCHEDULING)
		SM_PADAPTIVE,	// use progressive adaptive slot allocation (NO RESCHEDULING)
		SM_DYNSIMPLE,	// use the simple affinity based dynamic scheduling (like adaptive)
		SM_DYNMCBIN		// use Monte-Carlo bin allocation style algorithm
	};

	typedef struct sched_rscs { // resources 
		int32_t affinity; // exclusive CPU-numbers
		struct bitmask * affinity_mask;	// computed affinity mask
		int32_t rt_timew; // RT execution time soft limit
		int32_t rt_time;  // RT execution time hard limit
		int32_t mem_dataw; // Data memory soft limit
		int32_t mem_data;  // Data memory time hard limit
	} rscs_t;

	// ############################  WARN -- DO NOT CHANGE ##########################3
	// ##### from here on, same structure to keep format! ### unify?
	// use this? ----- depth 0 = image, 1 = container, 2 - pid

	typedef struct pidc_parm {
		struct pidc_parm *next; 
		char *psig; 			// matching signatures -> container IDs
		int 			 status;// generic status info
		struct sched_attr *attr;// standard Linux PID attributes
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
		int 			 status;// generic status info
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
		int 			 status;// generic status info
		struct sched_attr *attr;// container sched attributes, default
		struct sched_rscs *rscs;// container default & max resource settings 
		struct pids_parm  *pids;// linked list pointing to the pids for this img	
		struct conts_parm *conts;// linked list pointing to the containers	
	} img_t;

	// ################################## Until here! ##########################

	typedef struct containers {
		struct img_parm   *img;	// linked list of images_t
		struct cont_parm  *cont;// linked list of containers_t
		struct pidc_parm  *pids;// linked list of pidc_t
		struct sched_attr *attr;// global sched attributes, default.
		struct sched_rscs *rscs;// global resource settings, default & max
		uint32_t nthreads;		// number of configured containers PIDs-threads
		uint32_t num_cont;		// number of configured containers
	} containers_t;

	typedef struct resTracer { // resource tracers
		struct resTracer * next;
		struct bitmask * affinity; 	// exclusive CPU-num / or SMT sibling
		int 	 numa;			// NUMA node for this CPU / siblings
		float	 U;				// utilization factor
		int 	 status;		// generic status info
		uint64_t usedPeriod;	// amount of CPU-time left..
		uint64_t basePeriod;	// if a common period is set, or least common multiplier
		// used during runtime for stats
		float	 Umin;			// utilization factor
		float	 Uavg;			// utilization factor
		float	 Umax;			// utilization factor
	} resTracer_t;

	typedef struct sched_mon { // actual values for monitoring

		// runtime statistics
		uint64_t rt;			// deadline last read runtime value/budget
		uint64_t rt_min;		// minimum run-time value
		uint64_t rt_avg;		// average run-time value
		uint64_t rt_max;		// maximum run-time value

		// Time stamps and check counts
		uint64_t last_ts;		// last time stamp for this task
		uint64_t last_tsP;		// last time stamp for this task's period
		uint64_t deadline;		// DL: deadline last read absolute value (may approximate next iter)
								// FIFO/RR?.. WakeUp+cdf_period
		uint64_t dl_count;		// deadline/period verification/change count
		uint64_t dl_scanfail;	// deadline/period debug scan failure (diff == period)
		uint64_t dl_overrun;	// deadline/period overrun count

		int64_t  dl_diff;		// overrun-GRUB handling : deadline diff sum!
		int64_t  dl_diffmin;	// overrun-GRUB handling : diff min peak, filtered
		int64_t  dl_diffavg;	// overrun-GRUB handling : diff avg sqr, filtered
		int64_t  dl_diffmax;	// overrun-GRUB handling : diff max peak, filtered

		// CDF and distribution values
		uint64_t cdf_runtime;	// CDF p-thresh max runtime, trigger level
		uint64_t cdf_period;	// CDF computed periodic distance for non DL tasks

		stat_hist *	pdf_hist;	// histogram data to estimate the PDF
		stat_cdf *  pdf_cdf;	// CDF data collection

		stat_hist *	pdf_phist;	// histogram data to estimate the PDF of the period
		stat_cdf *  pdf_pcdf;	// CDF data collection for the period

		// Runtime allocation
		int32_t assigned; 		// actually running CPU, -1 = unassigned
		struct bitmask * assigned_mask;	// computed assignment mask
		uint64_t resched;		// number of rescheduling times
		uint64_t resample;		// number of resampling times
	} nodemon_t;

	typedef struct sched_pid { // PID management and monitoring info
		struct sched_pid * next;
		pid_t pid;		// runtime PID number
		int status;		// generic status info
		char * psig;	// temp char, then moves to entry in pidparam. identifying signature
		// usually at least one of two is set
		char * contid; 	// temp char, then moves to entry in pidparam. identifying container
		char * imgid; 	// temp char, then moves to entry in pidparam. identifying container
		struct sched_attr attr;
		struct sched_mon mon;
		pidc_t * param;			// points to entry in pid-param, mutliple pid-same param
	} node_t;

	typedef struct prg_settings {

		// filepaths
		char *logdir;				// path to put log data in
		char *logbasename;			// file prefix for logging data

		// signatures and folders
		char * cont_ppidc;
		char * cont_pidc;
		char * cont_cgrp; // CGroup sub-directory configuration for container detection

		// file-paths virtual file system
		char *procfileprefix;
		char *cgroupfileprefix;
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
		int update_wcet;			// WCET for sched deadline
		int loops;					// repetition loop count for container check
		int runtime;				// total orchestrator runtime, 0 is infinite
		int psigscan;				// scan for child threads, -n option only
		int trackpids;				// keep track of left pids, do not delete from list
		int dryrun;					// test only, no changes to environment
		int blindrun;				// blind run of orchestrator, avoid settings (extension of dryrun)
		int lock_pages;				// memory lock on startup
		int force;					// force environment changes if needed
		int smi;					// enable smi counter check
		int rrtime;					// round robin slice time. 0=no change

		// runtime values
		int kernelversion;			// kernel version -> opts based on this
		int status;					// generic status flags

		// affinity specification for system vs RT
		enum aff_mode setaffinity;	// affinity mode enumeration
		char * affinity; 			// default split, 0-0 SYS, Syscpus to end rest
		struct bitmask *affinity_mask; // default bitmask allocation of threads!!
		char * numa;				// value for numa selection;

		// runtime settings
		int ftrace; 				// enable Kernel ftrace for run-time statistics
		enum det_mode use_cgroup;	// identify processes via CGroup
		enum sched_mode sched_mode;	// scheduling control mode
		double ptresh;				// probability threshold for resource switching

	} prgset_t;

	extern int clocksources[];

	// generic push pop
	void push(void ** head, size_t size);
	void pop(void ** head);
	void qsortll(void **head, int (*compar)(const void *, const void*) );

	// special - free structure
	void freeContParm(containers_t * contparm);
	int	checkContParam(containers_t * contparm);
	void freePrgSet(prgset_t * prgset);
	void freeTracer(resTracer_t ** rHead);
	void freeParm(cont_t * item);
	void copyResourceConfigC(cont_t * from, cont_t * to);
	void copyResourceConfigP(pidc_t * from, pidc_t * to);

	// Management of PID nodes - runtime - MUTEX must be acquired
	// separate, as they set init values and free subs
	void node_push(node_t ** head);
	void node_pop(node_t ** head);
#endif
