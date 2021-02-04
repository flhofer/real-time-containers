#include "orchdata.h"	// grobal memory structures and maniputlation functions

#ifndef ORCHESTRATOR_H_
	#define ORCHESTRATOR_H_

	#define PRGNAME "PDO - Probabilistic dynamic orchestrator"
	#ifndef VERSION
		#define VERSION "-- UNVERSIONED --"
	#endif
	
/* --------------------------- Global variables for all the threads and programms ------------------ */

	// configuration settings - program and pid/container`
	extern containers_t * contparm; // container parameter settings
	extern prgset_t * prgset; // program settings structure

	// runtime value tracing
	extern pthread_mutex_t dataMutex; // data access mutex
	extern node_t * nhead; // head of pidlist - PID runtime and configuration details

	// resource allocation tracing
	extern pthread_mutex_t resMutex; // trace access mutex UNUSED for now
	extern resTracer_t * rHead;
#endif
