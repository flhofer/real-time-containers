/* 
###############################
# test script by Florian Hofer
# last change: 25/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "updateTest.h"
#include "../test.h"

// Includes from orchestrator library
#include "../../src/include/parse_config.h"
#include "../../src/include/kernutil.h"
#include "../../src/include/rt-sched.h"

// tested
#include "../../src/orchestrator/update.c"

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
	prgset = calloc (1, sizeof(prgset_t));
	parse_config_set_default(prgset);

	prgset->affinity = strdup("0");
	prgset->affinity_mask = parse_cpumask(prgset->affinity);

	prgset->ftrace = 1;

	// signatures and folders
	prgset->cont_ppidc = strdup(CONT_PPID);
	prgset->cont_pidc = strdup(CONT_PID);
	prgset->cont_cgrp = strdup(CGRP_DCKR);

	// filepaths virtual file system
	prgset->procfileprefix = strdup("/proc/sys/kernel/");
	prgset->cgroupfileprefix = strdup("/sys/fs/cgroup/");
	prgset->cpusystemfileprefix = strdup("/sys/devices/system/cpu/");

	parse_dockerfileprefix(prgset);

	contparm = calloc (1, sizeof(containers_t));
}

static void orchestrator_update_teardown() {
	// free memory
	while (nhead)
		node_pop(&nhead);

	freePrgSet(prgset);
	freeContParm(contparm);
}

/// TEST CASE -> test detected pid list using pid signture and ps
/// EXPECTED -> 3 elements detectes (and no leaks!)
START_TEST(orchestrator_update_getpids)
{
	// TODO: extend with shim and subprocess examples
	pid_t pid1, pid2, pid3;
	FILE * fd1, * fd2,  * fd3;

	char pid[CMD_LEN];

	// create pids
	fd1 = popen2("sleep 4", "r", &pid1);
	fd2 = popen2("sleep 2", "r", &pid2);
	fd3 = popen2("sleep 5", "r", &pid3);
	// set detect mode to pid
	free (prgset->cont_pidc);
	prgset->cont_pidc = strdup("sleep");
#ifdef BUSYBOX
	(void)sprintf(pid, "| grep -E '%s'", prgset->cont_pidc);
#else
	(void)sprintf(pid, "-C %s", prgset->cont_pidc);
#endif
	usleep(1000); // wait for process creation // yield

	selectUpdate();

	getPids(&nhead, pid, NULL);

	// verify 2 nodes exist
	ck_assert(nhead);
	ck_assert(nhead->next);
	ck_assert(nhead->next->next);
	ck_assert(!nhead->next->next->next);

	// verify pids
	ck_assert_int_eq(nhead->next->next->pid, pid1);
	ck_assert_int_eq(nhead->next->pid, pid2);
	ck_assert_int_eq(nhead->pid, pid3);

	pclose2(fd1, pid1, SIGINT); // close pipe
	pclose2(fd2, pid2, SIGINT); // close pipe
	pclose2(fd3, pid3, SIGINT); // close pipe
}
END_TEST

/// TEST CASE -> test insert/remove from list
/// EXPECTED -> 3 elements detected, than 1 removes, than 1 inserted
START_TEST(orchestrator_update_scannew)
{
	// TODO: extend with shim and subprocess examples
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

	selectUpdate();

	usleep(1000); // wait for process creation // yield
	scanNew();

	// verify 3 nodes exist
	ck_assert(nhead);
	ck_assert(nhead->next);
	ck_assert(nhead->next->next);
	ck_assert(!nhead->next->next->next);

	// verify pids
	ck_assert_int_eq(nhead->next->next->pid, pid1);
	ck_assert_int_eq(nhead->next->pid, pid2);
	ck_assert_int_eq(nhead->pid, pid3);


	pclose2(fd2, pid2, SIGINT); // send SIGINT = CTRL+C to sleep instances

	scanNew();

	// verify PIDs
	ck_assert_int_eq(nhead->next->pid, pid1);
	ck_assert_int_eq(nhead->pid, pid3);

	fd2 = popen2("sleep 3", "r", &pid2);
	usleep(1000); // wait for process creation // yield

	scanNew();

	// verify 3 nodes exist
	ck_assert(nhead);
	ck_assert(nhead->next);
	ck_assert(nhead->next->next);
	ck_assert(!nhead->next->next->next);

	// verify pids
	ck_assert_int_eq(nhead->next->next->pid, pid1);
	ck_assert_int_eq(nhead->next->pid, pid3);
	ck_assert_int_eq(nhead->pid, pid2);


	pclose2(fd1, pid1, SIGINT); // close pipe
	pclose2(fd2, pid2, SIGINT); // close pipe
	pclose2(fd3, pid3, SIGINT); // close pipe
}
END_TEST

/// TEST CASE -> fill link event structure and test passing/parameters
/// EXPECTED ->  resources set and all freed
START_TEST(orchestrator_update_dlinkread)
{
	containerEvent = malloc (sizeof (struct cont_event));
	containerEvent->event = cnt_add;
	containerEvent->id = strdup("1232144314");
	containerEvent->name = strdup("testcont");
	containerEvent->image = strdup("testimg");

	selectUpdate();
	updateDocker();

    // TODO: expand -- use existing id
	ck_assert_ptr_null(contparm->cont);
}
END_TEST


/// TEST CASE -> Stop update thread when setting status to -1
/// EXPECTED -> exit after 2 seconds, no error
START_TEST(orchestrator_update_stop)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 1;

	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(2);
//	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end

	node_pop(&nhead);
}
END_TEST

/// TEST CASE -> test detected pid list
/// EXPECTED -> 3 elements at first, then two with one deleted, desc order
START_TEST(orchestrator_update_findprocs)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 1;

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
	prgset->loops = 5; // shorten scan time
	
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

	// verify PIDs
	ck_assert_int_eq(nhead->next->pid, pid1);
	ck_assert_int_eq(nhead->pid, pid3);

	pclose2(fd1, pid1, SIGINT); // close pipe
	pclose2(fd3, pid3, SIGINT); // close pipe

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
	int stat1 = 1;

	// set detect mode to pid 
	free (prgset->cont_pidc);
	prgset->cont_pidc = strdup(""); // all!
	prgset->use_cgroup = DM_CMDLINE;
	prgset->loops = 5; // shorten scan time

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
	int stat1 = 1;
	pid_t pid1, pid2;
	FILE * fd1, * fd2;

	// create pids
	fd1 = popen2("sleep 4", "r", &pid1);
	fd2 = popen2("sleep 5", "r", &pid2);
	// set detect mode to pid 
	free (prgset->cont_pidc);
	prgset->cont_pidc = strdup("sleep");
	prgset->use_cgroup = DM_CMDLINE;
	prgset->loops = 5; // shorten scan time

	// push sig to config	
	contparm->rscs = malloc (sizeof(struct sched_rscs));
	contparm->rscs->affinity=0;
	contparm->rscs->affinity_mask = parse_cpumask("0");
	contparm->rscs->rt_timew=95000;
	contparm->rscs->rt_time=100000;
	contparm->rscs->mem_dataw=100;
	contparm->rscs->mem_data=-1;

	contparm->attr = calloc (1, sizeof(struct sched_attr));
	contparm->attr->size =SCHED_ATTR_SIZE;
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
		contparm->pids->status |= MSK_STATSHAT | MSK_STATSHRC;
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

	// ipdate-> the pid cfg is cloned for pids unrelated to container configs
	ck_assert(contparm->pids->next);
	ck_assert(contparm->pids->next->next);
	ck_assert_ptr_eq(nhead->param, contparm->pids->next);
	ck_assert_ptr_eq(nhead->next->param, contparm->pids);

	{
		struct rlimit rlim;		
		// RT-Time limit
		if (prlimit(pid1, RLIMIT_RTTIME, NULL, &rlim))
			err_msg_n(errno, "getting RT-Limit for PID %d", pid1);

		ck_assert_int_eq(contparm->pids->next->rscs->rt_timew, rlim.rlim_cur);
		ck_assert_int_eq(contparm->pids->next->rscs->rt_time,  rlim.rlim_max);

		if (prlimit(pid1, RLIMIT_DATA, NULL, &rlim))
			err_msg_n(errno, "getting data-Limit for PID %d", pid1);

		ck_assert_int_eq(contparm->pids->next->rscs->mem_dataw, rlim.rlim_cur);
		ck_assert_int_eq(contparm->pids->next->rscs->mem_data,  rlim.rlim_max);


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
		ck_assert(!memcmp(&attr, contparm->pids->next->attr, 48 )); //sizeof(struct sched_attr))); // TODO: Globally fix epanded struct in glibc 2.41

		if (sched_getattr (pid2, &(attr), sizeof(struct sched_attr), 0U) != 0) 
			warn("Unable to read params for PID %d: %s", pid2, strerror(errno));		

		ck_assert(!memcmp(&attr, contparm->pids->attr, 48));// sizeof(struct sched_attr))); // TODO: Globally fix epanded struct in glibc 2.41

	}
	pclose2(fd1, pid1, SIGINT); // close pipe
	pclose2(fd2, pid2, SIGINT); // close pipe

	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end

}
END_TEST

void orchestrator_update (Suite * s) {
	TCase *tc1 = tcase_create("update_newread");
	tcase_add_checked_fixture(tc1, orchestrator_update_setup, orchestrator_update_teardown);
	tcase_add_test(tc1, orchestrator_update_getpids);
//	TODO: add examples getpPids
	tcase_add_test(tc1, orchestrator_update_scannew);
	tcase_add_test(tc1, orchestrator_update_dlinkread);

	suite_add_tcase(s, tc1);

    TCase *tc2 = tcase_create("update_thread");
	tcase_add_checked_fixture(tc2, orchestrator_update_setup, orchestrator_update_teardown);
	tcase_add_exit_test(tc2, orchestrator_update_stop, EXIT_SUCCESS);
	tcase_add_test(tc2, orchestrator_update_findprocs);
	tcase_add_test(tc2, orchestrator_update_findprocsall);

    suite_add_tcase(s, tc2);

	TCase *tc3 = tcase_create("update_thread_resources");
	tcase_add_checked_fixture(tc3, orchestrator_update_setup, orchestrator_update_teardown);
	tcase_add_test(tc3, orchestrator_update_rscs);

    suite_add_tcase(s, tc3);

	return;
}
