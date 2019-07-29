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
#include "error.h"		// error and strerr print functions


// Should be needed only here
#include <limits.h>
#include <sys/vfs.h>

// localy globals variables used here ->
static long ticksps = 1; // get clock ticks per second (Hz)-> for stat readout
static uint64_t scount = 0; // total scan count
static int clocksources[] = {
	CLOCK_MONOTONIC,
	CLOCK_REALTIME,
	CLOCK_PROCESS_CPUTIME_ID,
	CLOCK_THREAD_CPUTIME_ID
};

// Included in kernel 4.13
#ifndef SCHED_FLAG_RECLAIM
	#define SCHED_FLAG_RECLAIM		0x02
#endif

// Included in kernel 4.16
#ifndef SCHED_FLAG_DL_OVERRUN
	#define SCHED_FLAG_DL_OVERRUN		0x04
#endif

#define USEC_PER_SEC		1000000
#define NSEC_PER_SEC		1000000000
#define TIMER_RELTIME		0
#define PIPE_BUFFER			4096

typedef struct pid_info {
	struct pid_info * next;
	pid_t pid;
	char * psig; 
	char * contid;
	char * imgid;
} pidinfo_t;


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
	return (((pidinfo_t *)b)->pid - ((pidinfo_t *)a)->pid);
}

/// dumpStats(): prints thread statistics to out
///
/// Arguments: -
///
/// Return value: -
static void dumpStats (){

	node_t * item = head;
	(void)printf( "\nStatistics for real-time SCHED_DEADLINE PIDs, %ld scans:"
					" (others are omitted)\n"
					"Average exponential with alpha=0.9\n\n"
					"PID - Cycle Overruns(total/found/fail) - avg rt (min/max) - sum diff (min/max/avg)\n"
			        "----------------------------------------------------------------------------------\n",
					scount );

	// for now does only a simple update count
	if (!item) {
		(void)printf("(no PIDs)\n");
	}
	while (item) {
		if (SCHED_DEADLINE == item->attr.sched_policy) 
		(void)printf("%5d%c: %ld(%ld/%ld/%ld) - %ld(%ld/%ld) - %ld(%ld/%ld/%ld)\n", 
			abs(item->pid), item->pid<0 ? '*' : ' ', 
			item->mon.dl_overrun, item->mon.dl_count+item->mon.dl_scanfail,
			item->mon.dl_count, item->mon.dl_scanfail, 
			item->mon.rt_avg, item->mon.rt_min, item->mon.rt_max,
			item->mon.dl_diff, item->mon.dl_diffmin, item->mon.dl_diffmax, item->mon.dl_diffavg);

		item=item->next; 
	}

}

/// get_sched_info(): get sched debug output info
///
/// Arguments: the node to get info for
///
/// Return value: error code, 0 = success
///
static int get_sched_info(node_t * item)
{
	char szFileName [_POSIX_PATH_MAX],
		szStatBuff [PIPE_BUFFER],
		ltag [80], // just tag of beginning, max lenght expected ~30 
    	*s;

	FILE *fp;

	sprintf (szFileName, "/proc/%u/sched", (unsigned) item->pid);

	if (-1 == access (szFileName, R_OK)) {
		return -1;
	} /** if **/


	if ((fp = fopen (szFileName, "r")) == NULL) {
		return -1;
	} /** IF_NULL **/

	// read output into buffer!
	if (0 >= fread (szStatBuff, sizeof(char), PIPE_BUFFER-1, fp)) {
		fclose (fp);
		return -1;
	}
	szStatBuff[PIPE_BUFFER-1] = '\0'; // safety first

	fclose (fp);

	int64_t num;
	int64_t diff = 0;
	int64_t ltrt = 0; // last seen runtime

	s = strtok (szStatBuff, "\n");
	while (NULL != s) {
		(void)sscanf(s,"%s %*c %ld", ltag, &num);
		if (strncasecmp(ltag, "dl.runtime", 4) == 0)	{
			// store last seen runtime
			ltrt = num;
			if (num != item->mon.dl_rt)
				item->mon.dl_count++;
		}
		if (strncasecmp(ltag, "dl.deadline", 4) == 0)	{
			if (0 == item->mon.dl_deadline) 
				item->mon.dl_deadline = num;
			else if (num != item->mon.dl_deadline) {
				// it's not, updated deadline found

				// calculate difference to last reading, should be 1 period
				diff = (int64_t)(num-item->mon.dl_deadline)-(int64_t)item->attr.sched_period;

				// difference is very close to multiple of period we might have a scan fail
				// in addition to the overshoot
				while (diff >= ((int64_t)item->attr.sched_period - TSCHS) ) { 
					item->mon.dl_scanfail++;
					diff -= (int64_t)item->attr.sched_period;
				}

				// overrun-GRUB handling statistics -- ?
				if (diff)  {
					item->mon.dl_overrun++;

					// usually: we have jitter but execution stays constant -> more than a slot?
					printDbg("\nPID %d Deadline overrun by %ldns, sum %ld\n",
						item->pid, diff, item->mon.dl_diff); 
				}

				item->mon.dl_diff += diff;
				item->mon.dl_diffmin = MIN (item->mon.dl_diffmin, diff);
				item->mon.dl_diffmax = MAX (item->mon.dl_diffmax, diff);

				// exponentially weighted moving average, alpha = 0.9
				item->mon.dl_diffavg = (item->mon.dl_diffavg * 9 + diff /* *1 */)/10;

				// runtime replenished - deadline changed: old value may be real RT ->
				// Works only if scan time < slack time 
				diff = (int64_t)item->attr.sched_runtime - item->mon.dl_rt;
				item->mon.rt_min = MIN (item->mon.rt_min, diff);
				item->mon.rt_max = MAX (item->mon.rt_max, diff);
				item->mon.rt_avg = (item->mon.rt_avg * 9 + diff /* *1 */)/10;

				item->mon.dl_deadline = num;
			}	

			// update last seen runtime
			item->mon.dl_rt = ltrt;
			break; // we're done reading
		}
		s = strtok (NULL, "\n");	
	}

  return 0;
}

/// updateStats(): update the real time statistics for all scheduled threads
/// -- used for monitoring purposes ---
///
/// Arguments: - 
///
/// Return value: number of PIDs found (total)
///
static int updateStats ()
{
	static int prot = 0; // pipe rotation animation
	static char const sp[4] = "/-\\|";

	prot = (prot+1) % 4;
	if (!prgset->quiet)	
		(void)printf("\b%c", sp[prot]);		
	fflush(stdout);

	// init head
	node_t * item = head;

	scount++; // increase scan-count
	// for now does only a simple update
	while (item != NULL) {
		// skip deactivated tracking items
		if (item->pid<0){
			item=item->next; 
			continue;
		}

		// update only when defaulting -> new entry
		if (SCHED_NODATA == item->attr.sched_policy) {
			if (sched_getattr (item->pid, &(item->attr), sizeof(struct sched_attr), 0U) != 0) {

				warn("Unable to read params for PID %d: %s", item->pid, strerror(errno));		
			}

			// set the flag for deadline notification if not enabled yet -- TEST
			if ((prgset->setdflag) && (SCHED_DEADLINE == item->attr.sched_policy) && (KV_416 <= prgset->kernelversion) && !(SCHED_FLAG_DL_OVERRUN == (item->attr.sched_flags & SCHED_FLAG_DL_OVERRUN))){

				cont("Set dl_overrun flag for PID %d", item->pid);		

				item->attr.sched_flags |= SCHED_FLAG_DL_OVERRUN;
				if (sched_setattr (item->pid, &item->attr, 0U))
					err_msg_n(errno, "Can not set overrun flag");
			
			} 
		}

		// get runtime value
		if (SCHED_DEADLINE == item->attr.sched_policy) {
			int ret;
			if ((ret = get_sched_info(item)) ) {
				err_msg ("reading thread debug details %d", ret);
			} 
		}

		item=item->next; 
	}

	return 0;
}

/// getContPids(): utility function to get PID list of interrest from Cgroups
/// Arguments: - pointer to array of PID
///
/// Return value: number of PIDs found (total)
///
static int getContPids (pidinfo_t **pidlst)
{
	struct dirent *dir;
	DIR *d = opendir(prgset->cpusetdfileprefix);
	if (d) {
		char *fname = NULL; // clear pointer again
		char pidline[BUFRD];
		char *pid;
		char kparam[15]; // pid+/cmdline read string
		int i =0;

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
							push((void **)pidlst, sizeof(pidinfo_t));
							// pid found
							(*pidlst)->pid = atoi(pid);
							printDbg("%d\n",(*pidlst)->pid);

							if (((*pidlst)->psig = malloc(MAXCMDLINE)) &&
								((*pidlst)->contid = strdup(dir->d_name))) { // alloc memory for strings

								(void)sprintf(kparam, "%d/cmdline", (*pidlst)->pid);
								if (!getkernvar("/proc/", kparam, (*pidlst)->psig, MAXCMDLINE)) // try to read cmdline of pid
									warn("can not read pid %d's command line", (*pidlst)->pid);

								(*pidlst)->psig=realloc((*pidlst)->psig, strlen((*pidlst)->psig)+1); // cut to exact (reduction = no issue)

							}
							else // FATAL, exit and execute atExit
								fatal("Could not allocate memory!");

							i++;

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

		return i;
	}
	else {
		warn("Can not open Docker CGroups - is the daemon still running?");
		cont( "switching to container PID detection mode");
		prgset->use_cgroup = DM_CNTPID; // switch to container pid detection mode
		return 0; // count of items found

	}

}

/// getpids(): utility function to get list of PID
/// Arguments: - pointer to array of PID
///			   - tag string containing the command signature to look for 
///
/// Return value: number of PIDs found (total)
///
static int getPids (pidinfo_t **pidlst, char * tag)
{
	FILE *fp;

	{
		char req[40]; // TODO: might overrun if signatures are too long
		if (30 < strlen (tag))
			err_exit("Signature string too long! (FIXME)");

		// prepare literal and open pipe request, request spid (thread) ids
		// spid and pid coincide for main process
		(void)sprintf (req,  "ps h -o spid,command %s", tag);

		if(!(fp = popen(req,"r")))
			return 0;
	}

	char pidline[BUFRD];
	char *pid;
	int i =0;
	// Scan through string and put in array
	while(fgets(pidline,BUFRD,fp)) {
		printDbg("Pid string return %s\n", pidline);
		pid = strtok (pidline," ");					

		push((void **)pidlst, sizeof(pidinfo_t));
        (*pidlst)->pid = atoi(pid);
        printDbg("%d",(*pidlst)->pid);

		// find command string and copy to new allocation
        pid = strtok (NULL, "\n"); // end of line?
        printDbg(" cmd: %s\n",pid);

		// add command string to pidlist
		if (!((*pidlst)->psig = strdup(pid))) // alloc memory for string
			// FATAL, exit and execute atExit
			fatal("Could not allocate memory!");
		(*pidlst)->contid = NULL;							

        i++;
    }

	pclose(fp);
	// return number of elements found
	return i;
}



/// getcPids(): utility function to get list of PID by PPID tag
/// Arguments: - pointer to array of PID
///			   - tag string containing the name of the parent pid to look for
///
/// Return value: number of PIDs found (total)
///
static int getpPids (pidinfo_t **pidlst, char * tag)
{
	char pidline[BUFRD];
	char req[40]; // TODO: might overrun if signatures are too long

	if (30 < strlen (tag))
		err_exit("Signature string too long! (FIXME)");

	// prepare literal and open pipe request
	(void)sprintf (req,  "pidof %s", tag);
	FILE *fp;

	if(!(fp = popen(req,"r")))
		return 0;

	int cnt2 = 0;
	// read list of PPIDs
	if (fgets(pidline,BUFRD-10,fp)) { // len -10 (+\n), limit maximum
		int i=0;
		while (pidline[i] && i<BUFRD) {
			if (' ' == pidline[i]) 
				pidline[i]=',';
			i++;
		}

		char pids[BUFRD] = "-T --ppid "; // len = 10, sum = total buffer
		(void)strcat(pids, pidline);
		pids[strlen(pids)-1]='\0'; // just to be sure.. terminate with nullchar, overwrite \n

		// replace space with, for PID list

		cnt2 = getPids(pidlst, pids );
	}
	pclose(fp);
	return cnt2;
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
	pidinfo_t *pidlst = NULL;
	int cnt = 0; // Count of found PID

	switch (prgset->use_cgroup) {

		case DM_CGRP: // detect by cgroup
			cnt = getContPids(&pidlst);
			break;

		case DM_CNTPID: // detect by container shim pid
			cnt = getpPids(&pidlst, prgset->cont_ppidc);
			break;

		default: ;// detect by pid signature
			// cmdline of own thread
			char pid[SIG_LEN];
			if (prgset->psigscan)
				sprintf(pid, "-TC %s", prgset->cont_pidc);
			else 
				sprintf(pid, "-C %s", prgset->cont_pidc);
			cnt = getPids(&pidlst, pid);
			break;		
	}

	// SPIDs arrive out of order
	// TODO Upgrade to push_sorted
	qsortll((void **)&pidlst, cmpPidItem);

#ifdef DEBUG
	for (pidinfo_t * curr = pidlst; ((curr)); curr=curr->next)
		printDbg("Result update pid %d\n", curr->pid);		
#endif

	node_t *act = head, *prev = NULL;
	printDbg("\nEntering node update");		

	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);
	while ((NULL != act) && (NULL != pidlst)) {
		// skip deactivated tracking items
		if (act->pid<0){
			act=act->next;
			continue; 
		}

		// insert a missing item		
		if (pidlst->pid > (act->pid)) {
			printDbg("\n... Insert new PID %d", pidlst->pid);		
			// insert, prev is upddated to the new element
			node_insert_after(&head, &prev, pidlst->pid, pidlst->psig, pidlst->contid);
			pop((void **)&pidlst);
		} 
		else		
		// delete a dopped item
		if (pidlst->pid < (act->pid)) {
			printDbg("\n... Delete %d", act->pid);		
			if (prgset->trackpids) // deactivate only
				act->pid*=-1;
			act = act->next;
			if (!prgset->trackpids)
				node_drop_after(&head, &prev);
		} 
		// ok, skip to next
		else {
			printDbg("\nNo change");		
			// free allocated items, no longer needed
			free(pidlst->psig);
			free(pidlst->contid);

			pop((void **)&pidlst);
			prev = act; // update prev 
			act = act->next;
		}
	}

	// has to be reversed in input
	while (NULL != pidlst) { // reached the end of the actual queue -- insert to list end
		printDbg("\n... Insert at end PID %d", pidlst->pid);		

		node_insert_after(&head, &prev, pidlst->pid, pidlst->psig, pidlst->contid);
		pop((void **)&pidlst);
		if (prev)
			prev = prev->next;
		else
			prev = head;
	}

	while (act != NULL) { // reached the end of the pid queue -- drop list end
		// drop missing items
		printDbg("\n... Delete at end %d", act->pid);// prev->next->pid);		
		// get next item, then drop old
		if (prgset->trackpids)// deactivate only
			act->pid = abs(act->pid)*-1;
		act = act->next;
		if (!prgset->trackpids)
			node_drop_after(&head, &prev);
	}

	// unlock data thread
	(void)pthread_mutex_unlock(&dataMutex);

	printDbg("\nExiting node update\n");	

#ifdef DEBUG
	for (node_t * curr = head; ((curr)); curr=curr->next)
		printDbg("Result list %d\n", curr->pid);		
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

		case 1: 
			// startup-refresh: this should be executed only once every td
			*pthread_state=2; // must be first thing! -> main writes -1 to stop
			scanNew(); 
			if (!prgset->quiet)	
				(void)printf("\rNode Stats update  ");		
		case 2: // normal thread loop
			if (!cc)
				*pthread_state=1; // must be first thing
			updateStats();
			break;
		case -1:
			*pthread_state=-2; // must be first thing! -> main writes -1 to stop
			if (!prgset->quiet)	
				(void)printf("\n");
			// tidy or whatever is necessary
			dumpStats();

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
	// TODO: Start using return value
	return EXIT_SUCCESS;
}

