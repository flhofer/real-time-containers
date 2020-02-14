/* 
###############################
# test script by Florian Hofer
# last change: 25/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/orchestrator/update.h"
#include "../../src/include/parse_config.h"
#include "../../src/include/kernutil.h"
#include "../../src/include/rt-sched.h"
#include <check.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <limits.h>
#include <sys/resource.h>
#include <linux/sched.h>	// linux specific scheduling

// for MUSL based systems
#ifndef RLIMIT_RTTIME
	#define RLIMIT_RTTIME 15
#endif

static void orchestrator_update_setup() {
	prgset = malloc (sizeof(prgset_t));
	parse_config_set_default(prgset);

	prgset->affinity= "0"; // todo, detect
	prgset->affinity_mask = parse_cpumask(prgset->affinity);


	prgset->logdir = strdup("./");
	prgset->logbasename = strdup("orchestrator.txt");
	prgset->logsize = 0;
	prgset->ftrace = 1;

	// signatures and folders
	prgset->cont_ppidc = strdup(CONT_PPID);
	prgset->cont_pidc = strdup(CONT_PID);
	prgset->cont_cgrp = strdup(CONT_DCKR);

	// filepaths virtual file system
	prgset->procfileprefix = strdup("/proc/sys/kernel/");
	prgset->cpusetfileprefix = strdup("/sys/fs/cgroup/cpuset/");
	prgset->cpusystemfileprefix = strdup("/sys/devices/system/cpu/");

	prgset->cpusetdfileprefix = malloc(strlen(prgset->cpusetfileprefix) + strlen(prgset->cont_cgrp)+1);
	*prgset->cpusetdfileprefix = '\0'; // set first chat to null
	prgset->cpusetdfileprefix = strcat(strcat(prgset->cpusetdfileprefix, prgset->cpusetfileprefix), prgset->cont_cgrp);

	contparm = malloc (sizeof(containers_t));
	contparm->img = NULL; // locals are not initialized
	contparm->pids = NULL;
	contparm->cont = NULL;
	contparm->nthreads = 0;
	contparm->num_cont = 0;
}

static void orchestrator_update_teardown() {
	free(prgset->logdir);
	free(prgset->logbasename);

	// signatures and folders
	free(prgset->cont_ppidc);
	free(prgset->cont_pidc);
	free(prgset->cont_cgrp);

	// filepaths virtual file system
	free(prgset->procfileprefix);
	free(prgset->cpusetfileprefix);
	free(prgset->cpusystemfileprefix);

	free(prgset->cpusetdfileprefix);

	free(prgset);

	free(contparm);
}

/// TEST CASE -> Stop update thread when setting status to -1
/// EXPECTED -> exit after 2 seconds, no error
START_TEST(orchestrator_update_stop)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 0;
	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(2);
//	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

/// TEST CASE -> test detected pid list
/// EXPECTED -> 3 elements at first, then two with one deleted, desc order
START_TEST(orchestrator_update_findprocs)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 0;
	pid_t pid1, pid2, pid3;
	FILE * fd1, * fd2,  * fd3;

	// create pids
	fd1 = popen2("sleep 4", "r", &pid1);
	fd2 = popen2("sleep 2", "r", &pid2);
	fd3 = popen2("sleep 5", "r", &pid3);
	// set detect mode to pid 
	free (prgset->cont_pidc);
	prgset->cont_pidc = strdup("sleep");
	prgset->use_cgroup = DM_CMDLINE;
	
	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(1);

	// verify 2 nodes exist
	ck_assert(nhead);
	ck_assert(nhead->next);
	ck_assert(nhead->next->next);
	ck_assert(!nhead->next->next->next);

	// verify pids
	ck_assert_int_eq(nhead->next->next->pid, pid1);
	ck_assert_int_eq(nhead->next->pid, pid2);
	ck_assert_int_eq(nhead->pid, pid3);

	pclose2(fd2, pid2, SIGINT); // send SIGINT = CTRL+C to sleep instances
	sleep(1);

	// verify pids
	ck_assert_int_eq(nhead->next->pid, pid1);
	ck_assert_int_eq(nhead->pid, pid3);

	// TODO: verify if threads remain defunct
	pclose(fd1); // close pipe of sleep instance = HUP
	pclose(fd3); // close pipe of sleep instance = HUP

	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

/// TEST CASE -> test will all pids on machine
/// EXPECTED -> adding and removing of pidof sequences
START_TEST(orchestrator_update_findprocsall)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 0;
	
	// set detect mode to pid 
	free (prgset->cont_pidc);
	prgset->cont_pidc = strdup(""); // all!
	prgset->use_cgroup = DM_CMDLINE;
	
	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(3); // 4 seconds timeout

	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

/// TEST CASE -> test assign resources
/// EXPECTED -> resources should match settings
START_TEST(orchestrator_update_rscs)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 0;
	pid_t pid1, pid2;
	FILE * fd1, * fd2;

	// create pids
	fd1 = popen2("sleep 4", "r", &pid1);
	fd2 = popen2("sleep 5", "r", &pid2);
	// set detect mode to pid 
	free (prgset->cont_pidc);
	prgset->cont_pidc = strdup("sleep");
	prgset->use_cgroup = DM_CMDLINE;

	// push sig to config	
	contparm->rscs = malloc (sizeof(struct sched_rscs));
	contparm->rscs->affinity=0;
	contparm->rscs->rt_timew=95000;
	contparm->rscs->rt_time=100000;
	contparm->rscs->mem_dataw=100;
	contparm->rscs->mem_data=-1;

	contparm->attr = malloc (sizeof(struct sched_attr));
	contparm->attr->size =48;
	contparm->attr->sched_policy=SCHED_BATCH;
	contparm->attr->sched_flags=SCHED_FLAG_RESET_ON_FORK;
	contparm->attr->sched_nice=5;
	contparm->attr->sched_priority=0;
	contparm->attr->sched_runtime=0;
	contparm->attr->sched_deadline=0;
	contparm->attr->sched_period=0;

	const char *pids[] = {	"sleep",
							NULL };

	const char ** pidsig = pids;
	while (*pidsig) {
		// new pid
		push((void**)&contparm->pids, sizeof(pidc_t));
		contparm->pids->psig = strdup(*pidsig);
		contparm->pids->attr = contparm->attr;
		contparm->pids->rscs = contparm->rscs;
		pidsig++;
	}


	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(1);

	// verify 2 nodes exist
	ck_assert(nhead);
	ck_assert(nhead->next);
	ck_assert(!nhead->next->next);

	// verify pids
	ck_assert_int_eq(nhead->next->pid, pid1);
	ck_assert_int_eq(nhead->pid, pid2);

	ck_assert_ptr_eq(nhead->param, contparm->pids);
	ck_assert_ptr_eq(nhead->next->param, contparm->pids);

	{
		struct rlimit rlim;		
		// RT-Time limit
		if (prlimit(pid1, RLIMIT_RTTIME, NULL, &rlim))
			err_msg_n(errno, "getting RT-Limit for PID %d", pid1);

		ck_assert_int_eq(contparm->pids->rscs->rt_timew, rlim.rlim_cur);
		ck_assert_int_eq(contparm->pids->rscs->rt_time,  rlim.rlim_max);

		if (prlimit(pid1, RLIMIT_DATA, NULL, &rlim))
			err_msg_n(errno, "getting data-Limit for PID %d", pid1);

		ck_assert_int_eq(contparm->pids->rscs->mem_dataw, rlim.rlim_cur);
		ck_assert_int_eq(contparm->pids->rscs->mem_data,  rlim.rlim_max);


		if (prlimit(pid2, RLIMIT_RTTIME, NULL, &rlim))
			err_msg_n(errno, "getting RT-Limit for PID %d", pid2);

		ck_assert_int_eq(contparm->pids->rscs->rt_timew, rlim.rlim_cur);
		ck_assert_int_eq(contparm->pids->rscs->rt_time,  rlim.rlim_max);

		if (prlimit(pid2, RLIMIT_DATA, NULL, &rlim))
			err_msg_n(errno, "getting data-Limit for PID %d", pid2);

		ck_assert_int_eq(contparm->pids->rscs->mem_dataw, rlim.rlim_cur);
		ck_assert_int_eq(contparm->pids->rscs->mem_data,  rlim.rlim_max);
	}

	{
		struct sched_attr attr;
		if (sched_getattr (pid1, &(attr), sizeof(struct sched_attr), 0U) != 0) 
			warn("Unable to read params for PID %d: %s", pid1, strerror(errno));		
		ck_assert(!memcmp(&attr, contparm->pids->attr, sizeof(struct sched_attr)));

		if (sched_getattr (pid2, &(attr), sizeof(struct sched_attr), 0U) != 0) 
			warn("Unable to read params for PID %d: %s", pid2, strerror(errno));		

		ck_assert(!memcmp(&attr, contparm->pids->attr, sizeof(struct sched_attr)));

	}
	pclose(fd1);
	pclose(fd2);

	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end

	// free
	while (contparm->pids) {
		free(contparm->pids->psig);
		pop((void **)&contparm->pids);
	}
	free(contparm->rscs);
	free(contparm->attr);
}
END_TEST

void orchestrator_update (Suite * s) {
	TCase *tc1 = tcase_create("update_thread");
 
	// TODO: reduce verbosity on failed pids... -> update.c
	tcase_add_checked_fixture(tc1, orchestrator_update_setup, orchestrator_update_teardown);
	tcase_add_exit_test(tc1, orchestrator_update_stop, EXIT_SUCCESS);
	tcase_add_test(tc1, orchestrator_update_findprocs);
	tcase_add_test(tc1, orchestrator_update_findprocsall);

    suite_add_tcase(s, tc1);

	TCase *tc2 = tcase_create("update_thread_resources");
 
	tcase_add_checked_fixture(tc2, orchestrator_update_setup, orchestrator_update_teardown);
	tcase_add_test(tc2, orchestrator_update_rscs);

    suite_add_tcase(s, tc2);

	return;
}
