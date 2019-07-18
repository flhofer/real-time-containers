/* 
###############################
# test script by Florian Hofer
# last change: 19/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/include/dockerlink.h"
#include <pthread.h>

//TODO: all tests!!!

static FILE * pp;

static char * dockerlink_files [5] = {
	"",			// empty
	"\n",		// CR
	"\0",		// null string
	"{}",		// empty declaration

	"{\n \"event\" : NULL\n }",	// section should be present
	};

static void dockerlink_tc1_startup() {
}

START_TEST(dockerlink_err_conf)
{	
	pthread_t thread1;
	int  iret1;
	char buf[4096] = "echo '";
	pp = popen (strcat(strcat(buf, dockerlink_files[_i]), "' && sleep 2"), "r");
	iret1 = pthread_create( &thread1, NULL, thread_watch_docker, (void*) &pp);
	ck_assert_int_eq(iret1, 0);
	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

static void dockerlink_tc1_teardown() {
	pclose(pp);
}


void library_dockerlink (Suite * s) {
	TCase *tc1 = tcase_create("dockerlink_err_conf");
 
	tcase_add_checked_fixture(tc1, dockerlink_tc1_startup, dockerlink_tc1_teardown);
	tcase_add_loop_exit_test(tc1, dockerlink_err_conf, EXIT_SUCCESS, 0, 5);

    suite_add_tcase(s, tc1);

	return;
}
