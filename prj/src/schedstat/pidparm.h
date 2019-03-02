#include <stdio.h>
#include <stdlib.h>
#include "rt-sched.h" // temporary as libc does not include new sched yet

#ifndef __PIDPARM_
#define __PIDPARM_

typedef struct pid_parm {
	char*  psig; 
	struct sched_attr attr;
	struct pid_parm* next;
} parm_t;

void ppush_t(parm_t * head, pid_t pid);
struct sched_attr * ppush(parm_t ** head, pid_t pid);
struct sched_attr * pget_node(parm_t * act);
struct sched_attr * pget_next(parm_t ** act);

#endif
