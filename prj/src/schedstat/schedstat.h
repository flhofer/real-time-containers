#include "orchdata.h"	// grobal memory structures and maniputlation functions

#ifndef __SCHEDSTAT_H_
	#define __SCHEDSTAT_H_

	#define PRGNAME "DC static orchestrator"
	
/* --------------------------- Global variables for all the threads and programms ------------------ */
	extern char * config; // filename of configuration file

	// configuration settings - program and pid/container`
	extern const parm_t * contparm; // read only container parameter settings
	extern prgset_t * prgset; // read only programm setings structure

	// runtime value tracing
	extern pthread_mutex_t dataMutex; // data access mutex
	extern node_t * head; // head of pidlist - PID runtime and configuration details

#endif
