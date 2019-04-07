#include <stdio.h>
#include <stdlib.h>
#include "rt-sched.h" // temporary as libc does not include new sched yet
#include "pidparm.h"

#ifndef __PIDLIST_
#define __PIDLIST_

struct sched_mon { // actual values for monitoring
/*	uint64_t rt_min;
	uint64_t rt_avg;
	uint64_t rt_max;*/
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
	struct sched_rscs rscs;
	struct sched_mon mon;
	parm_t * param;			// points to entry in pidparam, mutliple pid-same param
	struct sched_pid * next;
} node_t;

extern node_t * head;

void push_t(node_t * head, pid_t pid, char * psig, char * contid);
void push(node_t ** head, pid_t pid, char * psig, char * contid);
void insert_after(node_t ** head, node_t ** prev, pid_t pid, char * psig, char * contid);
pid_t pop(node_t ** head);
pid_t drop_after(node_t ** head, node_t ** prev);
//pid_t remove_last(node_t * head);
//pid_t remove_by_index(node_t ** head, int n);
//int remove_by_value(node_t ** head, pid_t pid);
struct sched_attr * get_node(node_t * act);
void get_next(node_t ** act);

#endif
