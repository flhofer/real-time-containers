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
#define DBG

/* Debug printing to console or buffer ?? */
#ifdef DBG
#define printDbg printf

#else
#define printDbg //
#endif

/* Available standard calls */

//sched_get_priority_max(int);
//sched_get_priority_min(int);
//sched_getparam(pid_t, struct sched_param *);
//sched_getscheduler(pid_t);
//sched_rr_get_interval(pid_t, struct timespec *);
//sched_setparam(pid_t, const struct sched_param *);
//sched_setscheduler(pid_t, int, const struct sched_param *);
//sched_yield(void)

// signal to keep status of triggers ext SIG
volatile sig_atomic_t stop;
// mutex to avoid read while updater fills or empties existing threads
pthread_mutex_t dataMutex;

// head of pidlist
node_t * head = NULL;

// interrupt handler for infinite while loop
void inthand ( int signum ) {
	stop = 1;
}

void *thread_update (void *arg); // thread that scans peridically for new entry pids
void *thread_manage (void *arg); // thread that verifies status and allocates new threads

int getpids (pid_t *pidno, size_t cnt, char * tag)
{
	char pidline[1024];
	char req[20];
	char *pid;
	int i =0  ;
	sprintf (req,  "pidof %s", tag);
	FILE *fp = popen(req,"r");
	fgets(pidline,1024,fp);
	pclose(fp);

	printDbg("Pid string return %s", pidline);
	pid = strtok (pidline," ");
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

	printDbg("Exiting node update\n");	
}

void getinfo() {
	// get PIDs 
	pid_t pidno[MAX_PIDS];

	int cnt = getpids(&pidno[0], MAX_PIDS, "bash");
	
	struct timespec tt;
	unsigned int flags = 0;
	int ret;
	struct sched_attr * pp;
	for (int i=0; i<cnt; i++){
		printDbg("Result first scan pid %d\n", pidno[i]);		
		pp = push (&head, pidno[i]);

		ret = sched_rr_get_interval(pidno[i], &tt);
		printDbg("Result pid %d %ld: %d %ld\n", pidno[i], (long)&pidno[i], ret, tt.tv_nsec);

		ret = sched_getattr (pidno[i], pp, sizeof(node_t), flags);
		printDbg("Result: %d %d\n", ret, (*pp).size);
	}
}

/* Thread to manage pid updates */
void *thread_update (void *arg)
{
	int* pthread_state = arg;
	// initialize the thread locals
	while(1)
	{
	  switch( *pthread_state )
	  {
	  case 0: // normal thread loop
		pthread_mutex_lock(&dataMutex); // move to position once done
		scanNew();
		pthread_mutex_unlock(&dataMutex); // move to position once done
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

/* Thread to manage thread scheduling */

//  pthread_mutex_lock(&dataMutex);
//  
//  pthread_mutex_unlock(&dataMutex);

void *thread_manage (void *arg)
{
	int* pthread_state = arg;
	// initialize the thread locals
	while(1)
	{
	  switch( *pthread_state )
	  {
	  case 0: // normal thread loop
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

// main program.. setup threads and keep loop
int main(int argc, char **argv)
{
	
	printDbg("Starting main PID: %d\n", getpid());
	printDbg("%s V %1.2f\n", PRGNAME, VERSION);	
	printDbg("Source compilation date: %s\n\n", __DATE__);

	// TODO: ADD check for SYS_NICE
	// TODO: ADD check for task prio


	// gather actual information at startup
	getinfo();

	// TODO: set all non concerning tasks to background resources	


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


