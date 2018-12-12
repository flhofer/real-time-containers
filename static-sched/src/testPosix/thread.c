#include <stdio.h>
#include <stdlib.h>
#include <string.h> // used for string parsing
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants
#include "pidlist.h" // memory structure to store information

#include <sched.h>
#include <linux/types.h>
#include <signal.h> // for SIGs

#define MAX_PIDS 64

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
	int i =0;
	sprintf (req,  "pidof %s", tag);
	FILE *fp = popen(req,"r");
	fgets(pidline,1024,fp);
	pclose(fp);

//	printf("Pid string return %s", pidline);
	pid = strtok (pidline," ");
	while(pid != NULL && i < cnt)
		    {
		            *pidno = atoi(pid);
//		            printf("%d\n",*pidno);
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
		printf("Result pid %d\n", pidno[i]);		
	}

	node_t *act = head, *prev = NULL;
	struct sched_attr * attr = get_node (head);
	int i = 0;
	while (act != NULL && attr != NULL && i < cnt) {
		// insert a missing item		
		if (pidno[i] < ((*act).pid)) {
			attr = insert_after(&head, &prev, pidno[i]);
			// sig here to other thread?
			i++;
		} 
		else		
		// insert a missing item		
		if (pidno[i] > ((*act).pid)) {
			int ret = drop_after(&head, &prev);
			// sig here to other thread?
		} 
		// ok, skip to next
		else {
			i++;
			prev = act; // update prev 
			attr =  get_next(&act);
		}
	}

}

void getinfo() {
	// get PIDs 
	pid_t pidno[MAX_PIDS];

	int cnt = getpids(&pidno[0], MAX_PIDS, "bash");

	for (int i=0; i<cnt; i++){
		printf("Result pid %d\n", pidno[i]);		
	}
	
	struct timespec tt;
	unsigned int flags = 0;
	int ret;
	struct sched_attr * pp;
	for (int i=0; i<cnt; i++){
		printf("Result pid %d\n", pidno[i]);		
		pp = push (&head, pidno[i]);

		ret = sched_rr_get_interval(pidno[i], &tt);
		printf("Result pid %d %ld: %d %ld\n", pidno[i], (long)&pidno[i], ret, tt.tv_nsec);

		ret = sched_getattr (pidno[i], pp, sizeof(node_t), flags);
		printf("Result: %d %d\n", ret, (*pp).size);
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
   }
}

// main program.. setup threads and keep loop
int main(int argc, char **argv)
{
	printf("Starting main PID: %d\n", getpid());

	// gather actual information at startup
	getinfo();

	pthread_t thread1, thread2;
	int t_stat1 = 1; // we control thread status
	int t_stat2 = 1; 
	int  iret1, iret2;

	/* Create independent threads each of which will execute function */
	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &t_stat1);
	iret2 = pthread_create( &thread2, NULL, thread_manage, (void*) &t_stat2);

	// set interrupt sig hand
	signal(SIGINT, inthand);

	stop = 1;
	while (!stop) {
		sleep (1);
	}

	// signal shutdown to threads
	t_stat1 = -1;
	t_stat2 = -1;

	// wait until threads have stopped
	pthread_join( thread1, NULL);
	pthread_join( thread2, NULL); 

    printf("exiting safely\n");
    system("pause");
    return 0;
}


