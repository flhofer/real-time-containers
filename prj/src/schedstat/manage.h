#include "jsmn.h"

#ifndef __MANAGE_H_
	#define __MANAGE_H_

	// settings from schedstat.h
	// Parameters and runtime values ----- 
	// values set at startup in main thread, never changed there anymore
	extern char * config; // filename of configuration file
	extern int affother; // set affinity of all pids in container?
	extern struct bitmask *affinity_mask; // default bitmask allocation of threads!!
	extern int quiet;	// quiet enabled
	extern int use_cgroup; // processes identificatiom mode, written before startup of thread
	extern char * cpusetdfileprefix; // file prefix for Docker's Cgroups, default = [CGROUP/]docker/

	extern pthread_mutex_t dataMutex;
	extern node_t * head;

	void *thread_manage (void *arg); // thread that scans peridically for new entry pids
	#define JSMN_STRICT // force json conformance when parsing
	#define MAX_UL 0.90
	#define JSN_READ 65	// Json string value read length, must be longer than SIG_LEN
#endif
