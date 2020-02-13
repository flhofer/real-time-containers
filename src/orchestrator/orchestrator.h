#include "orchdata.h"	// grobal memory structures and maniputlation functions

#ifndef __SCHEDSTAT_H_
	#define __SCHEDSTAT_H_

	#define PRGNAME "DC (adaptive) static orchestrator"
	#ifndef VERSION
		#define VERSION "-- UNVERSIONED --"
	#endif
	
/* --------------------------- Global variables for all the threads and programms ------------------ */

	// configuration settings - program and pid/container`
	extern containers_t * contparm; // container parameter settings
	extern prgset_t * prgset; // program settings structure

	// runtime value tracing
	extern pthread_mutex_t dataMutex; // data access mutex
	extern node_t * head; // head of pidlist - PID runtime and configuration details

#endif
