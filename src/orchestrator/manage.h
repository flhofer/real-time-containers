#ifndef __MANAGE_H_
	#define __MANAGE_H_

	// for debug and testing purposes, export
	void *thread_ftrace(void *arg);

	void *thread_manage (void *arg); // thread that scans peridically for new entry pids

#endif
