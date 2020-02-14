/* 
###############################
# test script by Florian Hofer
# last change: 25/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/orchestrator/manage.h"
#include "../../src/include/kernutil.h"
#include "../../src/include/rt-sched.h"
#include <check.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <limits.h>
#include <sys/resource.h>
#include <linux/sched.h>	// Linux specific scheduling
#include <poll.h>

#define TESTCPU "1"

static void orchestrator_manage_setup() {
	prgset = malloc (sizeof(prgset_t));
	parse_config_set_default(prgset);
	prgset->affinity= TESTCPU; // todo, detect
	prgset->affinity_mask = parse_cpumask(prgset->affinity);
	prgset->ftrace = 0;

	contparm = malloc (sizeof(containers_t));
	contparm->img = NULL; // locals are not initialized
	contparm->pids = NULL;
	contparm->cont = NULL;
	contparm->nthreads = 0;
	contparm->num_cont = 0;
}

static void orchestrator_manage_teardown() {
	free(prgset);
	free(contparm);
}

/// TEST CASE -> test read of runparameters of detected pid list
/// EXPECTED -> 3 elements show changed runtimes and/or deadlines
START_TEST(orchestrator_ftrace_readdata)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 0;
	prgset->ftrace = _i; // iteration 0 = debug, iteration 1 = ftrace

	const char * pidsig[] = {	"chrt -r 1 taskset -c " TESTCPU " sh -c \"for i in {1..10}; do sleep 1; echo 'test1'; done\"",
								"chrt -r 2 taskset -c " TESTCPU " sh -c \"for i in {1..10}; do sleep 1; echo 'test2'; done\"",
								"chrt -r 3 taskset -c " TESTCPU  " sh -c \"for i in {1..10}; do sleep 1; echo 'test3'; done\"",
								NULL };

	int sz_test = sizeof(pidsig)/sizeof(*pidsig)-1;
	FILE * fd[sz_test];
	pid_t pid[sz_test];

	{
		int i =0;
		while (pidsig[i]) {
			// new pid

			fd[i] = popen2(pidsig[i], "r", &pid[i]);

			printf("created pid %d\n", pid[i]);
			node_push(&nhead);
			nhead->pid = pid[i];
			nhead->psig = strdup(pidsig[i]);

			i++;
		}
	}

	iret1 = pthread_create( &thread1, NULL, thread_manage, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(8);
//	// set stop sig
	stat1 = -1;

	for (int i=0; i< sz_test; i++)
		pclose2(fd[i], pid[i], SIGINT); // send SIGINT = CTRL+C to watch instances

	if (!iret1) // thread started successfully
		iret1 = pthread_join(thread1, NULL); // wait until end

	ck_assert_int_lt(0, nhead->mon.dl_count);
	ck_assert_int_lt(0, nhead->next->mon.dl_count);
	ck_assert_int_lt(0, nhead->next->next->mon.dl_count);

	ck_assert_int_lt(0, nhead->mon.rt_avg);
	ck_assert_int_lt(0, nhead->next->mon.rt_avg);
	ck_assert_int_lt(0, nhead->next->next->mon.rt_avg);

	ck_assert_int_le(0, nhead->mon.rt_min);
	ck_assert_int_le(0, nhead->next->mon.rt_min);
	ck_assert_int_le(0, nhead->next->next->mon.rt_min);

	// free memory
	while (nhead)
		node_pop(&nhead);

}
END_TEST

/// TEST CASE -> Stop manage thread when setting status to -1
/// EXPECTED -> exit after 2 seconds, no error
/// iteration 0 = debug, iteration 1 = function trace
START_TEST(orchestrator_manage_stop)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 0;
	prgset->ftrace = _i; // iteration 0 = debug, iteration 1 = ftrace

	iret1 = pthread_create( &thread1, NULL, thread_manage, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(2);
	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

void orchestrator_manage (Suite * s) {
	TCase *tc1 = tcase_create("manage_thread_stop");

	tcase_add_checked_fixture(tc1, orchestrator_manage_setup, orchestrator_manage_teardown);
	tcase_add_loop_exit_test(tc1, orchestrator_manage_stop, EXIT_SUCCESS, 0, 2);
	suite_add_tcase(s, tc1);

	TCase *tc2 = tcase_create("manage_thread_read");
	tcase_add_checked_fixture(tc2, orchestrator_manage_setup, orchestrator_manage_teardown);
	tcase_add_loop_test(tc2, orchestrator_ftrace_readdata, 0, 2);
	tcase_set_timeout(tc2, 10);
    suite_add_tcase(s, tc2);

	return;
}
