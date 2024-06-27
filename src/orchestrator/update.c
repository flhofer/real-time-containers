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

#undef PFX
#define PFX "[update] "

// GLocal vars
char * pidSignature;

// declarations 
static void scanNew();
static void getCmdLinePids (node_t **pidlst);
static void (*pidUpdate)(node_t **) = getCmdLinePids; // default to PID command-line

/*
 *  cmpPidItem(): compares two pidlist items for Qsort
 *
 *  Arguments: pointers to the items to check
 *
 *  Return value: difference PID
 */
static int
cmpPidItem (const void * a, const void * b) {
	return (((node_t *)b)->pid - ((node_t *)a)->pid);
}

/*
 *  getContPids(): utility function to get PID list of interest from CGroups
 *
 *  Arguments: - pointer to linked list storing newly found PIDs
 *
 *  Return value: --
 */
static void
getContPids (node_t **pidlst)
{
	struct dirent *dir;
	DIR *d = opendir(prgset->cpusetdfileprefix);
	if (d) {
		char *fname = NULL;
		char buf[BUFRD];	// read buffer
		char *pid, *pid_ptr;	// strtok_r variables

		printDbg(PFX "Container detection!\n");

		while ((dir = readdir(d)) != NULL) {
			// scan trough docker CGroups, find them?
			if ((strlen(dir->d_name)>60)) {// container strings are very long! - KISS as it has to be efficient
				if ((fname=realloc(fname,strlen(prgset->cpusetdfileprefix)+strlen(dir->d_name)+strlen("/" CGRP_PIDS)+1))) {
					// copy to new prefix
					fname = strcat(strcpy(fname,prgset->cpusetdfileprefix),dir->d_name);
					fname = strcat(fname,"/" CGRP_PIDS);

					// prepare literal and open pipe request
					int fd = open(fname, O_RDONLY);

					int nleft = 0;	// number of bytes left to parse
					int	ret;		// return value number of bytes read, or error code
					int count = 0;  // number of PIDS

					// Scan through string and put in array
					while((ret = read(fd, buf+nleft,BUFRD-nleft-1))) {

						if (0 > ret){ // error..
							if (EINTR == errno) // retry on interrupt
								continue;
							warn("kernel tasks read error!");
							break;
						}

						// read success, update bytes to parse
						nleft += ret;

						printDbg(PIN "PID string return %s\n", buf);

						buf[nleft] = '\0'; // end of read check, nleft = max BUFRD-1;
						pid = strtok_r (buf,"\n", &pid_ptr);	

						printDbg(PIN "processing"); // Begin line
						while (NULL != pid && nleft && (6 < (&buf[BUFRD-1]-pid))) { // <6 = 5 pid no + \n
							// DO STUFF

							node_push(pidlst);
							// PID found
							(*pidlst)->pid = atoi(pid);
							(*pidlst)->status |= MSK_STATSIBL;
							printDbg("->%d ",(*pidlst)->pid);	// mid line

							updatePidCmdline(*pidlst); // checks and updates..
#ifdef CGROUP2
							char * name = strdup(dir->d_name);
							char * tok, *hex;
							(void)strtok_r(name, "-", &tok);	// 'docker-' part unused
							hex = strtok_r(NULL, ".", &tok);// &hex pos read, result por to '.scope' set, but overwritten with result - pos unused
#else
							char * hex = dir->d_name;
#endif
							if (!(( (*pidlst)->contid = strdup(hex) )))
								err_exit("Could not allocate memory!");
#ifdef CGROUP2
							free(name);
#endif

							nleft -= strlen(pid)+1;
							pid = strtok_r (NULL,"\n", &pid_ptr);
							count++;
						}
						if (1 == count) // only 1 found, reset sibling flag
							(*pidlst)->status &= ~MSK_STATSIBL;

						printDbg("\n"); // end of line
						if (pid) // copy leftover chars to beginning of string buffer
							memcpy(buf, buf+BUFRD-nleft-1, nleft); 
					}
					close(fd);

				}
				else // FATAL, exit and execute atExit
					err_exit("Could not allocate memory!");
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

/*
 *  getPids(): utility function to get list of PID for DM_CMDLINE DM_CNTPID mode
 *
 *  Arguments: - pointer to linked list of PID
 * 			   - tag string containing the command signature to look for
 * 			   - parent PID if present to note it as sort of container-ID in
 *
 *  Return value: --
 */
static void
getPids (node_t **pidlst, char * tag, char * ppid)
{
	FILE *fp;

	{
		if (!tag) 
			err_exit("Process signature tag is a null pointer!");

		int tlen = strlen (tag) + 22;
		char req[tlen];
		
		// prepare literal and open pipe request, request spid (thread) ids
		// spid and pid coincide for main process
#ifdef BUSYBOX
		(void)sprintf (req,  "ps -o pid,ppid,comm %s", tag);
#else
		(void)sprintf (req,  "ps h -o spid,command %s", tag);
#endif
		if(!(fp = popen(req,"r")))
			return;
	}

	char pidline[BUFRD];
	char *pid, *pid_ptr;
	int count = 0;
	// Scan through string and put in array
	while(fgets(pidline,BUFRD,fp)) {
		printDbg(PIN "Pid string return %s", pidline); // needs no \n -> fgets
		pid = strtok_r (pidline," ", &pid_ptr);

		node_push(pidlst);
        (*pidlst)->pid = atoi(pid);
		if (ppid){
			(*pidlst)->status |= MSK_STATSIBL;
			(*pidlst)->contid=strdup(ppid);		// string containing the list of parent PIDs
		}

#ifdef BUSYBOX
		// if busybox, second is ppid
        pid = strtok_r (NULL, " ", &pid_ptr); // ppid here	
#endif
		// find command string and copy to new allocation
        pid = strtok_r (NULL, "\n", &pid_ptr); // end of line?
        printDbg(PIN "processing->%d cmd: %s\n",(*pidlst)->pid, pid);

		// add command string to pidlist
		if (!((*pidlst)->psig = strdup(pid))) // alloc memory for string
			// FATAL, exit and execute atExit
			err_exit("Could not allocate memory!");
		(*pidlst)->contid = NULL;
		count++;
    }
	if (1 == count) // only 1 found, reset sibling flag
		(*pidlst)->status &= ~MSK_STATSIBL;

	pclose(fp);
}

/*
 *  getCmdLinePids(): intermediary for PID-Sig scan update
 *
 *  Arguments: - pointer to linked list of PID
 *
 *  Return value: --
 */
static void
getCmdLinePids (node_t **pidlst)
{
	// call function with computed signature at start
	getPids(pidlst, pidSignature, NULL);
}
/*
 *  getParentPids(): utility function to get list of PID by PPID tag (DM_CNTPID mode)
 *
 *  Arguments: - pointer to linked list of new PIDs
 *
 *  Return value: --
 */
static void
getParentPids (node_t **pidlst)
{
	char pidline[BUFRD-18];

	if (!prgset->cont_ppidc)
		err_exit("Process signature tag is a null pointer!");

	int tlen = strlen (prgset->cont_ppidc) + 7;
	char req[tlen];

	// prepare literal and open pipe request
	(void)sprintf (req,  "pidof %s", prgset->cont_ppidc);
	FILE *fp;

	if(!(fp = popen(req,"r")))
		return;

	// read list of PPIDs
	if (fgets(pidline,BUFRD-18,fp)) { // len -10 (+\n), limit maximum (see below)
		int i=0;
		// replace space with, for PID list
		while (pidline[i] && i<BUFRD) {
			if (' ' == pidline[i]) 
#ifdef BUSYBOX
				pidline[i]='|';	// use pipe for regex in grep
#else
				pidline[i]=','; // use comma for separated ppid list in standard ps
#endif
			i++;
		}

#ifdef BUSYBOX
		char pids[BUFRD];
		(void)sprintf(pids, "-T | grep -E '%s'", pidline); // len = 17, sum = total buffer
#else
		char pids[BUFRD] = "-T --ppid "; // len = 10, sum = total buffer
		(void)strcat(pids, pidline);
#endif
		pids[strlen(pids)-1]='\0'; // just to be sure.. terminate with null-char, overwrite \n

		getPids(pidlst, pids, pidline);
	}
	pclose(fp);
}

static contevent_t * lstevent;
pthread_t thread_dlink;
int  iret_dlink; // Timeout is set to 4 seconds by default

/*
 *  startDockerThread(): start docker verification thread
 *
 *  Arguments:
 *
 *  Return value: result of pthread_create, negative if failed
 */
static int
startDockerThread() {
	iret_dlink = pthread_create( &thread_dlink, NULL, dlink_thread_watch, NULL);
	if (iret_dlink)  // thread not started successfully
		err_msg_n(iret_dlink, "Failed to start docker_link thread");
#ifdef DEBUG
	else
		(void)pthread_setname_np(thread_dlink, "docker_link");
#endif
	return iret_dlink;
}

/*
 *  stopDockerThread(): stop docker verification thread
 *
 *  Arguments:
 *
 *  Return value: result of pthread_*, negative if one failed
 */
static int
stopDockerThread(){
	// set stop signal
	int ret = 0;
	if (!iret_dlink) { // thread started successfully
		int * dlink_return;
		if ((iret_dlink = pthread_kill (thread_dlink, SIGHUP))) // tell linking threads to stop
			err_msg_n(iret_dlink, "Failed to send signal to docker_link thread");
		ret |= iret_dlink;
		if ((iret_dlink = pthread_join (thread_dlink, (void**)&dlink_return))) // wait until end
			err_msg_n(iret_dlink, "Could not join with docker_link thread");
		ret |= iret_dlink;
		(void)printf(PFX "Threads stopped\n");
	}
	return ret;
}

/*
 *  updateDocker(): pull event from dockerlink and verify
 *
 *  Arguments:
 *
 *  Return value:
 */
static void
updateDocker() {
	
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

				// setPidResources calls findPidParameters with write access to configuration - but lock may not be needed
				(void)pthread_mutex_lock(&dataMutex);
				setPidResources(linked);
				(void)pthread_mutex_unlock(&dataMutex);

				free(linked->param);
				linked->param = NULL;

				node_pop(&linked);
				break;

			case cnt_remove: ;
				(void)pthread_mutex_lock(&dataMutex);
				printDbg(PFX "Container removal posted for %.12s", lstevent->id);
				node_t dummy = {nhead};
				node_t * curr = &dummy;

				// drop matching PIDs of this container
				while (((curr->next))){
					if (curr->next->contid == lstevent->id){
						if (prgset->trackpids)		// deactivate only
							curr->next->pid = abs(curr->next->pid) * -1;
						else {
							node_pop(&curr->next);
							continue; // don't move on to next item
						}
					}
					curr=curr->next;
				}
				(void)pthread_mutex_unlock(&dataMutex);
				break;

			default:
			case cnt_pending:
				// clear last event, do nothing
				free(lstevent);
				break;
		}
	}

	// scan for PID updates
	scanNew(); 
}

/*
 *  scanNew(): main function for thread_update, scans for PIDs and inserts
 *  or drops the PID list
 *
 *  Arguments:
 *
 *  Return value:
 */
static void
scanNew () {
	printDbg(PFX "Scanning for new PIDs:\n");

	// get PIDs 
	int wasEmpty = (!nhead);
	node_t *lnew = NULL; // pointer to new list head

	// call function for PID update, dependent on mode (Set at start)
	pidUpdate(&lnew);

	// SPIDs arrive out of order
	qsortll((void **)&lnew, cmpPidItem);

#ifdef DEBUG
	printDbg(PFX "Result update pid: ");
	for (node_t * curr = lnew; ((curr)); curr=curr->next)
		printDbg("%d ", curr->pid);
	printDbg("\n");
#endif

	printDbg(PFX "Entering node update\n");
	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	node_t	dummy = { nhead }; // dummy placeholder for head list
	node_t	*tail = &dummy;	  // pointer to tail element

	while ((NULL != tail->next) && (NULL != lnew)) { // go as long as both have elements

		// insert a missing item		
		if ((lnew->pid > abs(tail->next->pid))
				|| ((prgset->trackpids) && (tail->next->pid * -1 == lnew->pid))){

			if ((prgset->trackpids) && (abs(tail->next->pid) == lnew->pid)) {
				printDbg(PIN "... Dropping deactivated PID %d\n", lnew->pid);

				node_pop(&tail->next);
				nhead = dummy.next;
			}

			printDbg(PIN "... Insert new PID %d\n", lnew->pid);

			node_t * tmp = tail->next;
			tail->next = lnew;

			setPidResources(lnew); // find match and set resources

			// skip to next node, then overwrite added next ref
			lnew = lnew->next;
			tail = tail->next;

			tail->next=tmp; // append rest of list after new item
			continue;
		} 

		// delete a dopped item
		if (lnew->pid < abs(tail->next->pid)) {
			// skip deactivated tracking items
			if (0 > tail->next->pid){
				tail=tail->next;
				continue; 
			}

			printDbg(PIN "... Delete %d\n", tail->next->pid);
			if (prgset->trackpids){ // deactivate only
				tail->next->pid*=-1;
				tail = tail->next;
			}
			else{
				node_pop(&tail->next);
				nhead = dummy.next;
			}
			continue;
		} 

		// ok, they're equal, skip to next
		printDbg(PIN "... No change\n");
		// free allocated items, no longer needed
		node_pop(&lnew);
		tail = tail->next;
	}

	while (NULL != tail->next) { // reached the end of the pid queue -- drop list end
		// drop missing items
		printDbg(PIN "... Delete at end %d\n", tail->next->pid);// tail->next->pid);
		// get next item, then drop old
		if (prgset->trackpids){// deactivate only
			tail->next->pid = abs(tail->next->pid)*-1;
			tail = tail->next;
		}
		else
			node_pop(&tail->next);
	}

	// Reconstruct head after cleanup!
	nhead = dummy.next;

	if (NULL != lnew) { // reached the end of the actual queue -- insert to list end
		printDbg(PIN "... Insert at end, starting from PID %d - on\n", lnew->pid);
		tail->next = lnew;
		while (tail->next){
			setPidResources(tail->next); // find match and set resources
			tail=tail->next;
		}
	}

	// Restore once done
	nhead = dummy.next;

	// unlock data thread
	(void)pthread_mutex_unlock(&dataMutex);

	printDbg(PFX "Exiting node update\n");

#ifdef DEBUG
	printDbg(PFX "Active node list: ");
	for (node_t * curr = nhead; ((curr)); curr=curr->next)
		printDbg("%d ", curr->pid);
	printDbg("\n");
#endif

	// docker bypass problem of cpuset.cpu reset if no container is set
	if ((!nhead) && (!wasEmpty))
		resetContCGroups(prgset, prgset->affinity, prgset->numa);

	if ((nhead) && (wasEmpty))
		setContCGroups(prgset, 0);
}

/*
 *  selectUpdate(): select function and generate signature for update
 *
 *  Arguments:
 *
 *  Return value:
 */
static void
selectUpdate () {
	switch (prgset->use_cgroup) {

		case DM_CGRP: // detect by CGroup
			pidUpdate = getContPids;
			break;

		case DM_CNTPID: // detect by container shim pid
			pidUpdate = getParentPids;
			break;

		case DM_CMDLINE:
		default: ;// detect by pid signature

			// Generate PID signature to look for!
			pidSignature = malloc(CMD_LEN);
			pidSignature[0] = '\0';

	#ifdef BUSYBOX
			if (prgset->psigscan && strlen (prgset->cont_pidc))
				(void)sprintf(pidSignature, "-T | grep -E '%s'", prgset->cont_pidc);
			else if (strlen (prgset->cont_pidc))
				(void)sprintf(pidSignature, "| grep -E '%s'", prgset->cont_pidc);
			else
				(void)sprintf(pidSignature, "| grep -v '^PID'");
	#else
			if (prgset->psigscan && strlen (prgset->cont_pidc))
				(void)sprintf(pidSignature, "-TC %s", prgset->cont_pidc);
			else if (strlen (prgset->cont_pidc))
				(void)sprintf(pidSignature, "-C %s", prgset->cont_pidc);
	#endif
			pidUpdate = getCmdLinePids;

			pidSignature = realloc(pidSignature, strlen(pidSignature)+1);
			break;
	}
}


/*
 *  setThreadParameters(): set thread runtime parametes as of prgset
 *
 *  Arguments:
 *
 *  Return value:
 */
static void
setThreadParameters () {
	// setup of thread scheduling and priority
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

			if (sched_setattr(gettid(), &scheda, 0L)) {	// Custom function!
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

			if (sched_setscheduler(gettid(), prgset->policy, &schedp)) {
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

		if (sched_setscheduler(gettid(), prgset->policy, &schedp)) {
			warn("Could not set thread policy!");
		}
		else
			cont("set update thread to '%s', niceness %d.",
				policy_to_string(prgset->policy), prgset->priority);
	}
}

/*
 *  thread_update(): thread function call to manage and update present pids list
 *
 *  Arguments: - thread state/state machine, passed on to allow main thread stop
 *
 *  Return value: -
 */
void *
thread_update (void *arg)
{
	int32_t* pthread_state = (int32_t *)arg;
	int cc = 0, ret, stop = 0, dlink_on = 0;
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

	// select update function for scan mode
	selectUpdate();

	setThreadParameters();

	// initialize the thread locals
	while(1) {

		switch( *pthread_state )
		{
		case 0:

			*pthread_state=-1; // must be first thing! -> main writes -1 to stop

			// Read clock tick rate
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

			// start docker link thread
			dlink_on = (0 == startDockerThread());

			// set local variable -- all CPUs set.
			*pthread_state=1;
			//no break

		case 1: // normal thread loop
			if (dlink_on)
				updateDocker();
			if (cc)
				break;
			// update, once every td
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
			if (dlink_on)
				if (stopDockerThread())
					warn("Unable to stop the Docker link thread");
			//no break

		case -99:
			// exit
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
	// Start using return value
	return NULL;
}

