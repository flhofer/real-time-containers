#include "schedstat.h" // main settings and header file

#include "update.h"
#include "manage.h"


// Global variables for all the threads and programms

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

/// prepareEnvironment(): gets the list of active pids at startup, sets up
/// a CPU-shield if not present, and populates initial state of pid list
///
/// Arguments: 
///
/// Return value: 
///
void prepareEnvironment() {
	// get PIDs 
	// TODO: this will be changed
/*	pidinfo_t pidlst[MAX_PIDS];
	int flags;

	// here the other threads are not started yet.. no lock needed
	int cnt = getpids(&pidlst[0], MAX_PIDS, "bash");

	// TODO: set all non concerning tasks to background resources	
	
	// push into linked list
	for (int i=0; i<cnt; i++){
		printDbg("Result first scan pid %d\n", (pidlst +i)->pid);		
		// insert new item to list!		
		push (&head, (pidlst +i)->pid, (pidlst +i)->psig);
		// update actual parameters, gather from process
		// TODO: seems to need a bit of time when inserted, strange		
		usleep(10000);
		if (sched_getattr (head->pid, &(head->attr), sizeof(struct sched_attr), flags) != 0)
			printDbg(KMAG "Warn!" KNRM " Unable to read params for PID %d: %s\n", head->pid, strerror(errno));		
		
		if (flags != head->attr.sched_flags)
			// TODO: strangely there is a type mismatch
			printDbg(KMAG "Warn!" KNRM " Flags %d do not match %ld\n", flags, head->attr.sched_flags);		
	}*/
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
	printDbg("Source compilation date: %s\n", __DATE__);
	printDbg("This software comes with no waranty. Please be careful\n\n");

	// TODO: ADD check for SYS_NICE
	// TODO: ADD check for task prio

	// gather actual information at startup, prepare environment
	prepareEnvironment();

	pthread_t thread1, thread2;
	int32_t t_stat1 = 0; // we control thread status 32bit to be sure read is atomic on 32 bit -> sm on treads
	int32_t t_stat2 = 0; 
	int  iret1, iret2;

	/* Create independent threads each of which will execute function */
	iret1 = pthread_create( &thread1, NULL, thread_manage, (void*) &t_stat1);
	iret2 = pthread_create( &thread2, NULL, thread_update, (void*) &t_stat2);
	// TODO: set thread prio and sched to RR -> maybe 

	// set interrupt sig hand
	signal(SIGINT, inthand);

	while (!stop && (t_stat1 != -1 || t_stat2 != -1)) {
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
