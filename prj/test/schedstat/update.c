/* 
###############################
# test script by Florian Hofer
# last change: 25/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/schedstat/schedstat.h"
#include "../../src/schedstat/update.h"
#include "../../src/include/parse_config.h"
#include "../../src/include/kernutil.h"
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update

static void schedstat_update_setup() {
	prgset = malloc (sizeof(prgset_t));
	parse_config_set_default(prgset);

	prgset->logdir = strdup("./");
	prgset->logbasename = strdup("orchestrator.txt");
	prgset->logsize = 0;

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

static void schedstat_update_teardown() {
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
START_TEST(schedstat_update_stop)
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
START_TEST(schedstat_update_findprocs)
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
	ck_assert(head);
	ck_assert(head->next);
	ck_assert(head->next->next);
	ck_assert(!head->next->next->next);

	// verify pids
	ck_assert_int_eq(head->next->next->pid, pid1);
	ck_assert_int_eq(head->next->pid, pid2);
	ck_assert_int_eq(head->pid, pid3);

	pclose2(fd2, pid2, 0);
	sleep(1);

	// verify pids
	ck_assert_int_eq(head->next->pid, pid1);
	ck_assert_int_eq(head->pid, pid3);

	// TODO: verify if threads remain defunct
	pclose(fd1);
	pclose(fd3);

	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

/// TEST CASE -> test will all pids on machine
/// EXPECTED -> adding and removing of pidof sequences
START_TEST(schedstat_update_findprocsall)
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


void schedstat_update (Suite * s) {
	TCase *tc1 = tcase_create("update_thread");
 
	tcase_add_checked_fixture(tc1, schedstat_update_setup, schedstat_update_teardown);
	tcase_add_exit_test(tc1, schedstat_update_stop, EXIT_SUCCESS);
	tcase_add_test(tc1, schedstat_update_findprocs);
	tcase_add_test(tc1, schedstat_update_findprocsall);

    suite_add_tcase(s, tc1);

	return;
}