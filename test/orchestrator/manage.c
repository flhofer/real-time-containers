/* 
###############################
# test script by Florian Hofer
# last change: 25/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/orchestrator/orchestrator.h"
#include "../../src/orchestrator/manage.h"
#include "../../src/include/kernutil.h"
#include "../../src/include/rt-sched.h"
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <limits.h>
#include <sys/resource.h>
#include <linux/sched.h>	// linux specific scheduling
#include <check.h>

/// TEST CASE -> Stop update thread when setting status to -1
/// EXPECTED -> exit after 2 seconds, no error
START_TEST(orchestrator_ftrace_stop)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 0;
	iret1 = pthread_create( &thread1, NULL, thread_manage, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(4);
//	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

/// TEST CASE -> test detected pid list
/// EXPECTED -> 3 elements at first, then two with one deleted, desc order
START_TEST(orchestrator_ftrace_readdata)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 0;

	iret1 = pthread_create( &thread1, NULL, thread_manage, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	// TODO: insert code for manage - ftrace launch
	sleep(4);
	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

void orchestrator_manage (Suite * s) {
	TCase *tc1 = tcase_create("manage_thread");
 
	// TODO: using functions of update here, fix or export
	tcase_add_checked_fixture(tc1, orchestrator_update_setup, orchestrator_update_teardown);
	tcase_add_exit_test(tc1, orchestrator_ftrace_stop, EXIT_SUCCESS);
//	tcase_add_test(tc1, orchestrator_ftrace_readdata);

    suite_add_tcase(s, tc1);

	return;
}
