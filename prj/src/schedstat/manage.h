#include "jsmn.h"

#ifndef __MANAGE_H_
	#define __MANAGE_H_

	// settings from schedstat.h
	// Parameters and runtime values ----- 
	// values set at startup in main thread, never changed there anymore
	extern char * config; // filename of configuration file
	extern int affother; // set affinity of all pids in container?
	extern struct bitmask *affinity_mask; // default bitmask allocation of threads!!

	extern pthread_mutex_t dataMutex;
	extern node_t * head;

	struct resTracer { // resource tracers
		int32_t affinity; 		// exclusive cpu-num
		uint64_t usedPeriod;	// amount of cputime left..
		uint64_t basePeriod;	// if a common period is set, or least common multiplier
		// TODO: fill with other values, i.e. memory amounts ecc
		struct resTracer * next;
	};

	void *thread_manage (void *arg); // thread that scans peridically for new entry pids
	#define JSMN_STRICT // force json conformance when parsing

#endif
