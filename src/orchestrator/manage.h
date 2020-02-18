#ifndef __MANAGE_H_
#define __MANAGE_H_

#include <sys/types.h>

// for debug and testing purposes, export

// Linked list of CPU threads, public to allow external calls
struct ftrace_thread {
	struct ftrace_thread * next;
	pthread_t thread;	// thread information
	int iret;			// return value of thread launch
	int cpuno;			// CPU number monitored
	char * dbgfile;		// file pointer to the debug file. NULL == use default
};
void *thread_ftrace(void *arg);

void *thread_manage(void *arg); // thread that scans peridically for new entry pids

#endif
