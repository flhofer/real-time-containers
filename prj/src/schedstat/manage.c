#include "manage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // used for string parsing
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants

#include <sched.h>			// scheduler functions
#include <linux/sched.h>	// linux specific scheduling
#include <linux/types.h>	// data structure types, short names and linked list
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <fcntl.h>			// file control, new open/close functions
#include <dirent.h>			// dir enttry structure and expl
#include <errno.h>			// error numbers and strings

// Custmom includes
#include "schedstat.h"

#include "orchdata.h"	// memory structure to store information
#include "rt-utils.h"	// trace and other utils
#include "kernutil.h"	// generic kernel utilities
#include "error.h"		// error and strerr print functions

#include <sys/wait.h>
#include <sys/types.h>
#include <numa.h>			// numa node ident


// parameter tree linked list head, resource linked list head
static struct resTracer * rhead;

////// TEMP ---------------------------------------------

#define MAX_UL 0.90

/// checkUvalue(): verify if task fits into Utilization limits of a resource
///
/// Arguments: resource entry for this cpu, the attr structure of the task
///
/// Return value: 0 = ok, -1 = no space, 1 = ok but recalc base
int checkUvalue(struct resTracer * res, struct sched_attr * par) {
	uint64_t	base = res->basePeriod,
				used = res->usedPeriod;
	int rv = 0;
	
	if (base % par->sched_deadline != 0) {
		// realign periods
		uint64_t max_Value = MAX (base, par->sched_period);

		if (base % 1000 != 0 || par->sched_period % 1000 != 0)
			fatal("Nanosecond resolution periods not supported!");
			// temporary solution to avoid very long loops

		while(1) //Alway True
		{
			if(max_Value % base == 0 && max_Value % par->sched_period == 0) 
			{
				break;
			}
			max_Value+= 1000; // add a microsecond..
		}

		used *= max_Value/base;
		base = max_Value;	
		rv=1;
	}

	if (MAX_UL < (double)(used + par->sched_runtime * base/par->sched_period)/(double)(base) )
		rv = -1;

	return rv;
}

void addUvalue(struct resTracer * res, struct sched_attr * par) {
	if (res->basePeriod % par->sched_deadline != 0) {
		// realign periods
		uint64_t max_Value = MAX (res->basePeriod, par->sched_period);

		if (res->basePeriod % 1000 != 0 || par->sched_period % 1000 != 0)
			fatal("Nanosecond resolution periods not supported!");
			// temporary solution to avoid very long loops

		while(1) //Alway True
		{
			if(max_Value % res->basePeriod == 0 && max_Value % par->sched_period == 0) 
			{
				break;
			}
			max_Value+= 1000; // add a microsecond..
		}

		res->usedPeriod *= max_Value/res->basePeriod;
		res->basePeriod = max_Value;	

	}

	res->usedPeriod += par->sched_runtime * res->basePeriod/par->sched_period;
}

//////  END TEMP ---------------------------------------------


/// createResTracer(): create resource tracing memory elements
//
/// Arguments: 
///
/// Return value: N/D - int
///
int createResTracer(){
	// mask affinity and invert for system map / readout of smi of online CPUs
	for (int i=0;i<(prgset->affinity_mask->size);i++) 

		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by selected only
			push((void**)&rhead, sizeof(struct resTracer));
			rhead->affinity = i;
			rhead->basePeriod = 1;
	}		
	return 0;
}

/// manageSched(): main function called to reassign resources
///
/// Arguments: 
///
/// Return value: N/D - int
///
int manageSched(){

	// TODO: this is for the dynamic and adaptive scheduler only

    node_t * current = head;

	(void)pthread_mutex_lock(&dataMutex);

	while (current != NULL) {

        current = current->next;
    }


	(void)pthread_mutex_unlock(&dataMutex); 

	return 0;
}

/// thread_manage(): thread function call to manage schedule list
///
/// Arguments: - thread state/state machine, passed on to allow main thread stop
///
/// Return value: Exit Code - o for no error
void *thread_manage (void *arg)
{
	// be explicit!
	int32_t* pthread_state = (int32_t *)arg;
	// initialize the thread locals
	while(1)
	{
	  switch( *pthread_state )
	  {
	  case 0: // setup thread
		*pthread_state=1; // first thing
		// set lolcal variable -- all cpus set.
	  case 1: // normal thread loop, check and update data
//		if ()
			break;
	  case 2: //
		// update resources
		*pthread_state=1;
		(void)manageSched();
		break;

	  case -1:
		// tidy or whatever is necessary
		pthread_exit(0); // exit the thread signalling normal return
		break;
	  }
	  sleep(1);
	}
	// TODO: Start using return value
}

