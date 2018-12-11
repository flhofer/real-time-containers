#include <stdio.h>
#include <stdlib.h>
#include <string.h> // used for string parsing
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants

#include <sys/param.h>
#include <sys/user.h>
#include <sys/sysctl.h>


// Lookup https://www.systutorials.com/docs/linux/man/0p-unistd/

void *print_message_function( void *ptr );

// for unix systems only
/*int printprocs2()
{
  struct kinfo_proc *procs = NULL, *newprocs;
  char          thiscmd[MAXCOMLEN + 1];
  pid_t           thispid;
  int           mib[4];
  size_t                miblen;
  int           i, st, nprocs;
  size_t                size;
  
  
  size = 0;
  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_ALL;
  mib[3] = 0;
  miblen = 3;
  
  st = sysctl(mib, miblen, NULL, &size, NULL, 0);
  do {
    size += size / 10;
    newprocs = realloc(procs, size);
    if (newprocs == 0) {
      if (procs)
        free(procs);
      errx(1, "could not reallocate memory");
    }
    procs = newprocs;
    st = sysctl(mib, miblen, procs, &size, NULL, 0);
  } while (st == -1 && errno == ENOMEM);
  
  nprocs = size / sizeof(struct kinfo_proc);

  /* Now print out the data 
  for (i = 0; i < nprocs; i++) {
    thispid = procs[i].kp_proc.p_pid;
    strncpy(thiscmd, procs[i].kp_proc.p_comm, MAXCOMLEN);
    thiscmd[MAXCOMLEN] = '\0';
    printf("%d\t%s\n", thispid, thiscmd);
  }
  
  /* Clean up 
  free(procs);
  return(0);
} */


int printpids (pid_t* pidno, size_t cnt)
{
        char pidline[1024];
        char *pid;
        int i =0;
        FILE *fp = popen("pidof bash","r");
        fgets(pidline,1024,fp);
        pclose(fp);

        printf("Pid string return %s",pidline);
        pid = strtok (pidline," ");
        while(pid != NULL && i < cnt)
                {

                        *pidno = atoi(pid);
                        printf("%d\n",*pidno);
                        pid = strtok (NULL , " ");
						pidno += sizeof(pid_t);
                        i++;
                }

		// return number of elements found
		return i;
}



int main()
{
	pthread_t thread1, thread2;
	char *message1 = "Thread 1";
	char *message2 = "Thread 2";
	int  iret1, iret2;

	/* Create independent threads each of which will execute function */

	iret1 = pthread_create( &thread1, NULL, print_message_function, (void*) message1);
	iret2 = pthread_create( &thread2, NULL, print_message_function, (void*) message2);

	/* Wait till threads are complete before main continues. Unless we  */
	/* wait we run the risk of executing an exit which will terminate   */
	/* the process and all threads before the threads have completed.   */

	pthread_join( thread1, NULL);
	pthread_join( thread2, NULL); 

	printf("POSIX version: %ld\n", _POSIX_VERSION);
	printf("XOPEN version: %d\n", _XOPEN_VERSION);

	printf("XOPEN Realtime: %d %d\n", _XOPEN_REALTIME_THREADS, _POSIX_THREAD_SPORADIC_SERVER);
	printf("XOPEN Realtime opt: %ld %ld %ld %ld %ld %ld\n", _POSIX_MEMLOCK, _POSIX_MEMLOCK_RANGE, _POSIX_MESSAGE_PASSING, _POSIX_PRIORITY_SCHEDULING, _POSIX_SHARED_MEMORY_OBJECTS, _POSIX_SYNCHRONIZED_IO);

	struct timespec tt;
	

//sched_get_priority_max(int);
//sched_get_priority_min(int);
//sched_getparam(pid_t, struct sched_param *);
//sched_getscheduler(pid_t);
//sched_rr_get_interval(pid_t, struct timespec *);
//sched_setparam(pid_t, const struct sched_param *);
//sched_setscheduler(pid_t, int, const struct sched_param *);
//sched_yield(void)

	pid_t pidno[64];
	int cnt = printpids(&pidno[0], 64);

	pid_t pt = getpid();

	printf("Result: %d\n", pt);

	for (int i=0; i<cnt; i++){
		int ret = sched_rr_get_interval( pidno[i], &tt);

		printf("Result: %d %ld\n", ret, tt.tv_nsec);
	}

	printf("Thread 1 returns: %d\n",iret1);
	printf("Thread 2 returns: %d\n",iret2);
	exit(0);
}

void *print_message_function( void *ptr )
{
	char *message;
	message = (char *) ptr;
	printf("%s \n", message);
}


