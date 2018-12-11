#include <sched.h>
#include <linux/types.h>
#include <signal.h> // for SIGs

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // used for string parsing
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants
#include "rt_wrapper.h"

#define MAX_PIDS 64

typedef struct sched_pid {
	pid_t pid;
	struct sched_attr attr;
	struct sched_pid * next;
} node_t;

node_t * head = NULL;

void push_t(node_t * head, pid_t pid) {
    node_t * current = head;
    while (current->next != NULL) {
        current = current->next;
    }

    /* now we can add a new variable */
    current->next = malloc(sizeof(node_t));
    current->next->pid = pid;
    current->next->next = NULL;
}

struct sched_attr * push(node_t ** head, pid_t pid) {
    node_t * new_node;
    new_node = malloc(sizeof(node_t));

    new_node->pid = pid;
    new_node->next = *head;
    *head = new_node;
	return &new_node->attr;
}

pid_t pop(node_t ** head) {
    pid_t retval = -1;
    node_t * next_node = NULL;

    if (*head == NULL) {
        return -1;
    }

    next_node = (*head)->next;
    retval = (*head)->pid;
    free(*head);
    *head = next_node;

    return retval;
}

pid_t remove_last(node_t * head) {
    pid_t retval = 0;
    /* if there is only one item in the list, remove it */
    if (head->next == NULL) {
        retval = head->pid;
        free(head);
        return retval;
    }

    /* get to the second to last node in the list */
    node_t * current = head;
    while (current->next->next != NULL) {
        current = current->next;
    }

    /* now current points to the second to last item of the list, so let's remove current->next */
    retval = current->next->pid;
    free(current->next);
    current->next = NULL;
    return retval;

}

pid_t remove_by_index(node_t ** head, int n) {
    int i = 0;
    pid_t retval = -1;
    node_t * current = *head;
    node_t * temp_node = NULL;

    if (n == 0) {
        return pop(head);
    }

    for (i = 0; i < n-1; i++) {
        if (current->next == NULL) {
            return -1;
        }
        current = current->next;
    }

    temp_node = current->next;
    retval = temp_node->pid;
    current->next = temp_node->next;
    free(temp_node);

    return retval;
}

int remove_by_value(node_t ** head, pid_t pid) {
    node_t *previous, *current;

    if (*head == NULL) {
        return -1;
    }

    if ((*head)->pid == pid) {
        return pop(head);
    }

    previous = current = (*head)->next;
    while (current) {
        if (current->pid == pid) {
            previous->next = current->next;
            free(current);
            return pid;
        }

        previous = current;
        current  = current->next;
    }
    return -1;
}

// scroll trrough array
struct sched_attr * get_next(node_t ** act) {

    if (*act == NULL) {
        return NULL;
    }

    *act = (*act)->next;
    if (*act == NULL) {
        return NULL;
    }
    return &(*act)->attr;
}

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

// interrupt handler for infinite while loop
void inthand ( int signum ) {
	stop = 1;
}


void *print_message_function( void *ptr );
void *thread_update (void *arg); // thread that scans peridically for new entry pids
void *thread_manage (void *arg); // thread that verifies status and allocates new threads

int getpids (pid_t *pidno, size_t cnt, char * tag)
{
	char pidline[1024];
	char req[20];
	char *pid;
	int i =0;
	pid_t * pd1 = pidno;
	sprintf (req,  "pidof %s", tag);
	FILE *fp = popen(req,"r");
	fgets(pidline,1024,fp);
	pclose(fp);

//	printf("Pid string return %s", pidline);
	pid = strtok (pidline," ");
	while(pid != NULL && i < cnt)
		    {
		            *pidno = atoi(pid);
		            printf("%d %ld\n",*pidno, (long)pidno );
		            pid = strtok (NULL , " ");
					pidno += sizeof(pid_t);
		            i++;
		    }

	// return number of elements found
	return i;
}

void getinfo() {
	// get PIDs 
	long pidno[MAX_PIDS];
	int cnt = getpids(&pidno[0], MAX_PIDS, "bash");

	for (int i=0; i<cnt; i++){
		printf("Result pid %ld\n", pidno[i]);		
	}
	

	pid_t pt = getpid();
	printf("Running PID: %d\n", pt);

	struct timespec tt;
	unsigned int flags = 0;
	int ret;
	struct sched_attr * pp;
	for (int i=0; i<cnt+1; i++){
		printf("Result pid %ld\n", pidno[i]);		
		pp = push (&head, pidno[i]);

		ret = sched_rr_get_interval(pidno[i], &tt);
		printf("Result pid %ld %ld: %d %ld\n", pidno[i], (long)&pidno[i], ret, tt.tv_nsec);

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
	pthread_t thread1, thread2;
	int t_stat2 = 0; // we control thread status
	int t_stat1 = 0; 
	int  iret1, iret2;

	/* Create independent threads each of which will execute function */
	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &t_stat1);
	iret2 = pthread_create( &thread2, NULL, thread_manage, (void*) &t_stat2);

	getinfo();

	signal(SIGINT, inthand);

	stop = 1;
	while (!stop) {
		sleep (1);
	}

	t_stat1 = -1;
	t_stat2 = -1;

	// wait until threads have stopped
	pthread_join( thread1, NULL);
	pthread_join( thread2, NULL); 

    printf("exiting safely\n");
    system("pause");
    return 0;
}

void *print_message_function( void *ptr )
{
	char *message;
	message = (char *) ptr;
	printf("%s \n", message);
}


