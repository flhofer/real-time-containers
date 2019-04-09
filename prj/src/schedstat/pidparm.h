#include <stdio.h>
#include <stdlib.h>
#include "rt-sched.h" // temporary as libc does not include new sched yet

#ifndef __PIDPARM_
#define __PIDPARM_

#define SIG_LEN 65		// increased to 64 -> standard lenght of container IDs for docker
#define SCHED_NODATA 99 // constant for no scheduling data

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

void ppush(parm_t ** head);
struct sched_attr * pget_node(parm_t * act);
struct sched_attr * pget_next(parm_t ** act);

#endif
