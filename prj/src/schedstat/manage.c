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

#include <sys/resource.h>
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

static cpu_set_t cset_full; // local static to avoid recomputation.. (may also use affinity_mask? )

/// updateSched(): main function called to verify status of threads
//
/// Arguments: 
///
/// Return value: N/D
///
int updateSched() {

    node_t * current = head;
	int ovr = 0;
	cpu_set_t cset;

	(void)pthread_mutex_lock(&dataMutex);

	while (NULL != current) {
		// skip deactivated tracking items
		if (current->pid<0){
			current=current->next; 
			continue;
		}

		// NEW Entry? Params are not assigned yet. Do it now and reschedule.
		if (NULL == current->param) {
			// params unassigned
			if (!prgset->quiet)
				(void)printf("\n");
			info("new pid in list %d", current->pid);

			if (!node_findParams(current, contparm)) { // parameter set found in list -> assign and update
				// precompute affinity
				if (0 <= current->param->rscs->affinity) {
					// cpu affinity defined to one cpu?
					CPU_ZERO(&cset);
					CPU_SET(current->param->rscs->affinity & ~(SCHED_FAFMSK), &cset);
				}
				else {
					// cpu affinity to all
					cset = cset_full;
				}

				if (SCHED_OTHER != current->attr.sched_policy) { 
					// only if successful
					if (!current->psig) 
						current->psig = current->param->psig;
					if (!current->contid)
						current->contid = current->param->cont->contid;

					// TODO: track failed scheduling update?

					// update CGroup setting of container if in CGROUP mode
					if (DM_CGRP == prgset->use_cgroup && 
						((0 <= current->param->rscs->affinity) & ~(SCHED_FAFMSK))) {

						char *contp = NULL;
						char affinity[5];
						(void)sprintf(affinity, "%d", current->param->rscs->affinity);

						cont( "reassigning %.12s's CGroups CPU's to %s", current->contid, affinity);
						if ((contp=malloc(strlen(prgset->cpusetdfileprefix))
								+ strlen(current->contid)+1)) {
							contp[0] = '\0';   // ensures the memory is an empty string
							// copy to new prefix
							contp = strcat(strcat(contp,prgset->cpusetdfileprefix), current->contid);		
							
							if (setkernvar(contp, "/cpuset.cpus", affinity)){
								warn("Can not set cpu-affinity");
							}
						}
						else 
							warn("malloc failed!");

						free (contp);
					}
					// should it be else??
					else {

						// add pid to docker CGroup
						char pid[5];
						(void)sprintf(pid, "%d", current->pid);

						if (setkernvar(prgset->cpusetdfileprefix , "tasks", pid)){
							printDbg( KMAG "Warn!" KNRM " Can not move task %s\n", pid);
						}

						// Set affinity
						if (sched_setaffinity(current->pid, sizeof(cset), &cset ))
							err_msg_n(errno,"setting affinity for PID %d",
								current->pid);
						else
							cont("PID %d reassigned to CPU%d", current->pid, 
								current->param->rscs->affinity);
					}

					// only do if different than -1, <- not set values
					if (SCHED_NODATA != current->param->attr->sched_policy) {
						cont("Setting Scheduler of PID %d to '%s'", current->pid,
							policy_to_string(current->param->attr->sched_policy));
						if (sched_setattr (current->pid, current->param->attr, 0U))
							err_msg_n(errno, "setting attributes for PID %d",
								current->pid);
					}
					else
						cont("Skipping setting of scheduler for PID %d", current->pid);  


					// controlling resource limits
          			struct rlimit rlim;		
					// TODO: upgrade to a list of parameters, looping through.			

					// RT-Time limit
					if (-1 != current->param->rscs->rt_timew || -1 != current->param->rscs->rt_time) {
						if (prlimit(current->pid, RLIMIT_RTTIME, NULL, &rlim))
							err_msg_n(errno, "getting RT-Limit for PID %d",
								current->pid);
						else {
							if (-1 != current->param->rscs->rt_timew)
								rlim.rlim_cur = current->param->rscs->rt_timew;
							if (-1 != current->param->rscs->rt_time)
								rlim.rlim_max = current->param->rscs->rt_time;
							if (prlimit(current->pid, RLIMIT_RTTIME, &rlim, NULL ))
								err_msg_n(errno,"setting RT-Limit for PID %d",
									current->pid);
							else
								cont("PID %d RT-Limit set to %d-%d", current->pid, 											rlim.rlim_cur, rlim.rlim_max);
						}
					}

					// Data limit - Heap.. unitialized or not
					if (-1 != current->param->rscs->mem_dataw || -1 != current->param->rscs->mem_data) {
						if (prlimit(current->pid, RLIMIT_DATA, NULL, &rlim))
							err_msg_n(errno, "getting Data-Limit for PID %d",
								current->pid);
						else {
							if (-1 != current->param->rscs->mem_dataw)
								rlim.rlim_cur = current->param->rscs->mem_dataw;
							if (-1 != current->param->rscs->mem_data)
								rlim.rlim_max = current->param->rscs->mem_data;
							if (prlimit(current->pid, RLIMIT_DATA, &rlim, NULL ))
								err_msg_n(errno, "setting Data-Limit for PID %d",
									current->pid);
							else
								cont("PID %d Data-Limit set to %d-%d", current->pid, 											rlim.rlim_cur, rlim.rlim_max);
						}
					}

				}
				else if (prgset->affother) {
					if (sched_setaffinity(current->pid, sizeof(cset), &cset ))
						err_msg_n(errno, "setting affinity for PID %d",
							current->pid);
					else
						cont("non-RT PID %d reassigned to CPU%d", current->pid,
							current->param->rscs->affinity);
				}
				else
					cont("Skipping non-RT PID %d from rescheduling", current->pid);
			}
		}

		// TODO: check if there is some faulty behaviour
//		if (current->mon->dl_overrun)
//			ovr++;

		current = current->next;

	}
	(void)pthread_mutex_unlock(&dataMutex);
	return ovr;
}

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
			rpush ( &rhead);
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
		// TODO: adapt to cpu mask
		for (int i=0; i<sizeof(cset_full); CPU_SET(i,&cset_full) ,i++);
	  case 1: // normal thread loop, check and update data
		if (!updateSched())
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

