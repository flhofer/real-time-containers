#include <stdio.h>
#include <stdlib.h>
#include "rt-sched.h" // temporary as libc does not include new sched yet

#ifndef __PIDPARM_
#define __PIDPARM_

#define SIG_LEN 50


struct sched_rscs { // resources 
	int affinity; // exclusive cpu-num
	// TODO: fill with other values, i.e. memory bounds ecc
};

typedef struct pid_parm {
	char psig[SIG_LEN]; 	// matching signatures -> target pids
	struct sched_attr attr; // standard linux pid attributes
	struct sched_rscs rscs;   // additional resource settings 
	struct pid_parm* next;
} parm_t;

void ppush(parm_t ** head);
struct sched_attr * pget_node(parm_t * act);
struct sched_attr * pget_next(parm_t ** act);

#endif
