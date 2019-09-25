#include "update.h"

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
#include "dockerlink.h" // connection to docker runtime
#include "error.h"		// error and strerr print functions

// Should be needed only here
#include <limits.h>
#include <sys/resource.h>
#include <sys/vfs.h>

// localy globals variables used here ->
static long ticksps = 1; // get clock ticks per second (Hz)-> for stat readout
static int clocksources[] = {
	CLOCK_MONOTONIC,
	CLOCK_REALTIME,
	CLOCK_PROCESS_CPUTIME_ID,
	CLOCK_THREAD_CPUTIME_ID
};

#define USEC_PER_SEC		1000000
#define NSEC_PER_SEC		1000000000
#define TIMER_RELTIME		0

// Included in kernel 4.13
#ifndef SCHED_FLAG_RECLAIM
	#define SCHED_FLAG_RECLAIM		0x02
#endif

// for MUSL based systems
#ifndef RLIMIT_RTTIME
	#define RLIMIT_RTTIME 15
#endif
#ifndef pthread_yield
	#define pthread_yield sched_yield
#endif

/// tsnorm(): verifies timespec for boundaries + fixes it
///
/// Arguments: pointer to timespec to check
///
/// Return value: -
static inline void tsnorm(struct timespec *ts)
{
	while (ts->tv_nsec >= NSEC_PER_SEC) {
		ts->tv_nsec -= NSEC_PER_SEC;
		ts->tv_sec++;
	}
}

/// cmpPidItem(): compares two pidlist items for Qsort
///
/// Arguments: pointers to the items to check
///
/// Return value: difference PID
static int cmpPidItem (const void * a, const void * b) {
	return (((node_t *)b)->pid - ((node_t *)a)->pid);
}

/// setPidRlimit(): sets given resource limits
///
/// Arguments: - PID node
///			   - soft resource limit
///			   - hard resource limit
///			   - resource limit tag
///			   - resource limit string-name for prints
///
/// Return value: ---
static inline void setPidRlimit(pid_t pid, int32_t rls, int32_t rlh, int32_t type, char* name ) {

	struct rlimit rlim;		
	if (-1 != rls || -1 != rlh) {
		if (prlimit(pid, type, NULL, &rlim))
			err_msg_n(errno, "getting %s for PID %d", name,
				pid);
		else {
			if (-1 != rls)
				rlim.rlim_cur = rls;
			if (-1 != rlh)
				rlim.rlim_max = rlh;
			if (prlimit(pid, type, &rlim, NULL ))
				err_msg_n(errno,"setting %s for PID %d", name,
					pid);
			else
				cont("PID %d %s set to %d-%d", pid, name,
					rlim.rlim_cur, rlim.rlim_max);
		}
	}
} 

static cpu_set_t cset_full; // local static to avoid recomputation.. (may also use affinity_mask? )

// TODO: might need to move theese to manage

/// setPidResources(): set PID resources at first detection
/// Arguments: - pointer to PID item (node_t)
///
/// Return value: --
///
static void setPidResources(node_t * node) {

	cpu_set_t cset;

	// params unassigned
	if (!prgset->quiet)
		(void)printf("\n");
	info("new pid in list %d", node->pid);

	if (!node_findParams(node, contparm)) { // parameter set found in list -> assign and update
		// precompute affinity
		if (0 <= node->param->rscs->affinity) {
			// cpu affinity defined to one cpu?
			CPU_ZERO(&cset);
			CPU_SET(node->param->rscs->affinity & ~(SCHED_FAFMSK), &cset);
		}
		else {
			// cpu affinity to all
			cset = cset_full;
		}

		if (!node->psig) 
			node->psig = node->param->psig;
		// TODO: fix once containers are managed properly
		if (!node->contid && node->param->cont)
			node->contid = node->param->cont->contid;

		// TODO: track failed scheduling update?

		// TODO: change to consider multiple pids with different affinity
		// update CGroup setting of container if in CGROUP mode
		if (DM_CGRP == prgset->use_cgroup) {
			if (0 <= (node->param->rscs->affinity)) {

				char *contp = NULL;
				char affinity[5];
				(void)sprintf(affinity, "%d", node->param->rscs->affinity & ~(SCHED_FAFMSK));

				cont( "reassigning %.12s's CGroups CPU's to %s", node->contid, affinity);
				if ((contp=malloc(strlen(prgset->cpusetdfileprefix)	+ strlen(node->contid)+1))) {
					contp[0] = '\0';   // ensures the memory is an empty string
					// copy to new prefix
					contp = strcat(strcat(contp,prgset->cpusetdfileprefix), node->contid);		
					
					if (!setkernvar(contp, "/cpuset.cpus", affinity, prgset->dryrun)){
						warn("Can not set cpu-affinity");
					}
				}
				else 
					warn("malloc failed!");

				free (contp);
			}
		}
		// should it be else??
		else {

			// add pid to docker CGroup
			char pid[5];
			(void)sprintf(pid, "%d", node->pid);

			if (!setkernvar(prgset->cpusetdfileprefix , "tasks", pid, prgset->dryrun)){
				printDbg( KMAG "Warn!" KNRM " Can not move task %s\n", pid);
			}

			// Set affinity
			if (sched_setaffinity(node->pid, sizeof(cset), &cset ))
				err_msg_n(errno,"setting affinity for PID %d",
					node->pid);
			else
				cont("PID %d reassigned to CPU%d", node->pid, 
					node->param->rscs->affinity);
		}

		// only do if different than -1, <- not set values
		if (SCHED_NODATA != node->param->attr->sched_policy) {
			cont("Setting Scheduler of PID %d to '%s'", node->pid,
				policy_to_string(node->param->attr->sched_policy));
			if (sched_setattr (node->pid, node->param->attr, 0U))
				err_msg_n(errno, "setting attributes for PID %d",
					node->pid);
		}
		else
			cont("Skipping setting of scheduler for PID %d", node->pid);  

		// controlling resource limits
		setPidRlimit(node->pid, node->param->rscs->rt_timew,  node->param->rscs->rt_time,
			RLIMIT_RTTIME, "RT-Limit" );

		setPidRlimit(node->pid, node->param->rscs->mem_dataw,  node->param->rscs->mem_data,
			RLIMIT_DATA, "Data-Limit" );
	}
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
		char *pid;
		char kparam[15]; // pid+/cmdline read string

		printDbg( "\nContainer detection!\n");

		while ((dir = readdir(d)) != NULL) {
			// scan trough docker cgroups, find them?
			if ((strlen(dir->d_name)>60)) {// container strings are very long!
				if ((fname=realloc(fname,strlen(prgset->cpusetdfileprefix)+strlen(dir->d_name)+strlen("/tasks")+1))) {
					fname[0] = '\0';   // ensures the memory is an empty string
					// copy to new prefix
					fname = strcat(strcat(fname,prgset->cpusetdfileprefix),dir->d_name);
					fname = strcat(fname,"/tasks");

					// prepare literal and open pipe request
					pidline[BUFRD-1] = '\0'; // safety to avoid overrun
					int path = open(fname,O_RDONLY);

					// Scan through string and put in array
					int nleft = 0;
					while(nleft += read(path, pidline+nleft,BUFRD-nleft-1)) {  	// TODO: read vs fread
						printDbg("Pid string return %s\n", pidline);
						pidline[BUFRD-2] = '\n';  // end of read check, set\n to be sure to end strtok, not on \0
						pid = strtok (pidline,"\n");	
						while (pid != NULL && nleft && ( '\0' != pidline[BUFRD-2])) { // <6 = 5 pid no + \n
							node_push(pidlst);
							// pid found
							(*pidlst)->pid = atoi(pid);
							printDbg("%d\n",(*pidlst)->pid);
							(*pidlst)->det_mode = DM_CGRP;

							if (((*pidlst)->psig = malloc(MAXCMDLINE)) &&
								((*pidlst)->contid = strdup(dir->d_name))) {
								// alloc memory for strings

								(void)sprintf(kparam, "%d/cmdline", (*pidlst)->pid);
								if (!getkernvar("/proc/", kparam, (*pidlst)->psig, MAXCMDLINE))
									// try to read cmdline of pid
									warn("can not read pid %d's command line", (*pidlst)->pid);

								// cut to exact (reduction = no issue)
								(*pidlst)->psig=realloc((*pidlst)->psig, 
									strlen((*pidlst)->psig)+1);
							}
							else // FATAL, exit and execute atExit
								fatal("Could not allocate memory!");

							nleft -= strlen(pid)+1;
							pid = strtok (NULL,"\n");	
						}
						if (pid) // copy leftover chars to beginning of string buffer
							memcpy(pidline, pidline+BUFRD-nleft-2, nleft); 
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

/// getpids(): utility function to get list of PID
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
	char *pid;
	// Scan through string and put in array
	while(fgets(pidline,BUFRD,fp)) {
		printDbg("Pid string return %s\n", pidline);
		pid = strtok (pidline," ");					

		node_push(pidlst);
        (*pidlst)->pid = atoi(pid);
        printDbg("%d",(*pidlst)->pid);
		(*pidlst)->det_mode = mode;

		// find command string and copy to new allocation
        pid = strtok (NULL, "\n"); // end of line?
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
		pids[strlen(pids)-1]='\0'; // just to be sure.. terminate with nullchar, overwrite \n

		getPids(pidlst, pids, DM_CNTPID);
	}
	pclose(fp);
}

static contevent_t * lstevent;
pthread_t thread_dlink;
int  iret_dlink; // Timeout is set to 4 secs by default

/// startDocker(): start docker verification thread
///
/// Arguments: 
///
/// Return value: 
///
static void startDocker() {
	iret_dlink = pthread_create( &thread_dlink, NULL, thread_watch_docker, NULL);

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
		// process data, find pid entry

		switch (lstevent->event) {
			case cnt_add:
				// do nothing, call for pids
				// update container break
				//settings;
				;
				node_t * linked = NULL;
				node_push(&linked);
				linked->pid = 0; // impossible id -> sets value for cnt only
				linked->psig = lstevent->name; // used to store container name just fot find
				linked->contid = lstevent->id;
				linked->imgid = lstevent->image;

				free(lstevent);
				lstevent = NULL;
				setPidResources(linked);

				node_pop(&linked);
				break;

			case cnt_remove: ;
				(void)pthread_mutex_lock(&dataMutex);
				node_t dummy = {head};
				node_t * curr = &dummy;

				// drop matching pids of this container
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
}

/// scanNew(): main function for thread_update, scans for pids and inserts
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

		case DM_CGRP: // detect by cgroup
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
	for (node_t * curr = lnew; ((curr)); curr=curr->next)
		printDbg("Result update pid %d\n", curr->pid);		
#endif

	node_t	dummy = { head }; // dummy placeholder for head list
	node_t	*tail = &dummy;	  // pointer to tail element
	printDbg("\nEntering node update");		

	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);
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
	head = dummy.next;
	// unlock data thread
	(void)pthread_mutex_unlock(&dataMutex);

#ifdef DEBUG
	for (node_t * curr = head; ((curr)); curr=curr->next)
		printDbg("\nResult list %d\n", curr->pid);		
#endif

	printDbg("\nExiting node update\n");	
}

/// thread_update(): thread function call to manage and update present pids list
///
/// Arguments: - thread state/state machine, passed on to allow main thread stop
///
/// Return value: -
void *thread_update (void *arg)
{
	int32_t* pthread_state = (int32_t *)arg;
	int cc = 0, ret;
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

					// enable bandwith reclaim if supported, allow to reduce impact.. 
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
			startDocker();

			// set lolcal variable -- all cpus set.
			// TODO: adapt to cpu mask
			for (int i=0; i<sizeof(cset_full); CPU_SET(i,&cset_full) ,i++);

		case 1: 
			// startup-refresh: this should be executed only once every td
			*pthread_state=2; // must be first thing! -> main writes -1 to stop
			scanNew(); 
			if (!prgset->quiet)	
				(void)printf("\rNode Stats update  ");		
		case 2: // normal thread loop
			if (!cc)
				*pthread_state=1; // must be first thing
			updateDocker();
			break;
		case -1:
			*pthread_state=-2; // must be first thing! -> main writes -1 to stop
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

			break;
		case -2:
			// exit
			pthread_exit(0); // exit the thread signalling normal return
			break;
		}

		if (SCHED_DEADLINE == prgset->policy){
			// perfect sync with period here, allow replenish 
			if (pthread_yield()){
				warn("pthread_yield() failed. errno: %s",strerror (ret));
				*pthread_state=-1;
				break;				
			}

		}
		else {			
			// abs-time relative interval shift

			// calculate next execution intervall
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

		// we have a max runtime. Stop! -> after the clock_nanosleep time will be intervaltv
		if (prgset->runtime) {
			ret = clock_gettime(clocksources[prgset->clocksel], &now);
			if (0 != ret) {
				if (EINTR != ret)
					warn("clock_gettime() failed: %s", strerror(errno));
				*pthread_state=-1;
			}

			if (old.tv_sec + prgset->runtime <= now.tv_sec
				&& old.tv_nsec <= now.tv_nsec) 
				// set stop sig
				raise (SIGTERM); // tell main to stop
		}

		cc++;
		cc%=prgset->loops;
	}
	// set stop sig

	if (!iret_dlink) { // thread started successfully
		pthread_kill (thread_dlink, SIGINT); // tell linking threads to stop
		iret_dlink = pthread_join( thread_dlink, NULL); // wait until end
	}
	// TODO: Start using return value
	return EXIT_SUCCESS;
}

