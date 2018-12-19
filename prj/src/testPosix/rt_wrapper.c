#include "rt_wrapper.h" // temporary as libc does not include new sched yet

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
