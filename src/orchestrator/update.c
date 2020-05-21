#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // used for string parsing
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants

#include <sched.h>			// scheduler functions
#include <linux/sched.h>	// Linux specific scheduling
#include <linux/types.h>	// data structure types, short names and linked list
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <fcntl.h>			// file control, new open/close functions
#include <dirent.h>			// dir entry structure and expl
#include <errno.h>			// error numbers and strings
#include <time.h>			// constants and functions for clock

// Custom includes
#include "orchestrator.h"

#include "kernutil.h"	// generic kernel utilities
#include "dockerlink.h" // connection to docker runtime
#include "error.h"		// error and stderr print functions
#include "cmnutil.h"	// common definitions and functions
#include "resmgnt.h"	// resource management for PIDs and Containers

// locally globals variables used here ->
static long ticksps = 1; // get clock ticks per second (Hz)-> for stat readout
static int clocksources[] = {
	CLOCK_MONOTONIC,
	CLOCK_REALTIME,
	CLOCK_PROCESS_CPUTIME_ID,
	CLOCK_THREAD_CPUTIME_ID
};

#undef PFX
#define PFX "[update] "

// declarations 
static void scanNew();

/// cmpPidItem(): compares two pidlist items for Qsort
///
/// Arguments: pointers to the items to check
///
/// Return value: difference PID
static int cmpPidItem (const void * a, const void * b) {
	return (((node_t *)b)->pid - ((node_t *)a)->pid);
}

/// getContPids(): utility function to get PID list of interrest from Cgroups
/// Arguments: - pointer to linked list of PID
///
/// Return value: --
///
static void getContPids (node_t **pidlst)
{
	struct dirent *dir;
	DIR *d = opendir(prgset->cpusetdfileprefix);
	if (d) {
		char *fname = NULL; // clear pointer again
		char pidline[BUFRD];
		char *pid, *pid_ptr;

		printDbg( "\nContainer detection!\n");

		while ((dir = readdir(d)) != NULL) {
			// scan trough docker CGroups, find them?
			if ((strlen(dir->d_name)>60)) {// container strings are very long!
				if ((fname=realloc(fname,strlen(prgset->cpusetdfileprefix)+strlen(dir->d_name)+strlen("/tasks")+1))) {
					fname[0] = '\0';   // ensures the memory is an empty string
					// copy to new prefix
					fname = strcat(strcat(fname,prgset->cpusetdfileprefix),dir->d_name);
					fname = strcat(fname,"/tasks");

					// prepare literal and open pipe request
					int path = open(fname,O_RDONLY);

					// Scan through string and put in array
					int nleft = 0, got;
					while((got = read(path, pidline+nleft,BUFRD-nleft-1))) {
						if (0 > got){
							if (EINTR == errno) // retry on interrupt
								continue;
							warn("kernel tasks read error!");
							break;
						}
						nleft += got;
						printDbg("%s: PID string return %s\n", __func__, pidline);
						pidline[nleft] = '\0'; // end of read check, nleft = max 1023;
						pid = strtok_r (pidline,"\n", &pid_ptr);	
						printDbg("%s: processing ", __func__);
						while (NULL != pid && nleft && (6 < (&pidline[BUFRD-1]-pid))) { // <6 = 5 pid no + \n
							// DO STUFF

							node_push(pidlst);
							// PID found
							(*pidlst)->pid = atoi(pid);
							printDbg("->%d ",(*pidlst)->pid);
							(*pidlst)->det_mode = DM_CGRP;

							updatePidCmdline(*pidlst); // checks and updates..
							if (!(( (*pidlst)->contid = strdup(dir->d_name) )))
								fatal("Could not allocate memory!");

							nleft -= strlen(pid)+1;
							pid = strtok_r (NULL,"\n", &pid_ptr);	
						}
						printDbg("\n");
						if (pid) // copy leftover chars to beginning of string buffer
							memcpy(pidline, pidline+BUFRD-nleft-1, nleft); 
					}
					close(path);
				}
				else // FATAL, exit and execute atExit
					fatal("Could not allocate memory!");
			}
		}
		closedir(d);

		// free string buffers
		free (fname);

		return;
	}

	warn("Can not open Docker CGroups - is the daemon still running?");
	cont( "switching to container PID detection mode");
	prgset->use_cgroup = DM_CNTPID; // switch to container pid detection mode
}

/// getPids(): utility function to get list of PID
/// Arguments: - pointer to linked list of PID
///			   - tag string containing the command signature to look for 
///
/// Return value: --
///
static void getPids (node_t **pidlst, char * tag, int mode)
{
	FILE *fp;

	{
		if (!tag) 
			err_exit("Process signature tag is a null pointer!");

		int tlen = strlen (tag) + 21;
		char req[tlen];
		
		// prepare literal and open pipe request, request spid (thread) ids
		// spid and pid coincide for main process
		(void)sprintf (req,  "ps h -o spid,command %s", tag);

		if(!(fp = popen(req,"r")))
			return;
	}

	char pidline[BUFRD];
	char *pid, *pid_ptr;
	// Scan through string and put in array
	while(fgets(pidline,BUFRD,fp)) {
		printDbg("%s: Pid string return %s\n", __func__, pidline);
		pid = strtok_r (pidline," ", &pid_ptr);					

		node_push(pidlst);
        (*pidlst)->pid = atoi(pid);
        printDbg("processing->%d",(*pidlst)->pid);
		(*pidlst)->det_mode = mode;

		// find command string and copy to new allocation
        pid = strtok_r (NULL, "\n", &pid_ptr); // end of line?
        printDbg(" cmd: %s\n",pid);

		// add command string to pidlist
		if (!((*pidlst)->psig = strdup(pid))) // alloc memory for string
			// FATAL, exit and execute atExit
			fatal("Could not allocate memory!");
		(*pidlst)->contid = NULL;							
    }

	pclose(fp);
}

/// getcPids(): utility function to get list of PID by PPID tag
/// Arguments: - pointer to linked list of PID
///			   - tag string containing the name of the parent pid to look for
///
/// Return value: -- 
///
static void getpPids (node_t **pidlst, char * tag)
{
	char pidline[BUFRD];
	if (!tag) 
		err_exit("Process signature tag is a null pointer!");

	int tlen = strlen (tag) + 7;
	char req[tlen];

	// prepare literal and open pipe request
	(void)sprintf (req,  "pidof %s", tag);
	FILE *fp;

	if(!(fp = popen(req,"r")))
		return;

	// read list of PPIDs
	if (fgets(pidline,BUFRD-10,fp)) { // len -10 (+\n), limit maximum
		int i=0;
		// replace space with, for PID list
		while (pidline[i] && i<BUFRD) {
			if (' ' == pidline[i]) 
				pidline[i]=',';
			i++;
		}

		char pids[BUFRD] = "-T --ppid "; // len = 10, sum = total buffer
		(void)strcat(pids, pidline);
		pids[strlen(pids)-1]='\0'; // just to be sure.. terminate with null-char, overwrite \n

		getPids(pidlst, pids, DM_CNTPID);
	}
	pclose(fp);
}

static contevent_t * lstevent;
pthread_t thread_dlink;
int  iret_dlink; // Timeout is set to 4 seconds by default

/// startDockerThread(): start docker verification thread
///
/// Arguments: 
///
/// Return value: result of pthread_create, negative if failed
///
static int startDockerThread() {
	iret_dlink = pthread_create( &thread_dlink, NULL, dlink_thread_watch, NULL);
#ifdef DEBUG
	(void)pthread_setname_np(thread_dlink, "docker_link");
#endif
	return iret_dlink;
}

/// stopDockerThread(): stop docker verification thread
///
/// Arguments:
///
/// Return value: result of pthread_*, negative if one failed
///
static int stopDockerThread(){
	// set stop signal
	int ret = 0;
	if (!iret_dlink) { // thread started successfully
		if ((iret_dlink = pthread_kill (thread_dlink, SIGHUP))) // tell linking threads to stop
			perror("Failed to send signal to docker_link thread");
		ret |= iret_dlink;
		if ((iret_dlink = pthread_join (thread_dlink, NULL))) // wait until end
			perror("Could not join with docker_link thread");
		ret |= iret_dlink;
		(void)printf(PFX "Threads stopped\n");
	}
	return ret;
}

/// updateDocker(): pull event from dockerlink and verify
///
/// Arguments: 
///
/// Return value: 
///
static void updateDocker() {
	
	// NOTE: pointers are atomic
	if (!containerEvent)
		return;
	
	while (containerEvent) {	
		lstevent = containerEvent;
		containerEvent = NULL;
		// process data, find PID entry

		switch (lstevent->event) {
			case cnt_add:
				// do nothing, call for PIDs
				// update container break
				// settings;
				;
				node_t * linked = NULL;
				node_push(&linked);
				linked->pid = 0; // impossible id -> sets value for count only
				linked->psig = lstevent->name; // used to store container name just for find
				linked->contid = lstevent->id;
				linked->imgid = lstevent->image;

				free(lstevent);
				lstevent = NULL;
				setPidResources(linked);

				node_pop(&linked);
				break;

			case cnt_remove: ;
				(void)pthread_mutex_lock(&dataMutex);
				node_t dummy = {nhead};
				node_t * curr = &dummy;

				// drop matching PIDs of this container
				for (;((curr->next)); curr=curr->next) 
					if (curr->next->contid == lstevent->id) 
						node_pop(&curr->next);

				(void)pthread_mutex_unlock(&dataMutex);
				break;

			default:
			case cnt_pending:
				// clear last event, do nothing
				free(lstevent);
				break;
		}
	}

	// scan for PID updates // TODO: Selective update in scanNew if container mode on (less to do)
	// .. might be a problem if not matchin -> update comparison of list!!
	scanNew(); 
}

/// scanNew(): main function for thread_update, scans for PIDs and inserts
/// or drops the PID list
///
/// Arguments: 
///
/// Return value: 
///
static void scanNew () {
	// get PIDs 
	node_t *lnew = NULL; // pointer to new list head

	switch (prgset->use_cgroup) {

		case DM_CGRP: // detect by CGroup
			getContPids(&lnew);
			break;

		case DM_CNTPID: // detect by container shim pid
			getpPids(&lnew, prgset->cont_ppidc);
			break;

		default: ;// detect by pid signature
			// cmdline of own thread
			char pid[SIG_LEN];
			if (prgset->psigscan)
				sprintf(pid, "-TC %s", prgset->cont_pidc);
			else if (strlen (prgset->cont_pidc))
				sprintf(pid, "-C %s", prgset->cont_pidc);
			else 
				pid[0] = '\0';
			getPids(&lnew, pid, DM_CMDLINE);
			break;		
	}

	// SPIDs arrive out of order
	qsortll((void **)&lnew, cmpPidItem);

#ifdef DEBUG
	printDbg("\nResult update pid: ");
	for (node_t * curr = lnew; ((curr)); curr=curr->next)
		printDbg("%d ", curr->pid);
	printDbg("\n");
#endif

	printDbg("\nEntering node update");		
	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	node_t	dummy = { nhead }; // dummy placeholder for head list
	node_t	*tail = &dummy;	  // pointer to tail element

	while ((NULL != tail->next) && (NULL != lnew)) { // go as long as both have elements

		// insert a missing item		
		if (lnew->pid > abs(tail->next->pid)) {
			printDbg("\n... Insert new PID %d", lnew->pid);		

			node_t * tmp = tail->next;
			tail->next = lnew;

			setPidResources(lnew); // find match and set resources

			// skip to next node, then overwrite added next ref
			lnew = lnew->next;
			tail = tail->next;

			tail->next=tmp; // trunc of rest of list, point to new item
		} 
		else		
		// delete a dopped item
		if (lnew->pid < abs(tail->next->pid)) {
			// skip deactivated tracking items
			if (tail->next->pid<0){
				tail=tail->next;
				continue; 
			}

			printDbg("\n... Delete %d", tail->next->pid);		
			if (prgset->trackpids){ // deactivate only
				tail->next->pid*=-1;
				tail = tail->next;
			}
			else
				node_pop(&tail->next);
		} 
		// ok, they're equal, skip to next
		else {
			printDbg("\nNo change");		
			// free allocated items, no longer needed
			node_pop(&lnew);
			tail = tail->next;
		}
	}

	while (NULL != tail->next) { // reached the end of the pid queue -- drop list end
		// drop missing items
		printDbg("\n... Delete at end %d", tail->next->pid);// tail->next->pid);		
		// get next item, then drop old
		if (prgset->trackpids)// deactivate only
			tail->next->pid = abs(tail->next->pid)*-1;
		else
			node_pop(&tail->next);
	}

	if (NULL != lnew) { // reached the end of the actual queue -- insert to list end
		printDbg("\n... Insert at end, starting from PID %d - on\n", lnew->pid);		
		tail->next = lnew;
		while (tail->next){
			setPidResources(tail->next); // find match and set resources
			tail=tail->next;
		}
	}

	// 
	nhead = dummy.next;
	// unlock data thread
	(void)pthread_mutex_unlock(&dataMutex);

	printDbg("\nExiting node update\n");

#ifdef DEBUG
	printDbg("\nResult list: ");
	for (node_t * curr = nhead; ((curr)); curr=curr->next)
		printDbg("%d ", curr->pid);
	printDbg("\n");
#endif

}

/// thread_update(): thread function call to manage and update present pids list
///
/// Arguments: - thread state/state machine, passed on to allow main thread stop
///
/// Return value: -
void *thread_update (void *arg)
{
	int32_t* pthread_state = (int32_t *)arg;
	int cc = 0, ret, stop = 0;
	struct timespec intervaltv, now, old;

	// get clock, use it as a future reference for update time TIMER_ABS*
	ret = clock_gettime(clocksources[prgset->clocksel], &intervaltv);
	if (0 != ret) {
		if (EINTR != ret)
			warn("clock_gettime() failed: %s", strerror(errno));
		*pthread_state=-1;
	}
	old = intervaltv;

	if (prgset->runtime)
		cont("Runtime set to %d seconds", prgset->runtime);


	// initialize the thread locals
	while(1) {

		switch( *pthread_state )
		{
		case 0:			
			// setup of thread, configuration of scheduling and priority
			*pthread_state=-1; // must be first thing! -> main writes -1 to stop
			cont("Sample time set to %dus.", prgset->interval);
			// get jiffies per sec -> to ms
			ticksps = sysconf(_SC_CLK_TCK);
			if (1 > ticksps) { // must always be greater 0 
				warn("could not read clock tick config!");
				break;
			}
			else{ 
				// clock settings found -> check for validity
				cont("clock tick used for scheduler debug found to be %ldHz.", ticksps);
				if (500000/ticksps > prgset->interval)  
					warn("-- scan time more than double the debug update rate. On purpose?"
							" (obsolete kernel value) -- ");
			}
			if (SCHED_OTHER != prgset->policy && SCHED_IDLE != prgset->policy && SCHED_BATCH != prgset->policy) {
				// set policy to thread
				if (SCHED_DEADLINE == prgset->policy) {
					struct sched_attr scheda  = { 48, 
												SCHED_DEADLINE,
												SCHED_FLAG_RESET_ON_FORK,
												0,
												0,
												prgset->update_wcet*1000, prgset->interval*1000, prgset->interval*1000
												};

					// enable bandwidth reclaim if supported, allow to reduce impact..
					if (KV_413 <= prgset->kernelversion) 
						scheda.sched_flags |= SCHED_FLAG_RECLAIM;

					if (sched_setattr(0, &scheda, 0L)) {
						warn("Could not set thread policy!");
						// reset value -- not written in main anymore
						prgset->policy = SCHED_OTHER;
					}
					else
						cont("set update thread to '%s', runtime %dus.",
							policy_to_string(prgset->policy), prgset->update_wcet);

				}
				else {
					struct sched_param schedp  = { prgset->priority };

					if (sched_setscheduler(0, prgset->policy, &schedp)) {
						warn("Could not set thread policy!");
						// reset value -- not written in main anymore
						prgset->policy = SCHED_OTHER;
					}
					else
						cont("set update thread to '%s', priority %d.",
							policy_to_string(prgset->policy), prgset->priority);
				}
			}
			else if (SCHED_OTHER == prgset->policy && prgset->priority) {
				// SCHED_OTHER only, BATCH or IDLE are static to 0
				struct sched_param schedp  = { prgset->priority };

				if (sched_setscheduler(0, prgset->policy, &schedp)) {
					warn("Could not set thread policy!");
				}
				else 
					cont("set update thread to '%s', niceness %d.",
						policy_to_string(prgset->policy), prgset->priority);
			}

			// start docker link thread
			if (startDockerThread())
				warn("Unable to start the Docker link thread");

			// set local variable -- all CPUs set.
			*pthread_state=1;
			//no break

		case 1: // normal thread loop
			updateDocker();
			if (cc)
				break;
			// update, once every td
			if (!prgset->quiet)	
				(void)printf("\rNode Stats update  ");
			scanNew(); 

			break;
		case -1:
			*pthread_state=-2; // must be first thing! -> main writes -1 to stop
			(void)printf(PFX "Stopping\n");
			if (!prgset->quiet)	
				(void)printf("\n");
			// tidy or whatever is necessary

			// update time if not in runtime mode - has not been read yet
			if (!prgset->runtime) {
				ret = clock_gettime(clocksources[prgset->clocksel], &now);
				if (0 != ret) 
					if (EINTR != ret)
						warn("clock_gettime() failed: %s", strerror(errno));
			}

			// Done -> print total runtime, now updated every cycle
			info("Total test runtime is %ld seconds", now.tv_sec - old.tv_sec);

			//no break
		case -2:
			*pthread_state=-99; // must be first thing! -> main writes -1 to stop
			if (stopDockerThread())
				warn("Unable to stop the Docker link thread");
			//no break

		case -99:
			// exit
			//		pthread_exit(0); // exit the thread signaling normal return
			break;
		}

		// STOP Loop?
		if (-99 == *pthread_state)
			break;

		// If not, which timer?
		if (SCHED_DEADLINE == prgset->policy){
			// perfect sync with period here, allow replenish 
			if (pthread_yield()){
				warn("pthread_yield() failed. errno: %s",strerror (ret));
				*pthread_state=-1;
				break;				
			}

		}
		else {			
			// absolute time relative interval shift

			// calculate next execution interval
			intervaltv.tv_sec += prgset->interval / USEC_PER_SEC;
			intervaltv.tv_nsec+= (prgset->interval % USEC_PER_SEC) * 1000;
			tsnorm(&intervaltv);

			// sleep for interval nanoseconds
			ret = clock_nanosleep(clocksources[prgset->clocksel], TIMER_ABSTIME, &intervaltv, NULL);
			if (0 != ret) {
				// Set warning only.. shouldn't stop working
				// probably overrun, restarts immediately in attempt to catch up
				if (EINTR != ret) {
					warn("clock_nanosleep() failed. errno: %s",strerror (ret));
				}
			}
		}

		// we have a max runtime. Stop! -> after the clock_nanosleep time will be `intervaltv`
		if (prgset->runtime) {
			ret = clock_gettime(clocksources[prgset->clocksel], &now);
			if (0 != ret) {
				if (EINTR != ret)
					warn("clock_gettime() failed: %s", strerror(errno));
				*pthread_state=-1;
			}

			if (!stop && old.tv_sec + prgset->runtime <= now.tv_sec
				&& old.tv_nsec <= now.tv_nsec) {
				// set stop signal
				raise (SIGTERM); // tell main to stop
				stop = 1;
			}
		}

		cc++;
		cc%=prgset->loops;
	}

	(void)printf(PFX "Stopped\n");
	// TODO: Start using return value
	return NULL;
}

