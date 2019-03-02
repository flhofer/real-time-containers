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
int getpids (pid_t *pidno, size_t cnt, char * tag)
{
	char pidline[1024];
	char req[20];
	char *pid;
	int i =0  ;
	// prepare literal and open pipe request
	sprintf (req,  "pidof %s", tag);
	//sprintf (req,  "ps  %s", tag);
	FILE *fp = popen(req,"r");
	fgets(pidline,1024,fp);
	pclose(fp);

	printDbg("Pid string return %s", pidline);
	pid = strtok (pidline," ");
	// Scan through string and put in array
	while(pid != NULL && i < cnt)
		    {
		            *pidno = atoi(pid);
		            printDbg("%d\n",*pidno);
		            pid = strtok (NULL , " ");
					pidno++;
		            i++;
		    }

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
	pid_t pidno[MAX_PIDS];

	int cnt = getpids(&pidno[0], MAX_PIDS, "bash");
	for (int i=0; i<cnt; i++){
		printDbg("Result update pid %d\n", pidno[i]);		
	}

	node_t *act = head, *prev = NULL;
	struct sched_attr * attr = get_node (head);
	int i = cnt-1;

	printDbg("Entering node update\n");		
	// lock data to avoid inconsistency
	pthread_mutex_lock(&dataMutex);
	while ( (act != NULL) && (attr != NULL) && (i >= 0)) {
		// insert a missing item		
		if (pidno[i] < ((*act).pid)) {
			printDbg("Insert\n");		
			// insert, prev is upddated to the new element
			attr = insert_after(&head, &prev, pidno[i]);
			// sig here to other thread?
			i--;
		} 
		else		
		// insert a missing item		
		if (pidno[i] > ((*act).pid)) {
			printDbg("Delete\n");		
			attr = get_next(&act);
			int ret = drop_after(&head, &prev);
			// sig here to other thread?
		} 
		// ok, skip to next
		else {
			printDbg("No change\n");		
			i--;
			prev = act; // update prev 
			attr = get_next(&act);
		}
	}

	while (i >= 0) {
		printDbg("Insert at end\n");		
		attr = insert_after(&head, &prev, pidno[i]);
		// sig here to other thread?
		i--;
	}

	while ( (act != NULL) && (attr != NULL)) {
		// drop missing items
		printDbg("Delete\n");		
		// get next item, then drop old
		attr = get_next(&act);
		int ret = drop_after(&head, &prev);
		// sig here to other thread?
	}
	// unlock data thread
	pthread_mutex_unlock(&dataMutex);

	printDbg("Exiting node update\n");	
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
	pid_t pidno[MAX_PIDS];

	// here the other threads are not started yet.. no lock needed
	int cnt = getpids(&pidno[0], MAX_PIDS, "bash");

	// TODO: set all non concerning tasks to background resources	
	
	// push into linked list
	for (int i=0; i<cnt; i++){
		printDbg("Result first scan pid %d\n", pidno[i]);		
		(void)push (&head, pidno[i]);
	}
}

/// thread_manage(): thread function call to manage and update present pids list
///
/// Arguments: - thread state/state machine, passed on to allow main thread stop
///
/// Return value: Exit Code - o for no error - EXIT_SUCCESS
void *thread_update (void *arg)
{
	int32_t* pthread_state = arg;
	// initialize the thread locals
	while(1)
	{
	  switch( *pthread_state )
	  {
	  case 0: // normal thread loop
		scanNew();
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

