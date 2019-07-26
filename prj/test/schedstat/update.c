/* 
###############################
# test script by Florian Hofer
# last change: 25/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/schedstat/schedstat.h"
#include "../../src/schedstat/update.h"
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update


containers_t * contparm; // container parameter settings
prgset_t * prgset; // programm setings structure

// mutex to avoid read while updater fills or empties existing threads
pthread_mutex_t dataMutex;
// head of pidlist - PID runtime and configuration details
node_t * head = NULL;

static void schedstat_update_setup() {
	contparm = malloc (sizeof(containers_t));
	prgset = malloc (sizeof(prgset_t));
	
}

static void schedstat_update_teardown() {
	free(contparm);	
	free(prgset);
}

/// TEST CASE -> Stop link thread on signal
/// EXPECTED -> exit after 2 seconds, no error
START_TEST(schedstat_update_stop)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 0;
	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(2);
	// set stop sig
	pthread_kill (thread1, SIGINT); // tell linking threads to stop

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST


void schedstat_update (Suite * s) {
	TCase *tc1 = tcase_create("update_thread");
 
	tcase_add_checked_fixture(tc1, schedstat_update_setup, schedstat_update_teardown);
	tcase_add_exit_test(tc1, schedstat_update_stop, EXIT_SUCCESS);

    suite_add_tcase(s, tc1);

	return;
}
