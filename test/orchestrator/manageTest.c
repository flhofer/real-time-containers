/* 
###############################
# test script by Florian Hofer
# last change: 18/02/2020
# ©2019 all rights reserved ☺
###############################
*/

#include "manageTest.h"
#include "../test.h"

// Includes from orchestrator library
#include "../../src/include/parse_config.h"
#include "../../src/include/kernutil.h"
#include "../../src/include/rt-sched.h"

// tested
#include "../../src/orchestrator/manage.c"

#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <limits.h>
#include <linux/sched.h>	// Linux specific scheduling

#define MAX_PATH 256
#define TESTCPU "0"

void buildEventConf(){
	push((void**)&elist_head, sizeof(struct ftrace_elist));
	elist_head->eventid = 317;
	elist_head->event = "sched_switch";
	elist_head->eventcall = pickPidInfoS;
}

void clearEventConf(){
	while (elist_head){
		//free(elist_head->event);
		while (elist_head->fields){
			free(elist_head->fields->name);
			pop((void**)&elist_head->fields);
		}
		pop((void**)&elist_head);
	}
}

static void orchestrator_manage_setup() {
	prgset = calloc (1, sizeof(prgset_t));
	parse_config_set_default(prgset);
	prgset->affinity= TESTCPU;
	prgset->affinity_mask = parse_cpumask(prgset->affinity);
	prgset->ftrace = 0;
	prgset->procfileprefix = strdup("/proc/sys/kernel/");

	contparm = calloc (1, sizeof(containers_t));
	contparm->rscs = malloc(sizeof(struct sched_rscs));
	contparm->rscs->affinity = -1;
	contparm->rscs->affinity_mask = numa_allocate_cpumask();
	copy_bitmask_to_bitmask(prgset->affinity_mask, contparm->rscs->affinity_mask);
}

static void orchestrator_manage_teardown() {
	// free memory
	while (nhead)
		node_pop(&nhead);

	freePrgSet(prgset);
	freeContParm(contparm);
}

#ifdef PRVTEST
static void orchestrator_manage_checkread(){
	ck_assert_int_lt(0, nhead->mon.dl_count);
	ck_assert_int_lt(0, nhead->next->mon.dl_count);
	ck_assert_int_lt(0, nhead->next->next->mon.dl_count);

	ck_assert_int_lt(0, nhead->mon.rt_avg);
	ck_assert_int_lt(0, nhead->next->mon.rt_avg);
	ck_assert_int_lt(0, nhead->next->next->mon.rt_avg);

	ck_assert_int_le(0, nhead->mon.rt_min);
	ck_assert_int_le(0, nhead->next->mon.rt_min);
	ck_assert_int_le(0, nhead->next->next->mon.rt_min);
}

/// TEST CASE -> test read of run parameters of detected PID list, debug output
/// EXPECTED -> 3 elements show changed run-times and/or deadlines
START_TEST(orchestrator_manage_readdata)
{
	pthread_t thread1;
	int  iret1;
	int stat1 = 0;
	prgset->ftrace = 0;

	const char * pidsig[] = {	"chrt -r 1 taskset -c " TESTCPU " sh -c 'while [ 1 ]; do echo test1 ; done'",
								"chrt -r 2 taskset -c " TESTCPU " sh -c 'while [ 1 ]; do echo test2 ; done'",
								"chrt -r 3 taskset -c " TESTCPU " sh -c 'while [ 1 ]; do echo test3 ; done'",
								NULL };

	int sz_test = sizeof(pidsig)/sizeof(*pidsig)-1;
	FILE * fd[sz_test];
	pid_t pid[sz_test];

	{
		int i =0;
		while (pidsig[i]) {
			// new PID

			fd[i] = popen2(pidsig[i], "r", &pid[i]);

			printf("created PID %d\n", pid[i]);
			node_push(&nhead);
			nhead->pid = pid[i];
			nhead->psig = strdup(pidsig[i]);

			i++;
		}
	}

	iret1 = pthread_create( &thread1, NULL, thread_manage, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(5);
	// set stop status
	stat1 = -1;

	for (int i=0; i< sz_test; i++)
		pclose2(fd[i], pid[i], SIGINT); // send SIGINT = CTRL+C to watch instances

	if (!iret1) // thread started successfully
		iret1 = pthread_join(thread1, NULL); // wait until end

	orchestrator_manage_checkread();
}
END_TEST
#endif

/// TEST CASE -> Stop manage thread when setting status to -1
/// EXPECTED -> exit after 2 seconds, no error
/// iteration 0 = debug output, iteration 1 = function trace
START_TEST(orchestrator_manage_stop)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 0;
	prgset->ftrace = _i;

	iret1 = pthread_create( &thread1, NULL, thread_manage, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(2);
	// set stop status
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

#ifdef PRVTEST
/// TEST CASE -> test read of run parameters of detected PID list, fTrace read
/// EXPECTED -> 3 elements show changed run-times and/or deadlines
START_TEST(orchestrator_manage_readftrace)
{
	pthread_t thread1;
	int  iret1;
	struct ftrace_thread fthread;
	fthread.dbgfile = malloc(MAX_PATH); // it's freed inside the thread
	fthread.cpuno = 1; // dummy value
	(void)sprintf(fthread.dbgfile, "test/manage_ftread.dat"); // dump of a kernel thread scan

	prgset->ftrace = 1;


	const int pid[] = { 1, 2, 3	}; // to do setup

	for (int i=0; i<sizeof(pid)/sizeof(int); ++i) {
		node_push(&nhead);
		nhead->pid = pid[i];
		nhead->psig = strdup("");
	}

	buildEventConf();

	iret1 = pthread_create( &thread1, NULL, thread_ftrace, (void*)&fthread);
	ck_assert_int_eq(iret1, 0);

	sleep(2);

	// tell fTrace threads to stop
	(void)pthread_kill (thread1, SIGQUIT);

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end

	clearEventConf();

	orchestrator_manage_checkread();
}
END_TEST
#endif

/// TEST CASE -> read kernel debug tracing info and prepare structures
/// EXPECTED -> parsing of addresses
START_TEST(orchestrator_manage_ftrc_cfgread)
{
	buildEventConf();
	char * buf = malloc(4096);
	FILE *f;
	int ret;

	if ((f = fopen ("test/manage_sched_switch_fmt6.5.txt","r"))) {
		ret = fread(buf, sizeof(char), 4096, f);
		ck_assert_int_ne(ret, 0);
		fclose(f);
	}
	else
		ck_abort_msg("Could not open file: %s", strerror(errno));

	parseEventFields (&elist_head->fields,buf);
	// Types:
	// 0  short, 1 int, 2 long, 3 long long, etc.
	// 10 char
	// 20 pid_t

	ck_assert_ptr_nonnull(elist_head->fields );

	// specific to sched_switch

	ck_assert_str_eq( "next_prio", elist_head->fields->name );
	ck_assert_int_eq( 1, elist_head->fields->type );
	ck_assert_int_eq( 60, elist_head->fields->offset );
	ck_assert_int_eq( 4, elist_head->fields->size );
	ck_assert_int_eq( 1, elist_head->fields->sign );

	pop((void**)&elist_head->fields);

	ck_assert_str_eq( "next_pid", elist_head->fields->name );
	ck_assert_int_eq( 20, elist_head->fields->type );
	ck_assert_int_eq( 56, elist_head->fields->offset );
	ck_assert_int_eq( 4, elist_head->fields->size );
	ck_assert_int_eq( 1, elist_head->fields->sign );

	pop((void**)&elist_head->fields);

	ck_assert_str_eq( "next_comm[16]", elist_head->fields->name );
	ck_assert_int_eq( 10, elist_head->fields->type );
	ck_assert_int_eq( 40, elist_head->fields->offset );
	ck_assert_int_eq( 16, elist_head->fields->size );
	ck_assert_int_eq( 0, elist_head->fields->sign );

	pop((void**)&elist_head->fields);

	ck_assert_str_eq( "prev_state", elist_head->fields->name );
	ck_assert_int_eq( 2, elist_head->fields->type );
	ck_assert_int_eq( 32, elist_head->fields->offset );
	ck_assert_int_eq( 8, elist_head->fields->size );
	ck_assert_int_eq( 1, elist_head->fields->sign );

	pop((void**)&elist_head->fields);

	ck_assert_str_eq( "prev_prio", elist_head->fields->name );
	ck_assert_int_eq( 1, elist_head->fields->type );
	ck_assert_int_eq( 28, elist_head->fields->offset );
	ck_assert_int_eq( 4, elist_head->fields->size );
	ck_assert_int_eq( 1, elist_head->fields->sign );

	pop((void**)&elist_head->fields);

	ck_assert_str_eq( "prev_pid", elist_head->fields->name );
	ck_assert_int_eq( 20, elist_head->fields->type );
	ck_assert_int_eq( 24, elist_head->fields->offset );
	ck_assert_int_eq( 4, elist_head->fields->size );
	ck_assert_int_eq( 1, elist_head->fields->sign );

	pop((void**)&elist_head->fields);

	ck_assert_str_eq( "prev_comm[16]", elist_head->fields->name );
	ck_assert_int_eq( 10, elist_head->fields->type );
	ck_assert_int_eq( 8, elist_head->fields->offset );
	ck_assert_int_eq( 16, elist_head->fields->size );
	ck_assert_int_eq( 0, elist_head->fields->sign );

	pop((void**)&elist_head->fields);

	// Common to all events

	ck_assert_str_eq( "common_pid", elist_head->fields->name );
	ck_assert_int_eq( 1, elist_head->fields->type );
	ck_assert_int_eq( 4, elist_head->fields->offset );
	ck_assert_int_eq( 4, elist_head->fields->size );
	ck_assert_int_eq( 1, elist_head->fields->sign );

	pop((void**)&elist_head->fields);

	ck_assert_str_eq( "common_preempt_count", elist_head->fields->name );
	ck_assert_int_eq( 10, elist_head->fields->type );
	ck_assert_int_eq( 3, elist_head->fields->offset );
	ck_assert_int_eq( 1, elist_head->fields->size );
	ck_assert_int_eq( 0, elist_head->fields->sign );

	pop((void**)&elist_head->fields);

	ck_assert_str_eq( "common_flags", elist_head->fields->name );
	ck_assert_int_eq( 10, elist_head->fields->type );
	ck_assert_int_eq( 2, elist_head->fields->offset );
	ck_assert_int_eq( 1, elist_head->fields->size );
	ck_assert_int_eq( 0, elist_head->fields->sign );

	pop((void**)&elist_head->fields);

	ck_assert_str_eq( "common_type", elist_head->fields->name );
	ck_assert_int_eq( 0, elist_head->fields->type );
	ck_assert_int_eq( 0, elist_head->fields->offset );
	ck_assert_int_eq( 2, elist_head->fields->size );
	ck_assert_int_eq( 0, elist_head->fields->sign );

	clearEventConf();
}
END_TEST

void orchestrator_manage (Suite * s) {
	TCase *tc1 = tcase_create("manage_thread_stop");

	tcase_add_checked_fixture(tc1, orchestrator_manage_setup, orchestrator_manage_teardown);
	tcase_add_loop_exit_test(tc1, orchestrator_manage_stop, EXIT_SUCCESS, 0, 2);
	tcase_set_timeout(tc1, 10);
	suite_add_tcase(s, tc1);

	/* these depend on privileges and can not be run in the cloud */
#ifdef PRVTEST
	TCase *tc2 = tcase_create("manage_thread_read");
	tcase_add_checked_fixture(tc2, orchestrator_manage_setup, orchestrator_manage_teardown);
	tcase_add_test(tc2, orchestrator_manage_readdata);
	tcase_add_test(tc2, orchestrator_manage_readftrace);
	tcase_set_timeout(tc2, 10);
    suite_add_tcase(s, tc2);
#endif

	TCase *tc3 = tcase_create("manage_ftrace_cfg");
	tcase_add_test(tc3, orchestrator_manage_ftrc_cfgread);
	suite_add_tcase(s, tc3);

	return;
}
