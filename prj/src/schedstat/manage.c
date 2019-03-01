#include "schedstat.h"
#include "manage.h"


/// updateSched(): main function called to verify running schedule
//
/// Arguments: 
///
/// Return value: N/D
///
int manageSched(){
	uint64_t cputimes[MAX_CPUS] = {}; 
	uint64_t cpuperiod[MAX_CPUS] = {}; 
	cpu_set_t cset;

	// zero cpu-set, static size set
	CPU_ZERO(&cset);
	CPU_SET(0, &cset);

	pthread_mutex_lock(&dataMutex);

    node_t * current = head;
	while (current != NULL) {
		// get schedule of new pids
		if (current->attr.size == 0) {
			struct timespec tt;
			
			int ret = sched_rr_get_interval(current->pid, &tt);
			printDbg("Schedule pid %d: %d %ld\n", current->pid, ret, tt.tv_nsec);

			ret = sched_getattr (current->pid, &(current->attr), sizeof(node_t), 0U);
			printDbg("Attr: %d %d\n", ret, current->attr.sched_policy);

			ret = sched_setaffinity(current->pid, sizeof(cset), &cset );
			if (ret == 0)
				printDbg("Pid %d reassigned to CPU0\n", current->pid);

			// TODO: ret value evaluation 
		}

		// affinity not set?? default is 0, affinity of system stuff

		// sum of cpu-times, affinity is only 1 cpu here
		cputimes[current->affinity] += current->attr.sched_deadline;
		cpuperiod[current->affinity] += current->attr.sched_deadline;

        current = current->next;
    }


	pthread_mutex_unlock(&dataMutex);
}

/// thread_manage(): thread function call to manage schedule list
///
/// Arguments: - thread state/state machine, passed on to allow main thread stop
///
/// Return value: Exit Code - o for no error - EXIT_SUCCESS
void *thread_manage (void *arg)
{
	int* pthread_state = arg;
	// initialize the thread locals
	while(1)
	{
	  switch( *pthread_state )
	  {
	  case 0: // normal thread loop
		manageSched();
		break;
	  case -1:
		// tidy or whatever is necessary
		pthread_exit(0); // exit the thread signalling normal return
		break;
	  case 1: //
		// do something special
		
		break;
	  }
	  sleep(1);
	}
}

