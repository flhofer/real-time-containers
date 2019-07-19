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

static char * dockerlink_empty [5] = {
	"",			// empty
	"\n",		// CR
	"\0",		// null string
	"{}",		// empty declaration

	"{\n \"event\" : NULL\n }",	// section should be present
	};

static char * dockerlink_events [6] = {
	"{\"status\":\"kill\",\"id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"from\":\"testcnt\",\"Type\":\"container\",\"Action\":\"kill\",\"Actor\":{\"ID\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"Attributes\":{\"image\":\"testcnt\",\"it.florianhofer.release-date\":\"2019-04-12\",\"it.florianhofer.version\":\"0.2.0\",\"it.florianhofer.version.is-production\":\"no\",\"name\":\"rt-app-tst-10\",\"signal\":\"15\",\"vendor1\":\"Florian Hofer\",\"vendor2\":\"Florian Hofer\"}},\"scope\":\"local\",\"time\":1563572495,\"timeNano\":1563572495142834428}",
	"{\"status\":\"die\",\"id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"from\":\"testcnt\",\"Type\":\"container\",\"Action\":\"die\",\"Actor\":{\"ID\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"Attributes\":{\"exitCode\":\"0\",\"image\":\"testcnt\",\"it.florianhofer.release-date\":\"2019-04-12\",\"it.florianhofer.version\":\"0.2.0\",\"it.florianhofer.version.is-production\":\"no\",\"name\":\"rt-app-tst-10\",\"vendor1\":\"Florian Hofer\",\"vendor2\":\"Florian Hofer\"}},\"scope\":\"local\",\"time\":1563572495,\"timeNano\":1563572495313179855}",
	"{\"Type\":\"network\",\"Action\":\"disconnect\",\"Actor\":{\"ID\":\"a1ff03da79394dc71ea4b121f15ebfe58554f414a4ef4cedf8a54df4712b43d5\",\"Attributes\":{\"container\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"name\":\"bridge\",\"type\":\"bridge\"}},\"scope\":\"local\",\"time\":1563572495,\"timeNano\":1563572495458969571}",
	"{\"status\":\"stop\",\"id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"from\":\"testcnt\",\"Type\":\"container\",\"Action\":\"stop\",\"Actor\":{\"ID\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"Attributes\":{\"image\":\"testcnt\",\"it.florianhofer.release-date\":\"2019-04-12\",\"it.florianhofer.version\":\"0.2.0\",\"it.florianhofer.version.is-production\":\"no\",\"name\":\"rt-app-tst-10\",\"vendor1\":\"Florian Hofer\",\"vendor2\":\"Florian Hofer\"}},\"scope\":\"local\",\"time\":1563572495,\"timeNano\":1563572495533016142}",
	"{\"Type\":\"network\",\"Action\":\"connect\",\"Actor\":{\"ID\":\"a1ff03da79394dc71ea4b121f15ebfe58554f414a4ef4cedf8a54df4712b43d5\",\"Attributes\":{\"container\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"name\":\"bridge\",\"type\":\"bridge\"}},\"scope\":\"local\",\"time\":1563572500,\"timeNano\":1563572500962449913}",
	"{\"status\":\"start\",\"id\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"from\":\"testcnt\",\"Type\":\"container\",\"Action\":\"start\",\"Actor\":{\"ID\":\"4cf50eb963ca612f267cfb5890154afabcd1aa931d7e791f5cfee22bef698c29\",\"Attributes\":{\"image\":\"testcnt\",\"it.florianhofer.release-date\":\"2019-04-12\",\"it.florianhofer.version\":\"0.2.0\",\"it.florianhofer.version.is-production\":\"no\",\"name\":\"rt-app-tst-10\",\"vendor1\":\"Florian Hofer\",\"vendor2\":\"Florian Hofer\"}},\"scope\":\"local\",\"time\":1563572501,\"timeNano\":1563572501557282644}" 
	};

contevent_t cntexpected[6] = {
	{},
	};


START_TEST(dockerlink_err_json)
{	
	pthread_t thread1;
	int  iret1;
	char buf[10] = "echo '";
	strcat(strcat(buf, dockerlink_empty[_i]), "'");
	iret1 = pthread_create( &thread1, NULL, thread_watch_docker, (void*) buf);
	ck_assert_int_eq(iret1, 0);
	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

START_TEST(dockerlink_conf)
{	
	pthread_t thread1;
	contevent_t * cntevent = &cntexpected[_i];
	int  iret1;
	char buf[4096] = "echo '";
	strcat(strcat(buf, dockerlink_events[_i]), "'");
	iret1 = pthread_create( &thread1, NULL, thread_watch_docker, (void*) buf);
	ck_assert_int_eq(iret1, 0);
	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end

	memcmp (
	ck_assert_mem(cntevent, containerEvent, sizeof(contevent_t));

}
END_TEST

START_TEST(dockerlink_conf_att)
{	
	pthread_t thread1;
	int  iret1;
	char buf[4096] = "echo '";
	strcat(strcat(buf, dockerlink_events[_i]), "' && sleep 1");
	iret1 = pthread_create( &thread1, NULL, thread_watch_docker, (void*) buf);
	ck_assert_int_eq(iret1, 0);
	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

START_TEST(dockerlink_conf_dmp)
{	
	pthread_t thread1;
	int  iret1;
	char buf[4096] = "echo '";
	strcat(strcat(buf, dockerlink_events[_i]), "' && sleep 1");
	iret1 = pthread_create( &thread1, NULL, thread_watch_docker, (void*) buf);
	ck_assert_int_eq(iret1, 0);
	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

void library_dockerlink (Suite * s) {
	TCase *tc1 = tcase_create("dockerlink_json");
 
	tcase_add_loop_exit_test(tc1, dockerlink_err_json, EXIT_INV_CONFIG, 0, 5);
	tcase_add_loop_test(tc1, dockerlink_conf, 0, 6);
	tcase_add_loop_test(tc1, dockerlink_conf_att, 0, 1);
	tcase_add_test(tc1, dockerlink_conf_dmp);

    suite_add_tcase(s, tc1);

	return;
}
