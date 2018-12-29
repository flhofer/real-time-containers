#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // used for string parsing
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants
#include "pidlist.h" // memory structure to store information

#include <sched.h>
#include <linux/types.h>
#include <signal.h> // for SIGs
//#include <stdarg.h> __VA_ARGS__ does not work??

#define PRGNAME "DC static orchestrator"
#define VERSION 0.1
#define MAX_PIDS 64
#define MAX_CPUS 8

#define DBG

/* Debug printing to console or buffer ?? */
#ifdef DBG
#define printDbg (void)printf

#else
#define printDbg //
#endif

/* Available standard calls */

//sched_get_priority_max(int);//sched_get_priority_min(int);//sched_getparam(pid_t, struct sched_param *);//sched_getscheduler(pid_t);//sched_rr_get_interval(pid_t, struct timespec *);//sched_setparam(pid_t, const struct sched_param *);//sched_setscheduler(pid_t, int, const struct sched_param *);//sched_yield(void)

// signal to keep status of triggers ext SIG
volatile sig_atomic_t stop;
// mutex to avoid read while updater fills or empties existing threads
pthread_mutex_t dataMutex;

// head of pidlist
node_t * head = NULL;

/// inthand(): interrupt handler for infinite while loop, help 
/// this function is called from outside, interrupt handling routine
/// Arguments: - signal number of interrupt calling
///
/// Return value: 
///
void inthand ( int signum ) {
	stop = 1;
}

void *thread_update (void *arg); // thread that scans peridically for new entry pids
void *thread_manage (void *arg); // thread that verifies status and allocates new threads

/// updateSched(): main function called to verify running schedule
//
/// Arguments: 
///
/// Return value: N/D
///
int updateSched(){
	uint64_t cputimes[MAX_CPUS] = {}; 
	uint64_t cpuperiod[MAX_CPUS] = {}; 
	cpu_set_t cset;

	// zero cpu-set, static size set
	CPU_ZERO(&cset);
	CPU_SET(0, &cset);

	pthread_mutex_lock(&dataMutex);

    node_t * current = head;
	while (current != NULL) {
		// get schedule of new pids
		if (current->attr.size == 0) {
			struct timespec tt;
			
			int ret = sched_rr_get_interval(current->pid, &tt);
			printDbg("Schedule pid %d: %d %ld\n", current->pid, ret, tt.tv_nsec);

			ret = sched_getattr (current->pid, &(current->attr), sizeof(node_t), 0U);
			printDbg("Attr: %d %d\n", ret, current->attr.sched_policy);

			ret = sched_setaffinity(current->pid, sizeof(cset), &cset );
			if (ret == 0)
				printDbg("Pid %d reassigned to CPU0\n", current->pid);

			// TODO: ret value evaluation 
		}

		// affinity not set?? default is 0, affinity of system stuff

		// sum of cpu-times, affinity is only 1 cpu here
		cputimes[current->affinity] += current->attr.sched_deadline;
		cpuperiod[current->affinity] += current->attr.sched_deadline;

        current = current->next;
    }


	pthread_mutex_unlock(&dataMutex);
}

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
	int* pthread_state = arg;
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

/// thread_manage(): thread function call to manage and update schedule list
///
/// Arguments: - thread state/state machine, passed on to allow main thread stop
///
/// Return value: Exit Code - o for no error - EXIT_SUCCESS
void *thread_manage (void *arg)
{
	int* pthread_state = arg;
	// initialize the thread locals
	while(1)
	{
	  switch( *pthread_state )
	  {
	  case 0: // normal thread loop
		updateSched();
		break;
	  case -1:
		// tidy or whatever is necessary
		pthread_exit(0); // exit the thread signalling normal return
		break;
	  case 1: //
		// do something special
		
		break;
	  }
	  sleep(1);
	}
}

/// main(): mein program.. setup threads and keep loop for user/system break
///
/// Arguments: - Argument values not defined yet
///
/// Return value: Exit code - 0 for no error - EXIT_SUCCESS
int main(int argc, char **argv)
{
	
	printDbg("Starting main PID: %d\n", getpid());
	printDbg("%s V %1.2f\n", PRGNAME, VERSION);	
	printDbg("Source compilation date: %s\n\n", __DATE__);

	// TODO: ADD check for SYS_NICE
	// TODO: ADD check for task prio

	// gather actual information at startup, prepare environment
	prepareEnvironment();

	pthread_t thread1, thread2;
	int t_stat1 = 0; // we control thread status
	int t_stat2 = 0; 
	int  iret1, iret2;

	/* Create independent threads each of which will execute function */
	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &t_stat1);
	iret2 = pthread_create( &thread2, NULL, thread_manage, (void*) &t_stat2);
	// TODO: set thread prio and sched to RR -> maybe 

	// set interrupt sig hand
	signal(SIGINT, inthand);

	while (!stop) {
		sleep (1);
	}

	// signal shutdown to threads
	t_stat1 = -1;
	t_stat2 = -1;

	// wait until threads have stopped
	pthread_join( thread1, NULL);
	pthread_join( thread2, NULL); 

    printDbg("exiting safely\n");
    return 0;
}


