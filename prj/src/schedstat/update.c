#include "schedstat.h"
#include "update.h"

// test added
#include <limits.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#include <errno.h> // TODO: fix as general

//TODO: unify constants
extern int use_cgroup; // processes identificatiom mode, written before startup of thread


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
  int		cutime;                   /** user mode jiffies with childs **/
  int           cstime;                   /** kernel mode jiffies with childs **/
  int           counter;                  /** process's next timeslice **/
  int           priority;                 /** the standard nice value, plus fifteen **/
  unsigned int  timeout;                  /** The time in jiffies of the next timeout **/
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
  
  sscanf (t + 2, "%c %d %d %d %d %d %u %u %u %u %u %d %d %d %d %d %d %u %u %d %u %u %u %u %u %u %u %u %d %d %d %d %u",
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
	  &(pinfo->counter),
	  &(pinfo->priority),
	  &(pinfo->timeout),
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
	// init head
	node_t * item = head;
	int flags;
	procinfo * procinf = calloc(1, sizeof(procinfo));

	printDbg("Entering node stats update\n");		
	
	// for now does only a simple update
	while (item != NULL) {
		if (sched_getattr (item->pid, &(item->attr), sizeof(struct sched_attr), flags) != 0)
			printDbg(KMAG "Warn!" KNRM " Unable to read params for PID %d: %s\n", item->pid, strerror(errno));		

		if (flags != item->attr.sched_flags)
		// TODO: strangely there is a type mismatch
			printDbg(KMAG "Warn!" KNRM " Flags %d do not match %ld\n", flags, item->attr.sched_flags);		


		// get runtime value
		//char path[256],buffer[256]; 
		//int status,read_length;
        //sprintf(path,"/proc/%i/status",item->pid);

		/*if (!get_proc_info(item->pid, procinf)) {
			printf ("Hello: %d\n", procinf->utime);
			}  -> use function*/ 
		/*
        FILE *fd=fopen(path,"r");
        if(fd!=0){
            read_length=fread(buffer,1,255,fd);
            printf("Read: %s\n",buffer);
            fclose(fd);
        }
		*/


		// exponentially weighted moving average
		

		//item->mon.rt_avg = item->mon.rt_avg * 0.9 + item.attr;




		item=item->next; 
	}

	printDbg("Exiting node stats update\n");		

}

/// getpids(): utility function to get PID list of interrest
/// Arguments: - pointer to array of PID
///			   - size in elements of array of PID
///			   - tag string containing the 
///
/// Return value: number of PIDs found (total)
///
int getpids (pidinfo_t *pidlst, size_t cnt, char * tag)
{
	char pidline[1024];
	char req[40];
	char *pid;
	int i =0  ;
	// prepare literal and open pipe request
	//sprintf (req,  "pidof %s", tag);
	(void)sprintf (req,  "ps h -o pid,command -C %s", tag);
	FILE *fp = popen(req,"r");

	// Scan through string and put in array
	while(fgets(pidline,1024,fp) && i < cnt) {
		//printDbg("Pid string return %s\n", pidline);
		pid = strtok (pidline," ");					
        pidlst->pid = atoi(pid);
        printDbg("%d",pidlst->pid);

		// find command string and copy to new allocation
        pid = strtok (NULL, "\n"); // end of line?
        printDbg(" cmd: %s\n",pid);
		// TODO: what if len = max, null terminator?
		if (pidlst->psig = calloc(1, SIG_LEN)) // alloc memory for string
			(void)strncpy(pidlst->psig,pid,SIG_LEN); // copy string, max size of string
		pidlst++;
        i++;
    }

	pclose(fp);
	// return number of elements found
	return i;
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
	pidinfo_t pidlst[MAX_PIDS];

	int cnt = getpids(&pidlst[0], MAX_PIDS, "bash");
	for (int i=0; i<cnt; i++){
		printDbg("Result update pid %d\n", (pidlst+i)->pid);		
	}

	node_t *act = head, *prev = NULL;
	int i = cnt-1;

	printDbg("Entering node update\n");		
	// lock data to avoid inconsistency
	pthread_mutex_lock(&dataMutex);
	while ( (act != NULL) && (i >= 0)) {
		
		// insert a missing item		
		if ((pidlst +i)->pid < ((*act).pid)) {
			printDbg("Insert\n");		
			// insert, prev is upddated to the new element
			insert_after(&head, &prev, (pidlst +i)->pid, (pidlst +i)->psig);
			// sig here to other thread?
			i--;
		} 
		else		
		// delete a dopped item
		if ((pidlst +i)->pid > ((*act).pid)) {
			printDbg("Delete\n");		
			get_next(&act);
			(void)drop_after(&head, &prev);
			// sig here to other thread?
		} 
		// ok, skip to next
		else {
			printDbg("No change\n");		
			i--;
			prev = act; // update prev 
			get_next(&act);
		}
	}

	while (i >= 0) {
		printDbg("Insert at end\n");		
		insert_after(&head, &prev, (pidlst +i)->pid, (pidlst +i)->psig);
		// sig here to other thread?
		i--;
	}

	while (act != NULL) {
		// drop missing items
		printDbg("Delete\n");		
		// get next item, then drop old
		get_next(&act);
		(void)drop_after(&head, &prev);
		// sig here to other thread?
	}
	// unlock data thread
	(void)pthread_mutex_unlock(&dataMutex);

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
	int cc;
	// initialize the thread locals
	while(1) {
	
		switch( *pthread_state )
		{
		case 0: 
			// startup-refresh: this should be executed only once every td
			scanNew(); 
			*pthread_state=1;
		case 1: // normal thread loop
			updateStats();
			if (!cc) 
				*pthread_state=0;
			break;
		case -1:
			// tidy or whatever is necessary
			pthread_exit(0); // exit the thread signalling normal return
			break;
		}
		usleep(TSCAN);
		cc++;
		cc%=TDETM;
	}
}

