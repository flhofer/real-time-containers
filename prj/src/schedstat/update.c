#include "schedstat.h"
#include "update.h"
#include "manage.h"

// test added
#include <limits.h>
#include <sys/stat.h>
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
	(void)printf("\n\nStats- PID - Overshoots(total/scan)\n"
			         "-----------------------------------\n" );
	// for now does only a simple update count
	while (item != NULL) {
		(void)printf("PID %d: %ld(%ld/%ld)\n", item->pid, item->mon.dl_overrun, item->mon.dl_count, item->mon.dl_scount);

	item=item->next; 
	}

}


// test added 
typedef struct statstruct_proc {
  int           pid;                      /** The process id. **/
  char          exName [_POSIX_PATH_MAX]; /** The filename of the executable **/
  char          state; /** 1 **/          /** R is running, S is sleeping, 
			   D is sleeping in an uninterruptible wait,
			   Z is zombie, T is traced or stopped **/
  unsigned      euid,                      /** effective user id **/
                egid;                      /** effective group id */					     
  int           ppid;                     /** The pid of the parent. **/
  int           pgrp;                     /** The pgrp of the process. **/
  int           session;                  /** The session id of the process. **/
  int           tty;                      /** The tty the process uses **/
  int           tpgid;                    /** (too long) **/
  unsigned int	flags;                    /** The flags of the process. **/
  unsigned int	minflt;                   /** The number of minor faults **/
  unsigned int	cminflt;                  /** The number of minor faults with childs **/
  unsigned int	majflt;                   /** The number of major faults **/
  unsigned int  cmajflt;                  /** The number of major faults with childs **/
  int           utime;                    /** user mode jiffies **/
  int           stime;                    /** kernel mode jiffies **/
  int			cutime;                   /** user mode jiffies with childs **/
  int           cstime;                   /** kernel mode jiffies with childs **/
  int           nice;	                  /** niceness level **/
  int           priority;                 /** the standard nice value, plus fifteen **/
  unsigned int  num_threads;                  /** The time in jiffies of the next timeout **/
  unsigned int  itrealvalue;              /** The time before the next SIGALRM is sent to the process **/
  int           starttime; /** 20 **/     /** Time the process started after system boot **/
  unsigned int  vsize;                    /** Virtual memory size **/
  unsigned int  rss;                      /** Resident Set Size **/
  unsigned int  rlim;                     /** Current limit in bytes on the rss **/
  unsigned int  startcode;                /** The address above which program text can run **/
  unsigned int	endcode;                  /** The address below which program text can run **/
  unsigned int  startstack;               /** The address of the start of the stack **/
  unsigned int  kstkesp;                  /** The current value of ESP **/
  unsigned int  kstkeip;                 /** The current value of EIP **/
  int			signal;                   /** The bitmap of pending signals **/
  int           blocked; /** 30 **/       /** The bitmap of blocked signals **/
  int           sigignore;                /** The bitmap of ignored signals **/
  int           sigcatch;                 /** The bitmap of catched signals **/
  unsigned int  wchan;  /** 33 **/        /** (too long) **/
  int			sched, 		  /** scheduler **/
                sched_priority;		  /** scheduler priority **/
		
} procinfo;

int get_proc_info(pid_t pid, procinfo * pinfo)
{
  char szFileName [_POSIX_PATH_MAX],
    szStatStr [2048],
    *s, *t;
  FILE *fp;
  struct stat st;
  
  if (NULL == pinfo) {
    errno = EINVAL;
    return -1;
  }

  sprintf (szFileName, "/proc/%u/stat", (unsigned) pid);
  
  if (-1 == access (szFileName, R_OK)) {
    return (pinfo->pid = -1);
  } /** if **/

  if (-1 != stat (szFileName, &st)) {
  	pinfo->euid = st.st_uid;
  	pinfo->egid = st.st_gid;
  } else {
  	pinfo->euid = pinfo->egid = -1;
  }
  
  
  if ((fp = fopen (szFileName, "r")) == NULL) {
    return (pinfo->pid = -1);
  } /** IF_NULL **/
  
  if ((s = fgets (szStatStr, 2048, fp)) == NULL) {
    fclose (fp);
    return (pinfo->pid = -1);
  }

  /** pid **/
  sscanf (szStatStr, "%u", &(pinfo->pid));
  s = strchr (szStatStr, '(') + 1;
  t = strchr (szStatStr, ')');
  strncpy (pinfo->exName, s, t - s);
  pinfo->exName [t - s] = '\0';
  
  sscanf (t + 2, "%c %d %d %d %d %d %u %u %u %u %u %d %d %d %d %d %d %d %d %u %u %d %u %u %u %u %u %u %u %u %u %u %u",
	  /*       1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33*/
	  &(pinfo->state),
	  &(pinfo->ppid),
	  &(pinfo->pgrp),
	  &(pinfo->session),
	  &(pinfo->tty),
	  &(pinfo->tpgid),
	  &(pinfo->flags),
	  &(pinfo->minflt),
	  &(pinfo->cminflt),
	  &(pinfo->majflt),
	  &(pinfo->cmajflt),
	  &(pinfo->utime),
	  &(pinfo->stime),
	  &(pinfo->cutime),
	  &(pinfo->cstime),
	  &(pinfo->priority),
	  &(pinfo->nice),
	  &(pinfo->num_threads),
	  &(pinfo->itrealvalue),
	  &(pinfo->starttime),
	  &(pinfo->vsize),
	  &(pinfo->rss),
	  &(pinfo->rlim),
	  &(pinfo->startcode),
	  &(pinfo->endcode),
	  &(pinfo->startstack),
	  &(pinfo->kstkesp),
	  &(pinfo->kstkeip),
	  &(pinfo->signal),
	  &(pinfo->blocked),
	  &(pinfo->sigignore),
	  &(pinfo->sigcatch),
	  &(pinfo->wchan));
  fclose (fp);
  return 0;
}

int get_sched_info(node_t * item)
{
  char szFileName [_POSIX_PATH_MAX],
  	   szStatStr [2048],
       *s, *t;
  FILE *fp;
  struct stat st;
  
  sprintf (szFileName, "/proc/%u/sched", (unsigned) item->pid);
  
  if (-1 == access (szFileName, R_OK)) {
    return -1;
  } /** if **/
  
  if ((fp = fopen (szFileName, "r")) == NULL) {
    return -1;
  } /** IF_NULL **/
  
	long long num;
	while (EOF != fscanf(fp,"%s %*c %lld", szStatStr, &num)) {
//		printf("%s, %lld\n", szStatStr, num);
		// first letter gets lost in scanf due to head
/*		if (strncasecmp(szStatStr, "e.exec_start", 4) == 0)	{
			long long x;
			fscanf(fp,"%*c%lld", &x);		
			item->mon.dl_start = num*1000000+x;
//			printf("PID %d, %ld\n", item->pid, item->mon.dl_start);
		}*/
		if (strncasecmp(szStatStr, "dl.runtime", 4) == 0)	{
			item->mon.dl_rt = num;
		}
		if (strncasecmp(szStatStr, "dl.deadline", 4) == 0)	{
			item->mon.dl_scount++;
			if (0 == item->mon.dl_deadline) 
				item->mon.dl_deadline = num;
			else if (num != item->mon.dl_deadline) {
				item->mon.dl_count++;
				// modulo? inprobable skip?			
				long long diff = (num-item->mon.dl_deadline) %item->attr.sched_period;			
				if (diff)  {
					item->mon.dl_overrun++;
					warn("PID %d Deadline overrun by %lldns\n", item->pid, diff); 
				}
				item->mon.dl_deadline = num;
			}	
			break; // we're done reading
		}
	}

  fclose (fp);
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
	procinfo * procinf = calloc(1, sizeof(procinfo));

	// for now does only a simple update
	while (item != NULL) {

		// update only when defaulting -> new entry
		if (-1 == item->attr.sched_policy) {
			if (sched_getattr (item->pid, &(item->attr), sizeof(struct sched_attr), 0U) != 0) {

				warn("Unable to read params for PID %d: %s\n", item->pid, strerror(errno));		
			}

			// set the flag for deadline notification if not enabled yet -- TEST
			if ((SCHED_DEADLINE == item->attr.sched_policy) && (KV_416 <= kernelversion) && !(SCHED_FLAG_DL_OVERRUN == (item->attr.sched_flags & SCHED_FLAG_DL_OVERRUN))){

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
	FILE *fp = popen(req,"r");

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
	FILE *fp = popen(req,"r");

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
	int new= 0;
	printDbg("\nEntering node update");		
	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);
	while ((NULL != act) && (0 <= i )) {
		
		// insert a missing item		
		if ((pidlst +i)->pid > (act->pid)) {
			printDbg("\n... Insert new PID %d", (pidlst +i)->pid);		
			// insert, prev is upddated to the new element
			insert_after(&head, &prev, (pidlst +i)->pid, (pidlst +i)->psig, (pidlst +i)->contid);
			new++;
			i--;
		} 
		else		
		// delete a dopped item
		if ((pidlst +i)->pid < (act->pid)) {
			printDbg("\n... Delete %d", (pidlst +i)->pid);		
			get_next(&act);
			(void)drop_after(&head, &prev);
			new++;
		} 
		// ok, skip to next
		else {
			printDbg("\nNo change");		
			i--;
			prev = act; // update prev 
			get_next(&act);
		}
	}

	while (i >= 0) { // reached the end of the actual queue -- insert to list end
		printDbg("\n... Insert at end PID %d", (pidlst +i)->pid);		
		insert_after(&head, &prev, (pidlst +i)->pid, (pidlst +i)->psig, (pidlst +i)->contid);
		new++;
		i--;
	}

	while (act != NULL) { // reached the end of the pid queue -- drop list end
		// drop missing items
		printDbg("\n... Delete at end %d", act->pid);// prev->next->pid);		
		// get next item, then drop old
		get_next(&act);
		(void)drop_after(&head, &prev);
		new++;
	}
	// unlock data thread
	(void)pthread_mutex_unlock(&dataMutex);

//	if (new)
	printDbg("\n");		

// PDB	printDbg("Exiting node update\n");	
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
	struct timespec intervaltv;

	// get clock, use it as a future reference for update time TIMER_ABS*
	ret = clock_gettime(clocksources[clocksel], &intervaltv);
	if (ret != 0) {
		if (ret != EINTR)
			warn("clock_gettime() failed: %s", strerror(errno));
		*pthread_state=-1;
	}

	// initialize the thread locals
	while(1) {

		switch( *pthread_state )
		{
		case 0:			
			// setup of thread, configuration of scheduling and priority
			*pthread_state=1; // must be first thing! -> main writes -1 to stop
			// get jiffies per sec -> to ms
			ticksps = sysconf(_SC_CLK_TCK);
			if (ticksps<1) { // must always be greater 0 
				warn("could not read clock tick config!\n");
				break;
			}
			else{ 
				// clock settings found -> check for validity
				cont("clock tick used for scheduler debug found to be %ldHz.\n", ticksps);
				if (500000000/ticksps > interval*1000)  
					warn("scan time more than double the debug update rate. On purpose?\n");
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
			if (ret != 0) {
				// TODO: what-if?
				if (ret != EINTR) {
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

