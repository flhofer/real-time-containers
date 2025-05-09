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
	elist_head->eventid = 317;	// used for kernel 4 and 6, may differ
	elist_head->event = TR_EVENT_SWITCH;
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
	prgset->affinity = strdup(TESTCPU);
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
	while (elist_thead)
		pop((void**)&elist_thead);

	if (prgset)
		freePrgSet(prgset);
	if (contparm)
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
	(void)sprintf(fthread.dbgfile, "test/resources/manage_ftread.dat"); // dump of a kernel thread scan

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

char * eventFiles[] = {
		"test/resources/manage_sched_switch_fmt6.5.txt",
		"test/resources/manage_sched_switch_fmt6.1.txt",
		"test/resources/manage_sched_switch_fmt6.5w.txt"
};

struct {
	char* name;
	int type;
	int offset;
	int size;
	int sign;
} eventFields [][13] = {
		{
			{ "common_type",trv_short,0,2,0 }, { "common_flags",trv_char,2,1,0}, { "common_preempt_count",trv_char,3,1,0}, { "common_pid",trv_int,4,4,1},
			{ "prev_comm",trv_char,8,16,0},	{ "prev_pid",trv_pid_t,24,4,1}, { "prev_prio",trv_int,28,4,1}, { "prev_state",trv_long,32,8,1}, { "next_comm",trv_char,40,16,0},
			{ "next_pid",trv_pid_t,56,4,1},	{ "next_prio",trv_int,60,4,1}, {NULL}
		},
		{
			{ "common_type",trv_short,0,2,0}, { "common_flags",trv_char,2,1,0},	{ "common_preempt_count",trv_char,3,1,0},{ "common_pid",trv_int,4,4,1},	{ "common_preempt_lazy_count",trv_char,8,1,0},
			{ "prev_comm",trv_char,12,16,1}, { "prev_pid",trv_pid_t,28,4,1}, { "prev_prio",trv_int,32,4,1},	{ "prev_state",trv_long,40,8,1},
			{ "next_comm",trv_char,48,16,1}, { "next_pid",trv_pid_t,64,4,1}, { "next_prio",trv_int,68,4,1}, {NULL}
		},
		{
			{ "common_type",trv_short,0,2,0 }, { "common_flags",trv_char,2,1,0}, { "common_preempt_count",trv_char,3,1,0}, { "common_pid",trv_int,4,4,1},
			{ "prev_comm",trv_char,8,16,0},	{ "prev_pid",trv_pid_t,24,4,1}, { "prev_prio",trv_int,28,4,1}, { "prev_state",trv_longlong,32,16,1}, { "next_comm",trv_char,48,8,0},
			{ "next_pid",trv_pid_t,56,4,1}, {NULL}
		}
		};

/// TEST CASE -> read kernel debugcommon_type tracing info and prepare structures
/// EXPECTED -> parsing of field specifications, push to ecfg fields
START_TEST(orchestrator_manage_ftrc_cfgread)
{
	buildEventConf();
	char * buf = malloc(PIPE_BUFFER);
	FILE *f;
	int ret;
	int no = 0;

	if ((f = fopen (eventFiles[_i],"r"))) {
		ret = fread(buf, sizeof(char), PIPE_BUFFER-1, f);
		ck_assert_int_ne(ret, 0);
		buf[ret] = '\0';
		fclose(f);
	}
	else
		ck_abort_msg("Could not open file: %s", strerror(errno));

	parseEventFields (&elist_head->fields,buf);
	free (buf);

	while ((eventFields[_i][no].name)){
		ck_assert_ptr_nonnull(elist_head->fields );
		ck_assert_str_eq( eventFields[_i][no].name, elist_head->fields->name );
		ck_assert_int_eq( eventFields[_i][no].type, elist_head->fields->type );
		ck_assert_int_eq( eventFields[_i][no].offset, elist_head->fields->offset );
		ck_assert_int_eq( eventFields[_i][no].size, elist_head->fields->size );
		ck_assert_int_eq( eventFields[_i][no].sign, elist_head->fields->sign );
		free(elist_head->fields->name);
		pop((void**)&elist_head->fields);
		no++;
	}

	clearEventConf();
}
END_TEST

/// TEST CASE -> use ecfg structure
/// EXPECTED -> tr_ structure variables initialized with offsets
START_TEST(orchestrator_manage_ftrc_offsetparse)
{
	buildEventConf();
	char * buf = malloc(PIPE_BUFFER);
	FILE *f;
	int ret;

	if ((f = fopen ("test/resources/manage_sched_switch_fmt6.5.txt","r"))) {
		ret = fread(buf, sizeof(char), PIPE_BUFFER-1, f);
		ck_assert_int_ne(ret, 0);
		buf[ret] = '\0';
		fclose(f);
	}
	else
		ck_abort_msg("Could not open file: %s", strerror(errno));

	parseEventFields (&elist_head->fields, buf);

	ck_assert_ptr_nonnull(elist_head->fields );

	parseEventOffsets();

	// Values for Kernel 6.5 debug tracer format, sched_switch

	// Offset for event common
	ck_assert_ptr_eq((uint16_t*)0x00, tr_common.common_type);
	ck_assert_ptr_eq((uint8_t*) 0x02, tr_common.common_flags);
	ck_assert_ptr_eq((uint8_t*) 0x03, tr_common.common_preempt_count);
	ck_assert_ptr_eq((int32_t*) 0x04, tr_common.common_pid);

	// Offsets for sched_switch
	ck_assert_ptr_eq((char*)	0x08, tr_switch.prev_comm);
	ck_assert_ptr_eq((pid_t*)	0x18, tr_switch.prev_pid);
	ck_assert_ptr_eq((int32_t*)	0x1C, tr_switch.prev_prio);
	ck_assert_ptr_eq((int64_t*)	0x20, tr_switch.prev_state);
	ck_assert_ptr_eq((char*)	0x28, tr_switch.next_comm);
	ck_assert_ptr_eq((pid_t*)	0x38, tr_switch.next_pid);
	ck_assert_ptr_eq((int32_t*)	0x3C, tr_switch.next_prio);

	clearEventConf();

	// format example 6.1
	buildEventConf();
	if ((f = fopen ("test/resources/manage_sched_switch_fmt6.1.txt","r"))) {
		ret = fread(buf, sizeof(char), PIPE_BUFFER-1, f);
		ck_assert_int_ne(ret, 0);
		buf[ret] = '\0';
		fclose(f);
	}
	else
		ck_abort_msg("Could not open file: %s", strerror(errno));

	parseEventFields (&elist_head->fields, buf);
	free (buf);

	ck_assert_ptr_nonnull(elist_head->fields );

	parseEventOffsets();

	// Values for Kernel 6.1 debug tracer format, sched_switch

	// Offset for event common
	ck_assert_ptr_eq((uint16_t*)0x00, tr_common.common_type);
	ck_assert_ptr_eq((uint8_t*) 0x02, tr_common.common_flags);
	ck_assert_ptr_eq((uint8_t*) 0x03, tr_common.common_preempt_count);
	ck_assert_ptr_eq((int32_t*) 0x04, tr_common.common_pid);

	// Offsets for sched_switch
	ck_assert_ptr_eq((char*)	0x0C, tr_switch.prev_comm);
	ck_assert_ptr_eq((pid_t*)	0x1C, tr_switch.prev_pid);
	ck_assert_ptr_eq((int32_t*)	0x20, tr_switch.prev_prio);
	ck_assert_ptr_eq((int64_t*)	0x28, tr_switch.prev_state);
	ck_assert_ptr_eq((char*)	0x30, tr_switch.next_comm);
	ck_assert_ptr_eq((pid_t*)	0x40, tr_switch.next_pid);
	ck_assert_ptr_eq((int32_t*)	0x44, tr_switch.next_prio);

	clearEventConf();
}
END_TEST

/// TEST CASE -> pass a kernel tracer frame to pickPidCommon and evaluates it
/// EXPECTED -> corresponding nodes and data should change
START_TEST(orchestrator_manage_ftrc_ppcmn)
{
	// Test frame  - kernel 6.5
	unsigned char frame [] = {0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00};

	// Generate Nodes
	const int pid[] = { 1, 2, 3	};

	for (int i=0; i<sizeof(pid)/sizeof(int); ++i) {
		node_push(&nhead);
		nhead->pid = pid[i];
		char * name = malloc(16);
		(void)sprintf(name, "PID %d", i);
		nhead->psig = name;
	}

	// Default common  - kernel 6.5
	const struct tr_common tc_common_default = { (void *)0, (void *)2, (void *)3, (void *)4 };
	tr_common = tc_common_default;
	int ret = pickPidCommon(&frame, NULL, 0);

	ck_assert_int_eq(0, ret);
	ck_assert(!(nhead->status & MSK_STATNRSCH));
	ck_assert(nhead->next->status & MSK_STATNRSCH);
	ck_assert(!(nhead->next->next->status & MSK_STATNRSCH));
}
END_TEST

/// TEST CASE -> pass a kernel tracer frame to pickPidSwitch and evaluates it
/// EXPECTED -> corresponding nodes and data should change
START_TEST(orchestrator_manage_ftrc_ppswitch)
{
	// Default struct common/switch  - kernel 6.5
	const struct tr_common tc_common_default = { (void *)0, (void *)2, (void *)3, (void *)4 };
	tr_common = tc_common_default;
	const struct tr_switch tc_switch_default = { (void *)0x8, (void *)0x18, (void *)0x1C, (void *)0x20, (void*)0x28, (void*)0x38, (void*)0x3C };
	tr_switch = tc_switch_default;

	// Generate Nodes
	const int pid[] = { 1, 2, 3	};

	for (int i=0; i<sizeof(pid)/sizeof(int); ++i) {
		node_push(&nhead);
		nhead->pid = pid[i];
		char * name = malloc(16);
		(void)sprintf(name, "PID %d", (i+1));
		nhead->psig = name;
	}

	// Generate ftrace thread info
	push((void**)&elist_thead, sizeof(struct ftrace_thread));
	elist_thead->cpuno = 2;

	// Test frame  - kernel 6.5 (size = last pointer + size
	unsigned char frame [64] = {0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00 };	// base frame from test above
	memcpy(&frame[0x8], nhead->next->psig, 16);				// prev_comm sig
	frame[0x18]=2;
	memcpy(&frame[0x28], nhead->psig, 16);					// next_comm sig
	frame[0x38]=3;

	// TODOL modify to test sections of pickPidInfoS
	int ret = pickPidInfoS(&frame, elist_thead, 0);

	ck_assert_int_eq(0, ret);
	ck_assert(!(nhead->status & MSK_STATNRSCH));
	ck_assert(nhead->next->status & MSK_STATNRSCH);
	ck_assert(!(nhead->next->next->status & MSK_STATNRSCH));
}
END_TEST

/// TEST CASE -> pass a node information and check rt data update
/// EXPECTED -> data reflects runtime values, even if we miss a scan
START_TEST(orchestrator_manage_ppconsrt)
{
	// Generate Node
	node_push(&nhead);
	nhead->pid = getpid();
	nhead->psig = strdup("PidTest");

	nhead->mon.rt = 2000;
	nhead->attr.sched_policy = SCHED_DEADLINE;
	nhead->attr.sched_runtime = 4500;
	nhead->attr.sched_deadline = 20000;
	nhead->attr.sched_period = 22000;
	nhead->attr.sched_period = 22000;
	nhead->mon.deadline = 20000;
	pickPidConsolidatePeriod(nhead, 3000);
	ck_assert_int_eq(SCHED_DEADLINE, nhead->attr.sched_policy);
	ck_assert_int_eq(2000, nhead->mon.rt);		// untouched
	ck_assert_int_eq(0, nhead->mon.dl_scanfail);

	nhead->mon.last_ts = 2000;
	pickPidConsolidatePeriod(nhead, 3000);
	ck_assert_int_eq(SCHED_DEADLINE, nhead->attr.sched_policy);
	ck_assert_int_eq(2000, nhead->mon.rt_min);
	ck_assert_int_eq(2000, nhead->mon.rt_max);
	ck_assert_int_eq(0, nhead->mon.dl_scanfail);

	nhead->mon.last_ts = 2000;
	nhead->mon.deadline = 20000;
	nhead->mon.rt = 22050;
	pickPidConsolidatePeriod(nhead, 21500);
	ck_assert_int_eq(1, nhead->mon.dl_overrun);
	ck_assert_int_eq(0, nhead->mon.dl_scanfail);

	nhead->mon.last_ts = 2100;
	nhead->mon.deadline = 20000;
	nhead->mon.rt = 22050;
	pickPidConsolidatePeriod(nhead, 80000);
	ck_assert_int_eq(2, nhead->mon.dl_overrun);
	ck_assert_int_eq(2, nhead->mon.dl_scanfail);

}
END_TEST

/// TEST CASE -> test if resource is full -> only for deadline
/// EXPECTED -> should return 1 if extra time does not fit
START_TEST(orchestrator_manage_ppckbuf)
{
	// Generate Nodes
	const int pid[] = { 1, 2, 3, 4, 5, 6};
	int ret;

	for (int i=0; i<sizeof(pid)/sizeof(int); ++i) {
		node_push(&nhead);
		nhead->pid = pid[i];
		char * name = malloc(16);
		(void)sprintf(name, "PID %d", (i+1));
		nhead->psig = name;
		nhead->mon.assigned = i % 2;
		nhead->attr.sched_policy = (0 == i)? SCHED_OTHER : SCHED_DEADLINE;
		nhead->mon.deadline = 50000 + i * 10000;
		nhead->attr.sched_period = 5000 + 5000 * (i % 3);
		nhead->attr.sched_runtime = nhead->attr.sched_period / 5;
	}

	ret = pickPidCheckBuffer(nhead->next, 60000, 1000);
	ck_assert_int_eq(0, ret);
	ret = pickPidCheckBuffer(nhead->next->next, 75000, 1000); // should ignore pid 6
	ck_assert_int_eq(0, ret);
	ret = pickPidCheckBuffer(nhead->next->next, 76000, 1000); // s
	ck_assert_int_eq(1, ret);

}
END_TEST

void orchestrator_manage (Suite * s) {
	TCase *tc1 = tcase_create("manage_thread_stop");

	tcase_add_checked_fixture(tc1, orchestrator_manage_setup, orchestrator_manage_teardown);
	tcase_add_loop_exit_test(tc1, orchestrator_manage_stop, EXIT_SUCCESS, 0, 2);
	tcase_set_timeout(tc1, 10);
	suite_add_tcase(s, tc1);

	/* these depend on privileges and can not be run in the cloud */
	// TODO; update test, will not work with new config read
#ifdef PRVTEST
	TCase *tc2 = tcase_create("manage_thread_read");
	tcase_add_checked_fixture(tc2, orchestrator_manage_setup, orchestrator_manage_teardown);
	tcase_add_test(tc2, orchestrator_manage_readdata);
	tcase_add_test(tc2, orchestrator_manage_readftrace);
	tcase_set_timeout(tc2, 10);
    suite_add_tcase(s, tc2);
#endif

	TCase *tc3 = tcase_create("manage_ftrace_cfg");
	tcase_add_loop_test(tc3, orchestrator_manage_ftrc_cfgread, 0, 3);
	tcase_add_test(tc3, orchestrator_manage_ftrc_offsetparse);
	suite_add_tcase(s, tc3);

	TCase *tc4 = tcase_create("manage_ftrace_pickpid");
	tcase_add_checked_fixture(tc4, orchestrator_manage_setup, orchestrator_manage_teardown);
	tcase_add_test(tc4, orchestrator_manage_ftrc_ppcmn);
	tcase_add_test(tc4, orchestrator_manage_ftrc_ppswitch);
	suite_add_tcase(s, tc4);

	TCase *tc5 = tcase_create("manage_ftrace_pickpid_acc");
	tcase_add_checked_fixture(tc5, orchestrator_manage_setup, orchestrator_manage_teardown);
	tcase_add_test(tc5, orchestrator_manage_ppconsrt);
	tcase_add_test(tc5, orchestrator_manage_ppckbuf);
	suite_add_tcase(s, tc5);

	return;
}
