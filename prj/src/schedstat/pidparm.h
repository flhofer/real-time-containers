#include <stdio.h>
#include <stdlib.h>
#include "rt-sched.h" // temporary as libc does not include new sched yet

#ifndef __PIDPARM_
#define __PIDPARM_

#define SIG_LEN 50

typedef struct pid_parm {
	char psig[SIG_LEN]; 
	struct sched_attr attr;
	struct pid_parm* next;
} parm_t;

void ppush(parm_t ** head);
struct sched_attr * pget_node(parm_t * act);
struct sched_attr * pget_next(parm_t ** act);

#endif
