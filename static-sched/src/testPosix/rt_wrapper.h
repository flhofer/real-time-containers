#include <linux/types.h>
#include <sys/syscall.h>

#define gettid() syscall(__NR_gettid)

//#define SCHED_DEADLINE	6

/* XXX use the proper syscall numbers */
#ifdef __x86_64__
#define __NR_sched_setattr		314
#define __NR_sched_getattr		315
#endif

#ifdef __i386__
#define __NR_sched_setattr		351
#define __NR_sched_getattr		352
#endif

#ifdef __arm__
#define __NR_sched_setattr		380
#define __NR_sched_getattr		381
#endif

//static volatile int done;

struct sched_attr {
	__u32 size;

	__u32 sched_policy;
	__u64 sched_flags;

	/* SCHED_NORMAL, SCHED_BATCH */
	__s32 sched_nice;

	/* SCHED_FIFO, SCHED_RR */
	__u32 sched_priority;

	/* SCHED_DEADLINE (nsec) */
	__u64 sched_runtime;
	__u64 sched_deadline;
	__u64 sched_period;
};

int sched_setattr(pid_t pid,
		const struct sched_attr *attr,
		unsigned int flags)
{
	return syscall(__NR_sched_setattr, pid, attr, flags);
}

int sched_getattr(pid_t pid,
		struct sched_attr *attr,
		unsigned int size,
		unsigned int flags)
{
	return syscall(__NR_sched_getattr, pid, attr, size, flags);
}

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

