#include "schedstat.h"
#include "update.h"

// Should be needed only here
#include <limits.h>
#include <sys/vfs.h>

// Global variables used here ->
static char *fileprefix;
static long ticksps = 1; // get clock ticks per second (Hz)-> for stat readout
static uint64_t scount = 0; // total scan count

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

/// dumpStats(): prints thread statistics to out
///
/// Arguments: -
///
/// Return value: -
void dumpStats (){

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
		(void)printf("%5d: %ld(%ld/%ld/%ld) - %ld(%ld/%ld) - %ld(%ld/%ld/%ld)\n", 
			item->pid, item->mon.dl_overrun, item->mon.dl_count+item->mon.dl_scanfail,
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

	int64_t num;
	int64_t diff = 0;
	int64_t ltrt = 0; // last seen runtime

	s = strtok (szStatBuff, "\n");
	while (NULL != s) {
		(void)sscanf(s,"%s %*c %ld", ltag, &num);
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
			// store last seen runtime
			ltrt = num;
			if (num != item->mon.dl_rt)
				item->mon.dl_count++;
//			if (num < 0)
//					warn("PID %d negative left runtime %lldns\n", item->pid, item->mon.dl_rt); 
		}
		if (strncasecmp(ltag, "dl.deadline", 4) == 0)	{
			if (0 == item->mon.dl_deadline) 
				item->mon.dl_deadline = num;
			else if (num != item->mon.dl_deadline) {
				// it's not, updated deadline found
//				item->mon.dl_count++;

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
					printDbg("\nPID %d Deadline overrun by %lldns, sum %lld\n",
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

//  fclose (fp);
  return 0;
}

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

	scount++; // increase scan-count
	// for now does only a simple update
	while (item != NULL) {

		// update only when defaulting -> new entry
		if (SCHED_NODATA == item->attr.sched_policy) {
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
		if (SCHED_DEADLINE == item->attr.sched_policy) {
			int ret;
			if ((ret = get_sched_info(item)) ) {
				err_msg (KRED "Error!" KNRM " reading thread debug details  %d\n", ret);
			} 
		}

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
					int nleft = 0;
					while(nleft += read(path, pidline+nleft,PID_BUFFER-nleft)) {
						printDbg("Pid string return %s\n", pidline);

						pid = strtok (pidline,"\n");	
						while (pid != NULL && 5<nleft) {
							// pid found
							pidlst->pid = atoi(pid);
							printDbg("%d\n",pidlst->pid);

							pidlst->psig = NULL;							
							// find command string and copy to new allocation
							if (pidlst->contid = calloc(1, strlen(dir->d_name)+1)) // alloc memory for string
								(void)strncpy(pidlst->contid,dir->d_name, strlen(dir->d_name)); // copy string, max size of string
							pidlst++;
							i++;

							nleft -= strlen(pid)+1;
							pid = strtok (NULL,"\n");	

						}
						if (pid) // copy leftover chars to beginning of string buffer
							memcpy(pidline, pid, nleft); 
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
///			   - tag string containing the command signature to look for 
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



/// getcPids(): utility function to get list of PID by PPID tag
/// Arguments: - pointer to array of PID
///			   - size in elements of array of PID
///			   - tag string containing the name of the parent pid to look for
///
/// Return value: number of PIDs found (total)
///
int getpPids (pidinfo_t *pidlst, size_t cnt, char * tag)
{
	char pidline[PID_BUFFER];
	char req[40];

	// prepare literal and open pipe request
	(void)sprintf (req,  "pidof %s", tag);
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
	pidinfo_t pidlst[MAX_PIDS]; // TODO: create dynamic list, allocate as grows/ may use a parameter
	int cnt; // Count of found PID

	switch (use_cgroup) {

		case DM_CGRP: // detect by cgroup
			cnt = getContPids(&pidlst[0], MAX_PIDS);
			break;

		case DM_CNTPID: // detect by container shim pid
			cnt = getpPids(&pidlst[0], MAX_PIDS, cont_ppidc);
			break;

		default: ;// detect by pid signature
			// cmdline of own thread
			char pid[SIG_LEN];
			if (psigscan)
				sprintf(pid, "-TC %s", cont_pidc);
			else 
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
	int cc, ret;
	struct timespec intervaltv, now, old;

	// get clock, use it as a future reference for update time TIMER_ABS*
	ret = clock_gettime(clocksources[clocksel], &intervaltv);
	if (0 != ret) {
		if (EINTR != ret)
			warn("clock_gettime() failed: %s", strerror(errno));
		*pthread_state=-1;
	}
	old = intervaltv;

	if (runtime)
		cont("Runtime set to %d seconds\n", runtime);

	// initialize the thread locals
	while(1) {

		switch( *pthread_state )
		{
		case 0:			
			// setup of thread, configuration of scheduling and priority
			*pthread_state=-1; // must be first thing! -> main writes -1 to stop
			cont("Sample time set to %dus.\n", interval);
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
												0,// TODO : reset on fork should help for deadline and PID - , SCHED_FLAG_RECLAIM,
												0,
												0,
												update_wcet*1000, interval*1000, interval*1000
												};


					if (sched_setattr(0, &scheda, 0L)) {
						warn("Could not set thread policy!\n");
						// reset value -- not written in main anymore
						policy = SCHED_OTHER;
					}
					else {
						cont("set update thread to '%s', runtime %dus.\n", policy_to_string(policy), update_wcet);
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
						cont("set update thread to '%s', priority %d.\n", policy_to_string(policy), priority);
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
			dumpStats();

			// update time if not in runtime mode - has not been read yet
			if (!runtime) {
				ret = clock_gettime(clocksources[clocksel], &now);
				if (0 != ret) 
					if (EINTR != ret)
						warn("clock_gettime() failed: %s", strerror(errno));
			}

			// Done -> print total runtime, now updated every cycle
			info("Total test runtime is %ld seconds\n", now.tv_sec - old.tv_sec);

			break;
		case -2:
			// exit
			pthread_exit(0); // exit the thread signalling normal return
			break;
		}

		if (SCHED_DEADLINE == policy){
			// perfect sync with period here, allow replenish 
			if (pthread_yield()){
				warn("pthread_yield() failed. errno: %s\n",strerror (ret));
				*pthread_state=-1;
				break;				
			}

		}
		else {			
			// abs-time relative interval shift
			// TODO: implement relative time and timer based variants?

			// calculate next execution intervall
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

		// we have a max runtime. Stop! -> after the clock_nanosleep time will be intervaltv
		if (runtime) {
			ret = clock_gettime(clocksources[clocksel], &now);
			if (0 != ret) {
				if (EINTR != ret)
					warn("clock_gettime() failed: %s", strerror(errno));
				*pthread_state=-1;
			}

			if (old.tv_sec + runtime <= now.tv_sec
				&& old.tv_nsec <= now.tv_nsec) 
				// set stop sig
				raise (SIGTERM); // tell main to stop
		}

		cc++;
		cc%=loops;
	}
}

