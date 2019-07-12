#include "jsmn.h"

#ifndef __MANAGE_H_
	#define __MANAGE_H_

	// settings from schedstat.h
	// Parameters and runtime values ----- 
	// values set at startup in main thread, never changed there anymore
	extern char * config; // filename of configuration file
	extern prgset_t * prgset; // read only programm setings structure

	extern pthread_mutex_t dataMutex;
	extern node_t * head;

	void *thread_manage (void *arg); // thread that scans peridically for new entry pids
	#define JSMN_STRICT // force json conformance when parsing
	#define MAX_UL 0.90
	#define JSN_READ 65	// Json string value read length, must be longer than SIG_LEN
#endif
