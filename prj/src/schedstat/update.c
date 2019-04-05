#include "schedstat.h"
#include "update.h"
#include "manage.h"

// test added
#include <limits.h>
//#include <sys/stat.h>
#include <sys/vfs.h>

#include <errno.h> // TODO: fix as general

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
#define PID_BUFFER			4096

int hallo = SCHED_FLAG_DL_OVERRUN;

static int clocksources[] = {
	CLOCK_MONOTONIC,
	CLOCK_REALTIME,
	CLOCK_PROCESS_CPUTIME_ID,
	CLOCK_THREAD_CPUTIME_ID
};

//TODO: unify constants
// Parameters and runtime values ----- 
// values set at startup in main thread, never changed there anymore
extern int use_cgroup; // processes identificatiom mode, written before startup of thread
extern int interval; // settting, default to SCAN
extern int update_wcet; // worst case execution time for deadline scheduled task
extern int loops; // once every x interval the containers are checked again
extern int priority; // priority for eventual RT policy
extern int policy;	/* default policy if not specified */
extern int clocksel; // clock selection for intervals
extern char * cont_ppidc; // container pid signature to look for
extern char * cont_pidc; // command line pid signature to look for
extern int kernelversion; // using kernel version.. 
extern int setdflag; // set deadline overrun flag? for DL processes?

// Global variables used here ->

static char *fileprefix;
long ticksps = 1; // get clock ticks per second (Hz)-> for stat readout

static inline void tsnorm(struct timespec *ts)
{
	while (ts->tv_nsec >= NSEC_PER_SEC) {
		ts->tv_nsec -= NSEC_PER_SEC;
		ts->tv_sec++;
	}
}

/// dumpStats(): prints thread statistics to out
///
/// Arguments: -
///
/// Return value: -
void dumpStats (){

	node_t * item = head;
	(void)printf("\n\nStats- PID - Overshoots(total/scan/fail) \n"
			         "----------------------------------------\n" );
	// for now does only a simple update count
	while (item != NULL) {
		(void)printf("PID %d: %ld(%ld/%ld/%ld)\n", item->pid, item->mon.dl_overrun, item->mon.dl_count, item->mon.dl_scount, item->mon.dl_scanfail);

	item=item->next; 
	}

}

int get_sched_info(node_t * item)
{
	char szFileName [_POSIX_PATH_MAX],
		szStatBuff [PID_BUFFER],
		ltag [80], // just tag of beginning, max lenght expected ~30 
    	*s;

	FILE *fp;
	struct stat st;

	sprintf (szFileName, "/proc/%u/sched", (unsigned) item->pid);

	if (-1 == access (szFileName, R_OK)) {
		return -1;
	} /** if **/


	if ((fp = fopen (szFileName, "r")) == NULL) {
		return -1;
	} /** IF_NULL **/

	// read output into buffer!
	if (0 >= fread (szStatBuff, sizeof(char), PID_BUFFER-1, fp)) {
		fclose (fp);
		return -1;
	}
	szStatBuff[PID_BUFFER-1] = '\0'; // safety first

	fclose (fp);

	long long num;
	s = strtok (szStatBuff, "\n");
	while (NULL != s) {
		(void)sscanf(s,"%s %*c %lld", ltag, &num);
//DBG		printf("%s - %lld\n", ltag, num);
		
//		printf("%s, %lld\n", ltag, num);
		// first letter gets lost in scanf due to head
/*		if (strncasecmp(ltag, "se.exec_start", 4) == 0)	{
			long long x;
			fscanf(fp,"%*c%lld", &x);		
			item->mon.dl_start = num*1000000+x;
//			printf("PID %d, %ld\n", item->pid, item->mon.dl_start);
		}*/
		if (strncasecmp(ltag, "dl.runtime", 4) == 0)	{
			item->mon.dl_rt = num;
//			if (num < 0)
//					warn("PID %d negative left runtime %lldns\n", item->pid, item->mon.dl_rt); 
		}
		if (strncasecmp(ltag, "dl.deadline", 4) == 0)	{
			item->mon.dl_scount++;
			if (0 == item->mon.dl_deadline) 
				item->mon.dl_deadline = num;
			else if (num != item->mon.dl_deadline) {
				item->mon.dl_count++;
				// modulo? inprobable skip?			
				long long diff = (num-item->mon.dl_deadline)-item->attr.sched_period;			
				if (diff)  {
					if (0 == diff % item->attr.sched_period) 
						// difference is exact multiple of period => scan fail? 
						item->mon.dl_scanfail++;
					else {
						item->mon.dl_overrun++;
						printf("\n");
						warn("PID %d Deadline overrun by %lldns\n", item->pid, diff); 
					}
				}
				item->mon.dl_deadline = num;
			}	
			break; // we're done reading
		}
		s = strtok (NULL, "\n");	
	}

//  fclose (fp);
  return 0;
}


// until here

// Thread managing pid list update

/// updateStats(): update the real time statistics for all scheduled threads
/// -- used for monitoring purposes ---
///
/// Arguments: - 
///
/// Return value: number of PIDs found (total)
///
int updateStats ()
{
	static int prot = 0; // pipe rotation animation
	static char const sp[4] = "/-\\|";

	prot = (++prot) % 4;
	(void)printf("\b%c", sp[prot]);		
	fflush(stdout);


	// init head
	node_t * item = head;

	// for now does only a simple update
	while (item != NULL) {

		// update only when defaulting -> new entry
		if (-1 == item->attr.sched_policy) {
			if (sched_getattr (item->pid, &(item->attr), sizeof(struct sched_attr), 0U) != 0) {

				warn("Unable to read params for PID %d: %s\n", item->pid, strerror(errno));		
			}

			// set the flag for deadline notification if not enabled yet -- TEST
			if ((setdflag) && (SCHED_DEADLINE == item->attr.sched_policy) && (KV_416 <= kernelversion) && !(SCHED_FLAG_DL_OVERRUN == (item->attr.sched_flags & SCHED_FLAG_DL_OVERRUN))){

				cont("Set dl_overrun flag for PID %d\n", item->pid);		

				item->attr.sched_flags |= SCHED_FLAG_DL_OVERRUN;
				if (sched_setattr (item->pid, &item->attr, 0U))
					err_msg(KRED "Error!" KNRM ": %s\n", strerror(errno));
			
			} 
		}

		// get runtime value
/*		if (SCHED_DEADLINE == item->attr.sched_policy) {
			int ret;
			if ((ret = get_sched_info(item)) ) {
				err_msg (KRED "Error!" KNRM " reading thread debug details  %d\n", ret);
			} 
		}
*/
		/*
		if (!get_proc_info(item->pid, procinf) && (procinf->flags & 0xF > 0) ) {
			item->mon.dl_overrun++;
			printf ("Hello: %d\n", procinf->flags);
		} these are different flags.. 
		*/ 

		// exponentially weighted moving average
		//item->mon.rt_avg = item->mon.rt_avg * 0.9 + item.attr;

		item=item->next; 
	}

}

/// getContPids(): utility function to get PID list of interrest from Cgroups
/// Arguments: - pointer to array of PID
///			   - size in elements of array of PID
///
/// Return value: number of PIDs found (total)
///
int getContPids (pidinfo_t *pidlst, size_t cnt)
{

	// no memory has been allocated yet
	fileprefix = cpusetdfileprefix; // set to docker directory

	struct dirent *dir;
	DIR *d = opendir(fileprefix);// -> pointing to global
	if (d) {
		fileprefix = NULL; // clear pointer again
		char * nfileprefix = NULL;
		char pidline[PID_BUFFER];
		char *pid;
		int i =0;

		while ((dir = readdir(d)) != NULL) {
		// scan trough docker cgroups, find them?
			if ((strlen(dir->d_name)>60) && // container strings are very long!
					(fileprefix=realloc(fileprefix,strlen(cpusetdfileprefix)+strlen(dir->d_name)+1))) {
				fileprefix[0] = '\0';   // ensures the memory is an empty string
				// copy to new prefix
				strcat(fileprefix,cpusetdfileprefix);
				strcat(fileprefix,dir->d_name);

				printDbg( "\nContainer detection!\n");

				if ((nfileprefix=realloc(nfileprefix,strlen(fileprefix)+strlen("/tasks")+1))) {
					nfileprefix[0] = '\0';   // ensures the memory is an empty string
					// copy to new prefix
					strcat(nfileprefix,fileprefix);
					strcat(nfileprefix,"/tasks");

					// prepare literal and open pipe request
					int path = open(nfileprefix,O_RDONLY);

					// Scan through string and put in array
					int nread = 0;
					while(nread =read(path, pidline,PID_BUFFER-1)) { // used len -1 TODO: fix, doesn't get all tasks, readln? -> see schedstat
						printDbg("Pid string return %s\n", pidline);
						pidline[nread] = '\0';						
						pid = strtok (pidline,"\n");	
						while (pid != NULL) {
							// pid found
							pidlst->pid = atoi(pid);
							printDbg("%d\n",pidlst->pid);

							pidlst->psig = NULL;							
							// find command string and copy to new allocation
							if (pidlst->contid = calloc(1, strlen(dir->d_name)+1)) // alloc memory for string
								(void)strncpy(pidlst->contid,dir->d_name, strlen(dir->d_name)); // copy string, max size of string
							pidlst++;
							i++;

							pid = strtok (NULL,"\n");	

						}
						
					}

					close(path);
				}

			}
		}
		closedir(d);

		// free string buffers
		if (fileprefix)
			free (fileprefix);

		if (nfileprefix)
			free (nfileprefix);

		return i;
	}
	else {
		warn("Can not open Docker CGroups - is the daemon still running?\n");
		cont( "switching to container PID detection mode\n");
		use_cgroup = DM_CNTPID; // switch to container pid detection mode
		return 0; // count of items found

	}

}

/// getpids(): utility function to get list of PID
/// Arguments: - pointer to array of PID
///			   - size in elements of array of PID
///			   - tag string containing the 
///
/// Return value: number of PIDs found (total)
///
int getPids (pidinfo_t *pidlst, size_t cnt, char * tag)
{
	char pidline[PID_BUFFER];
	char req[40];
	char *pid;
	int i =0  ;
	// prepare literal and open pipe request, request spid (thread) ids\
	// spid and pid coincide for main process
	(void)sprintf (req,  "ps h -o spid,command %s", tag);
	FILE *fp;

	if(!(fp = popen(req,"r")))
		return 0;

	// Scan through string and put in array
	while(fgets(pidline,PID_BUFFER,fp) && i < cnt) {
		printDbg("Pid string return %s\n", pidline);
		pid = strtok (pidline," ");					
        pidlst->pid = atoi(pid);
        printDbg("%d",pidlst->pid);

		// find command string and copy to new allocation
        pid = strtok (NULL, "\n"); // end of line?
        printDbg(" cmd: %s\n",pid);
		// TODO: what if len = max, null terminator?
		if (pidlst->psig = calloc(1, SIG_LEN)) // alloc memory for string
			(void)strncpy(pidlst->psig,pid,SIG_LEN); // copy string, max size of string
		pidlst->contid = NULL;							

		pidlst++;
        i++;
    }

	pclose(fp);
	// return number of elements found
	return i;
}



/// getpids(): utility function to                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          get PID list of interrest
/// Arguments: - pointer to array of PID
///			   - size in elements of array of PID
///			   - tag string containing the 
///
/// Return value: number of PIDs found (total)
///
int getcPids (pidinfo_t *pidlst, size_t cnt)
{
	char pidline[PID_BUFFER];
	char req[40];

	// prepare literal and open pipe request
	(void)sprintf (req,  "pidof %s", cont_ppidc);
	FILE *fp;

	if(!(fp = popen(req,"r")))
		return 0;

	int cnt2 = 0;
	// read list of PPIDs
	if (fgets(pidline,PID_BUFFER-10,fp)) { // len -10 (+\n), limit maximum
		int i=0;
		while (pidline[i] && i<PID_BUFFER) {
			if (' ' == pidline[i]) 
				pidline[i]=',';
			i++;
		}

		char pids[PID_BUFFER] = "-T --ppid "; // len = 10, sum = total buffer
		(void)strcat(pids, pidline);
		pids[strlen(pids)-1]='\0'; // just to be sure.. terminate with nullchar, overwrite \n

		// replace space with, for PID list

		cnt2 = getPids(&pidlst[0], cnt, pids );
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
void scanNew () {
	// get PIDs 
	pidinfo_t pidlst[MAX_PIDS]; // TODO: create dynamic list, allocate as grows

	/// --------------------
	/// cgroup present, fix cpu-sets of running containers
	int cnt;

	switch (use_cgroup) {

		case DM_CGRP: // detect by cgroup
			cnt = getContPids(&pidlst[0], MAX_PIDS);
			break;

		case DM_CNTPID: // detect by container shim pid
			cnt = getcPids(&pidlst[0], MAX_PIDS);
			break;

		default: ;// TODO: update - detect by pid signature
			char pid[SIG_LEN];
			sprintf(pid, "-C %s", cont_pidc);
			cnt = getPids(&pidlst[0], MAX_PIDS, pid);
			break;
		
	}
#ifdef DBG
	for (int i=0; i<cnt; i++){
		(void)printf("Result update pid %d\n", (pidlst+i)->pid);		
	}
#endif

	node_t *act = head, *prev = NULL;
	int i = cnt-1;
	printDbg("\nEntering node update");		
	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);
	while ((NULL != act) && (0 <= i )) {
		
		// insert a missing item		
		if ((pidlst +i)->pid > (act->pid)) {
			printDbg("\n... Insert new PID %d", (pidlst +i)->pid);		
			// insert, prev is upddated to the new element
			insert_after(&head, &prev, (pidlst +i)->pid, (pidlst +i)->psig, (pidlst +i)->contid);
			i--;
		} 
		else		
		// delete a dopped item
		if ((pidlst +i)->pid < (act->pid)) {
			printDbg("\n... Delete %d", (pidlst +i)->pid);		
			get_next(&act);
			(void)drop_after(&head, &prev);
		} 
		// ok, skip to next
		else {
			printDbg("\nNo change");		
			// free allocated items
			if ((pidlst +i)->psig)
				free((pidlst +i)->psig);
			if ((pidlst +i)->contid)
				free((pidlst +i)->contid);

			i--;
			prev = act; // update prev 
			get_next(&act);
		}
	}

	while (i >= 0) { // reached the end of the actual queue -- insert to list end
		printDbg("\n... Insert at end PID %d", (pidlst +i)->pid);		
		insert_after(&head, &prev, (pidlst +i)->pid, (pidlst +i)->psig, (pidlst +i)->contid);
		i--;
	}

	while (act != NULL) { // reached the end of the pid queue -- drop list end
		// drop missing items
		printDbg("\n... Delete at end %d", act->pid);// prev->next->pid);		
		// get next item, then drop old
		get_next(&act);
		(void)drop_after(&head, &prev);
	}
	// unlock data thread
	(void)pthread_mutex_unlock(&dataMutex);

	printDbg("\n");		


	printDbg("Exiting node update\n");	


}

/// thread_update(): thread function call to manage and update present pids list
///
/// Arguments: - thread state/state machine, passed on to allow main thread stop
///
/// Return value: Exit Code - o for no error - EXIT_SUCCESS
void *thread_update (void *arg)
{
	int32_t* pthread_state = (int32_t *)arg;
	int cc, ret;
	struct timespec intervaltv, diff, old;

	// get clock, use it as a future reference for update time TIMER_ABS*
	ret = clock_gettime(clocksources[clocksel], &intervaltv);
	if (0 != ret) {
		if (EINTR != ret)
			warn("clock_gettime() failed: %s", strerror(errno));
		*pthread_state=-1;
	}
	old = intervaltv;

	// initialize the thread locals
	while(1) {

		switch( *pthread_state )
		{
		case 0:			
			// setup of thread, configuration of scheduling and priority
			*pthread_state=1; // must be first thing! -> main writes -1 to stop
			// get jiffies per sec -> to ms
			ticksps = sysconf(_SC_CLK_TCK);
			if (1 > ticksps) { // must always be greater 0 
				warn("could not read clock tick config!\n");
				break;
			}
			else{ 
				// clock settings found -> check for validity
				cont("clock tick used for scheduler debug found to be %ldHz.\n", ticksps);
				if (500000/ticksps > interval)  
					warn("-- scan time more than double the debug update rate. On purpose? (obsolete kernel value) -- \n");
			}
			if (SCHED_OTHER != policy) { // TODO: set niceness for other/bash?
				// set policy to thread

				if (SCHED_DEADLINE == policy) {
					struct sched_attr scheda  = { 48, 
												SCHED_DEADLINE,
												0,
												0,
												priority,
												update_wcet, interval, interval
												};


					if (sched_setattr(0, &scheda, 0L)) {
						warn("Could not set thread policy!\n");
						// reset value -- not written in main anymore
						policy = SCHED_OTHER;
					}
					else {
						cont("set update thread to '%s', runtime %dus.\n", policyname(policy), update_wcet);
						}
				}
				else{
					struct sched_param schedp  = { priority };

					if (sched_setscheduler(0, policy, &schedp)) {
						warn("Could not set thread policy!\n");
						// reset value -- not written in main anymore
						policy = SCHED_OTHER;
					}
					else {
						cont("set update thread to '%s', priority %d.\n", policyname(policy), priority);
						}
				}
			}

		case 1: 
			// startup-refresh: this should be executed only once every td
			*pthread_state=2; // must be first thing! -> main writes -1 to stop
			scanNew(); 
			(void)printf("\rNode Stats update  ");		
		case 2: // normal thread loop
			if (!cc)
				*pthread_state=1; // must be first thing
			updateStats();
			break;
		case -1:
			*pthread_state=-2; // must be first thing! -> main writes -1 to stop
			(void)printf("\n");
			// tidy or whatever is necessary
//#ifdef DBG
			dumpStats();
//#endif

			// Done -> print total runtime
			ret = clock_gettime(clocksources[clocksel], &diff);
			if (0 != ret) {
				if (EINTR != ret)
					warn("clock_gettime() failed: %s", strerror(errno));
				*pthread_state=-1;
			}
			old = intervaltv;

			info("Total test runtime is %ld seconds\n", diff.tv_sec - old.tv_sec);

			break;
		case -2:
			// exit
			pthread_exit(0); // exit the thread signalling normal return
			break;
		}

		if (SCHED_DEADLINE == policy) 
			// perfect sync with period here, allow replenish 
			pthread_yield(); 
		else {			
			// abs-time relative interval shift
			// TODO: implement relative time and timer based variants?

			// calculate new sleep intervall
			intervaltv.tv_sec += interval / USEC_PER_SEC;
			intervaltv.tv_nsec+= (interval % USEC_PER_SEC) * 1000;
			tsnorm(&intervaltv);

			// sleep for interval nanoseconds
			ret = clock_nanosleep(clocksources[clocksel], TIMER_ABSTIME, &intervaltv, NULL);
			if (0 != ret) {
				// TODO: what-if?
				if (EINTR != ret) {
					warn("clock_nanosleep() failed. errno: %s\n",strerror (ret));
				}
				*pthread_state=-1;
				break;
			}
		}
		cc++;
		cc%=loops;
	}
}

