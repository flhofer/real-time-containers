#include "schedstat.h"
#include "update.h"


// Thread managing pid list update

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
        (void)printDbg("%d",pidlst->pid);

		// find command string and copy to new allocation
        pid = strtok (NULL, ""); // end of line?
        (void)printDbg(" cmd: %s\n",pid);
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
	char * pidsig = "hello"; // TODO: fix filename list 

	int cnt = getpids(&pidlst[0], MAX_PIDS, "bash");
	for (int i=0; i<cnt; i++){
		(void)printDbg("Result update pid %d\n", (pidlst+i)->pid);		
	}

	node_t *act = head, *prev = NULL;
	struct sched_attr * attr = get_node (head);
	int i = cnt-1;

	(void)printDbg("Entering node update\n");		
	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);
	while ( (act != NULL) && (attr != NULL) && (i >= 0)) {
		// insert a missing item		
		if ((pidlst +i)->pid < ((*act).pid)) {
			(void)printDbg("Insert\n");		
			// insert, prev is upddated to the new element
			attr = insert_after(&head, &prev, (pidlst +i)->pid, (pidlst +i)->psig);
			// sig here to other thread?
			i--;
		} 
		else		
		// delete a dopped item
		if ((pidlst +i)->pid > ((*act).pid)) {
			(void)printDbg("Delete\n");		
			attr = get_next(&act);
			int ret = drop_after(&head, &prev);
			// sig here to other thread?
		} 
		// ok, skip to next
		else {
			(void)printDbg("No change\n");		
			i--;
			prev = act; // update prev 
			attr = get_next(&act);
		}
	}

	while (i >= 0) {
		(void)printDbg("Insert at end\n");		
		attr = insert_after(&head, &prev, (pidlst +i)->pid, (pidlst +i)->psig);
		// sig here to other thread?
		i--;
	}

	while ( (act != NULL) && (attr != NULL)) {
		// drop missing items
		(void)printDbg("Delete\n");		
		// get next item, then drop old
		attr = get_next(&act);
		int ret = drop_after(&head, &prev);
		// sig here to other thread?
	}
	// unlock data thread
	(void)pthread_mutex_unlock(&dataMutex);

	(void)printDbg("Exiting node update\n");	
}

/// prepareEnvironment(): gets the list of active pids at startup, sets up
/// a CPU-shield if not present, and populates initial state of pid list
///
/// Arguments: 
///
/// Return value: 
///
void prepareEnvironment() {
	// get PIDs 
	pidinfo_t pidlst[MAX_PIDS];
	char * pidsig = "hello"; // TODO: fix filename list 

	// here the other threads are not started yet.. no lock needed
	int cnt = getpids(&pidlst[0], MAX_PIDS, "bash");

	// TODO: set all non concerning tasks to background resources	
	
	// push into linked list
	for (int i=0; i<cnt; i++){
		(void)printDbg("Result first scan pid %d\n", (pidlst +i)->pid);		
		push (&head, (pidlst +i)->pid, (pidlst +i)->psig);
	}
}

/// thread_manage(): thread function call to manage and update present pids list
///
/// Arguments: - thread state/state machine, passed on to allow main thread stop
///
/// Return value: Exit Code - o for no error - EXIT_SUCCESS
void *thread_update (void *arg)
{
	int32_t* pthread_state = (int32_t *)arg;
	// initialize the thread locals
	while(1)
	{
	  switch( *pthread_state )
	  {
	  case 0: // normal thread loop
		scanNew();
		*pthread_state=-1;
		break;
	  case -1:
		// tidy or whatever is necessary
		pthread_exit(0); // exit the thread signalling normal return
		break;
	  case 1: //
		// do something special
		break;
	  }
	  usleep(1000000);
	}
}

