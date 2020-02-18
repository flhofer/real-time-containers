/* 
###############################
# test script by Florian Hofer
# last change: 15/02/2020
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/include/dockerlink.h"
#include <check.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update

static char * dockerlink_empty [6] = {
	"",			// empty
	"\n",		// CR
	"\0",		// null string
	"{}",		// empty declaration
	"{\n \"type\" : NULL\n }",	// section should be present
	"{\n \"type\" : \"container\",\n }", // empty section test
	};

static const char * dockerlink_events [6] = {
	"{\"status\":\"kill\",\"id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"from\":\"testcnt\",\"Type\":\"container\",\"Action\":\"kill\",\"Actor\":{\"ID\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"Attributes\":{\"image\":\"testcnt\",\"it.florianhofer.release-date\":\"2019-04-12\",\"it.florianhofer.version\":\"0.2.0\",\"it.florianhofer.version.is-production\":\"no\",\"name\":\"rt-app-tst-10\",\"signal\":\"15\",\"vendor1\":\"Florian Hofer\",\"vendor2\":\"Florian Hofer\"}},\"scope\":\"local\",\"time\":1563572495,\"timeNano\":1563572495142834428}",
	"{\"status\":\"die\",\"id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"from\":\"testcnt\",\"Type\":\"container\",\"Action\":\"die\",\"Actor\":{\"ID\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"Attributes\":{\"exitCode\":\"0\",\"image\":\"testcnt\",\"it.florianhofer.release-date\":\"2019-04-12\",\"it.florianhofer.version\":\"0.2.0\",\"it.florianhofer.version.is-production\":\"no\",\"name\":\"rt-app-tst-10\",\"vendor1\":\"Florian Hofer\",\"vendor2\":\"Florian Hofer\"}},\"scope\":\"local\",\"time\":1563572495,\"timeNano\":1563572495313179855}",
	"{\"Type\":\"network\",\"Action\":\"disconnect\",\"Actor\":{\"ID\":\"a1ff03da79394dc71ea4b121f15ebfe58554f414a4ef4cedf8a54df4712b43d5\",\"Attributes\":{\"container\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"name\":\"bridge\",\"type\":\"bridge\"}},\"scope\":\"local\",\"time\":1563572495,\"timeNano\":1563572495458969571}",
	"{\"status\":\"stop\",\"id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"from\":\"testcnt\",\"Type\":\"container\",\"Action\":\"stop\",\"Actor\":{\"ID\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"Attributes\":{\"image\":\"testcnt\",\"it.florianhofer.release-date\":\"2019-04-12\",\"it.florianhofer.version\":\"0.2.0\",\"it.florianhofer.version.is-production\":\"no\",\"name\":\"rt-app-tst-10\",\"vendor1\":\"Florian Hofer\",\"vendor2\":\"Florian Hofer\"}},\"scope\":\"local\",\"time\":1563572495,\"timeNano\":1563572495533016142}",
	"{\"Type\":\"network\",\"Action\":\"connect\",\"Actor\":{\"ID\":\"a1ff03da79394dc71ea4b121f15ebfe58554f414a4ef4cedf8a54df4712b43d5\",\"Attributes\":{\"container\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"name\":\"bridge\",\"type\":\"bridge\"}},\"scope\":\"local\",\"time\":1563572500,\"timeNano\":1563572500962449913}",
	"{\"status\":\"start\",\"id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"from\":\"testcnt\",\"Type\":\"container\",\"Action\":\"start\",\"Actor\":{\"ID\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"Attributes\":{\"image\":\"testcnt\",\"it.florianhofer.release-date\":\"2019-04-12\",\"it.florianhofer.version\":\"0.2.0\",\"it.florianhofer.version.is-production\":\"no\",\"name\":\"rt-app-tst-10\",\"vendor1\":\"Florian Hofer\",\"vendor2\":\"Florian Hofer\"}},\"scope\":\"local\",\"time\":1563572501,\"timeNano\":1563572501557282644}" 
	};

static contevent_t cntexpected[6] = {
	{ cnt_remove, "rt-app-tst-10", "4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29", "testcnt", 1563572495142834428},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{ cnt_add, "rt-app-tst-10", "4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29", "testcnt", 1563572501557282644},
	};

static void checkContainer(contevent_t * cntevent) {

	usleep(1000);
	(void)pthread_mutex_lock(&containerMutex);
			
	while (!containerEvent) {
		(void)pthread_mutex_unlock(&containerMutex);

		// if no event, and we did't expect one. return
		if (!cntevent->id){
			ck_assert(!containerEvent);
			return;
		}

		usleep(1000);
		(void)pthread_mutex_lock(&containerMutex);
	}

	ck_assert(containerEvent);
	ck_assert(containerEvent->name);
	ck_assert(containerEvent->id);
	ck_assert(containerEvent->image);

	ck_assert_int_eq(cntevent->event, containerEvent->event);
	ck_assert_str_eq(cntevent->name, containerEvent->name);
	ck_assert_str_eq(cntevent->id, containerEvent->id);
	ck_assert_str_eq(cntevent->image, containerEvent->image);
	ck_assert_int_eq(cntevent->timenano, containerEvent->timenano);

	// cleanup
	free(containerEvent);
	containerEvent = NULL;
	(void)pthread_mutex_unlock(&containerMutex);

}

/// TEST CASE -> cycle through invalid JSON events
/// EXPECTED -> immediate response, error exit
/// NOTES -> thread exits with invalid format
START_TEST(dockerlink_err_json)
{	
	pthread_t thread1;
	int  iret1;
	char buf[50] = "echo '";
	strcat(strcat(buf, dockerlink_empty[_i]), "'");
	iret1 = pthread_create( &thread1, NULL, thread_watch_docker, (void*) buf);
	ck_assert_int_eq(iret1, 0);
	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

/// TEST CASE -> cycle through events
/// EXPECTED -> immediate container response for 0 and 5 only
/// NOTES -> connection/test should end, thread exits when pipe dies
START_TEST(dockerlink_conf)
{	
	pthread_t thread1;
	int  iret1;
	char buf[1024] = "echo '";
	strcat(strcat(buf, dockerlink_events[_i]), "'");
	iret1 = pthread_create( &thread1, NULL, thread_watch_docker, (void*) buf);
	ck_assert_int_eq(iret1, 0);
	checkContainer(&cntexpected[_i]);
	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL ); // wait until end
}
END_TEST

/// TEST CASE -> cycle through events, wait 1 second with pipe open
/// EXPECTED -> immediate container response for 0 and 5 only
///				thread closed after a second
/// NOTES -> connection/test should end after delay
/// 		thread exits when pipe dies
START_TEST(dockerlink_conf_att)
{	
	pthread_t thread1;
	int  iret1;
	char buf[1024] = "echo '";
	strcat(strcat(buf, dockerlink_events[_i]), "' && sleep 1");
	iret1 = pthread_create( &thread1, NULL, thread_watch_docker, (void*) buf);
	ck_assert_int_eq(iret1, 0);
	checkContainer(&cntexpected[_i]);
	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

/// TEST CASE -> DUMP all events at once
/// EXPECTED -> same behavior as if with delay
/// NOTES -> connection/test should end after delay
/// 		thread exits when pipe dies
START_TEST(dockerlink_conf_dmp)
{	
	pthread_t thread1;
	int  iret1;
	char buf[4096] = "echo '";
	for (int i=0; i<6; i++) {
		strcat(buf, dockerlink_events[i]);
		if (i==5) 
			break;
		strcat(buf, "' && sleep 0.5 && echo '");
	}		
	strcat(buf, "'");
	iret1 = pthread_create( &thread1, NULL, thread_watch_docker, (void*) buf);
	ck_assert_int_eq(iret1, 0);

	checkContainer(&cntexpected[0]);
	checkContainer(&cntexpected[5]);

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

/// TEST CASE -> Stop link thread on signal
/// EXPECTED -> exit after 2 seconds, no error
START_TEST(dockerlink_stop)
{
	pthread_t thread1;
	int  iret1;
	char buf[10] = "sleep 10"; // Timeout is set to 4 secs by default
	iret1 = pthread_create( &thread1, NULL, thread_watch_docker, (void*) buf);
	ck_assert_int_eq(iret1, 0);

	sleep(2);
	// set stop signal
	(void)pthread_kill (thread1, SIGHUP); // tell linking threads to stop

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end

}
END_TEST

/// TEST CASE -> Stop link thread on signal
/// EXPECTED -> exit after 2 seconds, no error
START_TEST(dockerlink_startfail)
{	
	pthread_t thread1;
	int  iret1;
	char buf[10] = "doucqeker"; // non existing command
	iret1 = pthread_create( &thread1, NULL, thread_watch_docker, (void*) buf);
	ck_assert_int_eq(iret1, 0);

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end

}
END_TEST

/*
static char * dockerlink_cevents [6] = {
	"2019-07-22 14:59:10.335889938 +0000 UTC moby /containers/create {\"id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"runtime\":{\"name\":\"io.containerd.runtime.v1.linux\",\"options\":{\"type_url\":\"containerd.linux.runc.RuncOptions\",\"value\":\"CgRydW5jEhwvdmFyL3J1bi9kb2NrZXIvcnVudGltZS1ydW5j\"}}}",
	"2019-07-22 14:59:11.117134632 +0000 UTC moby /tasks/create {\"container_id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"bundle\":\"/run/containerd/io.containerd.runtime.v1.linux/moby/4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"io\":{\"stdout\":\"/var/run/docker/containerd/4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29/init-stdout\",\"stderr\":\"/var/run/docker/containerd/4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29/init-stderr\"},\"pid\":10019}",
	"2019-07-22 14:59:11.135907274 +0000 UTC moby /tasks/start {\"container_id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"pid\":10019}",
	"2019-07-22 15:00:57.395828623 +0000 UTC moby /tasks/exit {\"container_id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"pid\":10019,\"exited_at\":\"2019-07-22T15:00:57.332138232Z\"}",
	"2019-07-22 15:00:57.461623225 +0000 UTC moby /tasks/delete {\"container_id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"pid\":10019,\"exited_at\":\"2019-07-22T15:00:57.332138232Z\"}",
	"2019-07-22 15:00:57.890534722 +0000 UTC moby /containers/delete {\"id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\"}"
	};
*/

void library_dockerlink (Suite * s) {
	TCase *tc1 = tcase_create("dockerlink_json");
 
	tcase_add_loop_exit_test(tc1, dockerlink_err_json, EXIT_INV_CONFIG, 0, 6);
	tcase_add_loop_test(tc1, dockerlink_conf, 0, 6);
	tcase_add_loop_test(tc1, dockerlink_conf_att, 0, 6);
	tcase_add_test(tc1, dockerlink_conf_dmp);

    suite_add_tcase(s, tc1);

	TCase *tc2 = tcase_create("dockerlink_startstop");
	tcase_add_exit_test(tc2, dockerlink_stop, EXIT_SUCCESS);
	tcase_add_exit_test(tc2, dockerlink_startfail, EXIT_SUCCESS);

	suite_add_tcase(s, tc2);

	return;
}
