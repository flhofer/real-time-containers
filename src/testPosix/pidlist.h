#include <stdio.h>
#include <stdlib.h>
#include "rt-sched.h" // temporary as libc does not include new sched yet

#ifndef __PIDLIST_
#define __PIDLIST_

typedef struct sched_pid {
	pid_t pid;
	int affinity;
	struct sched_attr attr;
	struct sched_pid * next;
} node_t;

extern node_t * head;

void push_t(node_t * head, pid_t pid);
struct sched_attr * push(node_t ** head, pid_t pid);
struct sched_attr * insert_after(node_t ** head, node_t ** prev, pid_t pid);
pid_t pop(node_t ** head);
pid_t drop_after(node_t ** head, node_t ** prev);
//pid_t remove_last(node_t * head);
//pid_t remove_by_index(node_t ** head, int n);
//int remove_by_value(node_t ** head, pid_t pid);
struct sched_attr * get_node(node_t * act);
struct sched_attr * get_next(node_t ** act);

#endif
