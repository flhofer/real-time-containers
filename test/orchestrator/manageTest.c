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
#include <sys/stat.h>
#include <linux/sched.h>	// Linux specific scheduling

#define MAX_PATH 256
#define TESTCPU "0"

static void
manageTestWriteFile(const char * path, const char * value){
	FILE * file = fopen(path, "w");
	ck_assert_msg(file, "Could not create test file %s", path);
	ck_assert_int_eq((int)strlen(value), (int)fwrite(value, 1, strlen(value), file));
	ck_assert_int_eq(0, fclose(file));
}

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
	scount = 0;
	cpuStatTimestamp = 0;
	ftrace_stop = 0;
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
	clearEventConf();
	freeTracer(&rHead);

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

/// TEST CASE -> append an ftrace event from a synthetic tracefs directory
/// EXPECTED -> event ID, callback and field description are loaded and enabled
START_TEST(orchestrator_manage_ftrc_append)
{
	ck_assert_int_eq(-1, parseEventOffsets());

	char directory[] = "/tmp/manage-event-XXXXXX";
	ck_assert_ptr_nonnull(mkdtemp(directory));
	char events[PATH_MAX];
	char sched[PATH_MAX];
	char eventdir[PATH_MAX];
	char prefix[PATH_MAX];
	char enable[PATH_MAX];
	char id[PATH_MAX];
	char format[PATH_MAX];
	ck_assert_int_lt(snprintf(events, sizeof(events), "%s/events", directory), (int)sizeof(events));
	ck_assert_int_lt(snprintf(sched, sizeof(sched), "%s/sched", events), (int)sizeof(sched));
	ck_assert_int_lt(snprintf(eventdir, sizeof(eventdir), "%s/sched_switch", sched), (int)sizeof(eventdir));
	ck_assert_int_lt(snprintf(prefix, sizeof(prefix), "%s/", directory), (int)sizeof(prefix));
	ck_assert_int_lt(snprintf(enable, sizeof(enable), "%s/enable", eventdir), (int)sizeof(enable));
	ck_assert_int_lt(snprintf(id, sizeof(id), "%s/id", eventdir), (int)sizeof(id));
	ck_assert_int_lt(snprintf(format, sizeof(format), "%s/format", eventdir), (int)sizeof(format));
	ck_assert_int_eq(0, mkdir(events, S_IRWXU));
	ck_assert_int_eq(0, mkdir(sched, S_IRWXU));
	ck_assert_int_eq(0, mkdir(eventdir, S_IRWXU));
	manageTestWriteFile(enable, "0\n");
	manageTestWriteFile(id, "317\n");
	manageTestWriteFile(format,
			"name: sched_switch\n"
			"ID: 317\n"
			"format:\n"
			"\tfield:unsigned short common_type; offset:0; size:2; signed:0;\n");

	ck_assert_int_eq(0, appendEvent(prefix, TR_EVENT_SWITCH, pickPidInfoS));
	ck_assert_ptr_nonnull(elist_head);
	ck_assert_int_eq(317, elist_head->eventid);
	ck_assert_str_eq(TR_EVENT_SWITCH, elist_head->event);
	ck_assert_ptr_eq(pickPidInfoS, elist_head->eventcall);
	ck_assert_ptr_nonnull(elist_head->fields);
	ck_assert_str_eq("common_type", elist_head->fields->name);

	char value[3] = { 0 };
	FILE * file = fopen(enable, "r");
	ck_assert_ptr_nonnull(file);
	ck_assert_int_eq(2, fread(value, 1, 2, file));
	ck_assert_int_eq(0, fclose(file));
	ck_assert_str_eq("1\n", value);

	free(elist_head->event);
	elist_head->event = NULL;
	clearEventConf();
	ck_assert_int_eq(0, unlink(enable));
	ck_assert_int_eq(0, unlink(id));
	ck_assert_int_eq(0, unlink(format));
	ck_assert_int_eq(0, rmdir(eventdir));
	ck_assert_int_eq(0, rmdir(sched));
	ck_assert_int_eq(0, rmdir(events));
	ck_assert_int_eq(0, rmdir(directory));
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
	elist_thead->tracer = malloc(sizeof(struct resTracer));

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

	free(elist_thead->tracer);
}
END_TEST

/// TEST CASE -> process a migrated task's per-CPU events out of order
/// EXPECTED -> stale switch-out is ignored and the newer interval is preserved
START_TEST(orchestrator_manage_ftrc_ppswitch_migration)
{
	const struct tr_common tc_common_default = { (void *)0, (void *)2, (void *)3, (void *)4 };
	tr_common = tc_common_default;
	const struct tr_switch tc_switch_default = { (void *)0x8, (void *)0x18, (void *)0x1C, (void *)0x20, (void*)0x28, (void*)0x38, (void*)0x3C };
	tr_switch = tc_switch_default;

	node_push(&nhead);
	nhead->pid = 42;
	nhead->psig = strdup("PID 42");
	nhead->mon.assigned = 2;

	struct ftrace_thread oldCPU = { .cpuno = 1 };
	struct ftrace_thread newCPU = { .cpuno = 2 };
	oldCPU.tracer = malloc(sizeof(struct resTracer));
	newCPU.tracer = malloc(sizeof(struct resTracer));

	unsigned char switchIn[64] = { 0 };
	unsigned char switchOut[64] = { 0 };
	pid_t pid = nhead->pid;
	int64_t preempted = 0x0100;

	memcpy(&switchIn[0x28], nhead->psig, strlen(nhead->psig));
	memcpy(&switchIn[0x38], &pid, sizeof(pid));
	ck_assert_int_eq(0, pickPidInfoS(switchIn, &newCPU, 200));
	ck_assert_int_eq(2, nhead->mon.last_cpu);
	ck_assert_uint_eq(200, nhead->mon.last_ts);
	ck_assert_int_eq(2, nhead->mon.assigned);

	memcpy(&switchOut[0x8], nhead->psig, strlen(nhead->psig));
	memcpy(&switchOut[0x18], &pid, sizeof(pid));
	memcpy(&switchOut[0x20], &preempted, sizeof(preempted));
	ck_assert_int_eq(0, pickPidInfoS(switchOut, &oldCPU, 100));
	ck_assert_uint_eq(0, nhead->mon.rt);
	ck_assert_int_eq(2, nhead->mon.last_cpu);
	ck_assert_uint_eq(200, nhead->mon.last_ts);
	ck_assert_int_eq(2, nhead->mon.assigned);

	ck_assert_int_eq(0, pickPidInfoS(switchOut, &newCPU, 250));
	ck_assert_uint_eq(50, nhead->mon.rt);
	ck_assert_int_eq(-1, nhead->mon.last_cpu);
	ck_assert_int_eq(2, nhead->mon.assigned);

	free(oldCPU.tracer);
	free(newCPU.tracer);
}
END_TEST

/// TEST CASE -> process wakeups used to infer a non-Deadline task period
/// EXPECTED -> stale and early wakeups are ignored while full cycles are sampled
START_TEST(orchestrator_manage_ftrc_ppwakeup)
{
	const struct tr_common common = { (void *)0, (void *)2, (void *)3, (void *)4 };
	const struct tr_wakeup wakeup = { (void *)8, (void *)24, (void *)28, (void *)32 };
	tr_common = common;
	tr_wakeup = wakeup;

	node_push(&nhead);
	nhead->pid = 42;
	nhead->psig = strdup("helper");
	nhead->attr.sched_policy = SCHED_OTHER;
	nhead->mon.cdf_period = 100000000;

	unsigned char frame[40] = { 0 };
	pid_t pid = nhead->pid;
	int32_t priority = 120;
	int32_t CPU = 0;
	memcpy(&frame[4], &pid, sizeof(pid));
	memcpy(&frame[8], nhead->psig, strlen(nhead->psig));
	memcpy(&frame[24], &pid, sizeof(pid));
	memcpy(&frame[28], &priority, sizeof(priority));
	memcpy(&frame[32], &CPU, sizeof(CPU));

	ck_assert_int_eq(0, pickPidInfoW(frame, NULL, 1000000000));
	ck_assert_uint_eq(1000000000, nhead->mon.last_tsP);
	ck_assert_uint_eq(1, nhead->mon.dl_count);
	ck_assert_ptr_null(nhead->mon.pdf_phist);

	ck_assert_int_eq(0, pickPidInfoW(frame, NULL, 1020000000));
	ck_assert_uint_eq(1000000000, nhead->mon.last_tsP);
	ck_assert_uint_eq(1, nhead->mon.dl_count);
	ck_assert_int_eq(0, pickPidInfoW(frame, NULL, 900000000));
	ck_assert_uint_eq(1000000000, nhead->mon.last_tsP);

	ck_assert_int_eq(0, pickPidInfoW(frame, NULL, 1100000000));
	ck_assert_uint_eq(1100000000, nhead->mon.last_tsP);
	ck_assert_uint_eq(2, nhead->mon.dl_count);
	ck_assert_ptr_nonnull(nhead->mon.pdf_phist);
	ck_assert_uint_eq(1, nhead->mon.pdf_pscope.samples);
	ck_assert_uint_eq(1200000000, nhead->mon.deadline);

	nhead->attr.sched_policy = SCHED_DEADLINE;
	ck_assert_int_eq(0, pickPidInfoW(frame, NULL, 1200000000));
	ck_assert_uint_eq(1100000000, nhead->mon.last_tsP);
	ck_assert_uint_eq(2, nhead->mon.dl_count);

	nhead->attr.sched_policy = SCHED_OTHER;
	frame[8] = 0x80;
	ck_assert_int_eq(-1, pickPidInfoW(frame, NULL, 1200000000));
	frame[8] = 'h';
	uint16_t malformed = 0xF000;
	memcpy(frame, &malformed, sizeof(malformed));
	ck_assert_int_eq(-1, pickPidInfoW(frame, NULL, 1200000000));
}
END_TEST

/// TEST CASE -> invalidate runtime state after a CPU reports lost trace events
/// EXPECTED -> affected samples are dropped without touching another CPU's interval
START_TEST(orchestrator_manage_ftrc_loss)
{
	node_push(&nhead);
	node_t * active = nhead;
	active->pid = 1;
	active->mon.assigned = 1;
	active->mon.last_cpu = 1;
	active->mon.last_ts = 100;
	active->mon.rt = 30;

	node_push(&nhead);
	node_t * sleeping = nhead;
	sleeping->pid = 2;
	sleeping->mon.assigned = 1;
	sleeping->mon.rt = 20;

	node_push(&nhead);
	node_t * otherCPU = nhead;
	otherCPU->pid = 3;
	otherCPU->mon.assigned = 2;
	otherCPU->mon.last_cpu = 2;
	otherCPU->mon.last_ts = 120;
	otherCPU->mon.rt = 10;

	ck_assert_int_eq(2, invalidateCPURuntime(1));
	ck_assert_uint_eq(0, active->mon.rt);
	ck_assert_uint_eq(0, active->mon.last_ts);
	ck_assert_int_eq(-1, active->mon.last_cpu);
	ck_assert(active->status & MSK_STATRTINV);
	ck_assert_uint_eq(0, sleeping->mon.rt);
	ck_assert(sleeping->status & MSK_STATRTINV);
	ck_assert_uint_eq(10, otherCPU->mon.rt);
	ck_assert_uint_eq(120, otherCPU->mon.last_ts);
	ck_assert_int_eq(2, otherCPU->mon.last_cpu);
	ck_assert(!(otherCPU->status & MSK_STATRTINV));

	sleeping->attr.sched_policy = SCHED_OTHER;
	sleeping->mon.last_ts = 200;
	sleeping->mon.rt = 10;
	pickPidConsolidatePeriod(sleeping, 250);
	ck_assert_uint_eq(0, sleeping->mon.rt);
	ck_assert_ptr_null(sleeping->mon.pdf_hist);
	ck_assert(!(sleeping->status & MSK_STATRTINV));
}
END_TEST

/// TEST CASE -> start an ftrace reader with an unavailable trace pipe
/// EXPECTED -> reader exits cleanly and reports the file access error
START_TEST(orchestrator_manage_ftrc_missingpipe)
{
	struct ftrace_thread thread = { 0 };
	thread.cpuno = 0;
	thread.dbgfile = strdup("/tmp/manage-trace-pipe-does-not-exist");
	pthread_t reader;
	ck_assert_int_eq(0, pthread_create(&reader, NULL, thread_ftrace, &thread));
	void * result = NULL;
	ck_assert_int_eq(0, pthread_join(reader, &result));
	ck_assert_ptr_nonnull(result);
	ck_assert_int_eq(ENOENT, *(int *)result);
	free(result);
}
END_TEST

/// TEST CASE -> collect managed runtime and close an observation window
/// EXPECTED -> utilization uses elapsed time and trace loss discards the window
START_TEST(orchestrator_manage_resource_usage)
{
	push((void**)&rHead, sizeof(resTracer_t));
	rHead->affinity = numa_allocate_cpumask();
	numa_bitmask_setbit(rHead->affinity, 2);
	rHead->UobsMin = 1.0;

	rHead->observedTimestamp = 100;
	rHead->observedEnd = 200;
	rHead->observedRuntime = 25;

	prgset->ftrace = 1;
	rHead->statisticsTimestamp = 100;
	updateResourceUtilization(200);
	ck_assert(rHead->status & MSK_STATROBSRDY);
	ck_assert_double_eq_tol(0.25, rHead->Uobserved, 0.000001);
	ck_assert_double_eq_tol(0.25, rHead->UobsAvg, 0.000001);

	rHead->observedRuntime = 50;
	rHead->status |= MSK_STATROBSINV;
	rHead->observedEnd = 300;
	updateResourceUtilization(300);
	ck_assert(!(rHead->status & MSK_STATROBSRDY));
	ck_assert_uint_eq(0, rHead->observedRuntime);
	ck_assert_double_eq_tol(0.0, rHead->Uobserved, 0.000001);

	freeTracer(&rHead);
}
END_TEST

/// TEST CASE -> parse a per-CPU /proc/stat record
/// EXPECTED -> total excludes guest fields and idle includes I/O wait
START_TEST(orchestrator_manage_cpustat)
{
	int CPUno = -1;
	uint64_t total = 0;
	uint64_t idle = 0;
	const char * line = "cpu7 100 20 30 400 50 6 7 8 9 10";

	ck_assert_int_eq(0, parseCPUStat(line, &CPUno, &total, &idle));
	ck_assert_int_eq(7, CPUno);
	ck_assert_uint_eq(621, total);
	ck_assert_uint_eq(450, idle);
	ck_assert_int_eq(-1, parseCPUStat("cpu 100 20 30 400", &CPUno, &total, &idle));
}
END_TEST

/// TEST CASE -> keep a percentile-allowed overflow outside the precision bins
/// EXPECTED -> one 2% overflow is retained, a second one resamples the range
START_TEST(orchestrator_manage_hist_scope)
{
	stat_hist * hist = NULL;
	stat_scope scope = { 0, 0, 0, 0.0, 0.0, 0.0, 0.0, DBL_MAX , -DBL_MAX};
	ck_assert_int_eq(0, runstats_histInit(&hist, 1.0));

	for (int i = 0; i < 49; i++)
		ck_assert_int_eq(0, runstats_histAdd(hist, &scope, 1.0));
	ck_assert_int_eq(0, runstats_histAdd(hist, &scope, 2.0));

	ck_assert_int_eq(0, runstats_histCheck(hist, &scope));
	ck_assert_uint_eq(50, scope.samples);
	ck_assert_uint_eq(1, scope.overflows);
	ck_assert_double_eq_tol(49.0, gsl_histogram_sum(hist), 0.000001);
	ck_assert_double_eq_tol(1.02, runstats_histMean(hist, &scope), 0.000001);
	ck_assert_double_eq_tol(1.86, runstats_histSixSigma(hist, &scope), 0.000001);
	ck_assert_int_lt(0, runstats_histResample(&hist, &scope, 0.98));	// returns -GSL_CONTINUE should not resample, only 1 overflow, within 2% tolerance
	stat_cdf * cdf = NULL;
	ck_assert_int_eq(0, runstats_cdfCreate(&hist, &cdf));
	double percentile = runstats_cdfSample(cdf, &scope, 0.98);
	ck_assert(percentile >= 0.98 && percentile < 1.021);
	runstats_cdfFree(&cdf);

	ck_assert_int_eq(0, runstats_histAdd(hist, &scope, 2.0));
	ck_assert_int_eq(0, runstats_histResample(&hist, &scope, 0.98));	// resample, 2nd overflow, outside 2% tolerance
	ck_assert_uint_eq(0, scope.samples);
	ck_assert_double_ge(gsl_histogram_max(hist), 2.1);

	runstats_histFree(hist);
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

/// TEST CASE -> configured DL period must not be added to a stale CDF period
/// EXPECTED -> buffer accounting uses the DL scheduler period exactly once
START_TEST(orchestrator_manage_ppckbuf_dlperiod)
{
	// Task whose remaining buffer is being checked.
	node_push(&nhead);
	node_t * item = nhead;
	item->pid = 1;
	item->mon.assigned = 0;
	item->attr.sched_policy = SCHED_DEADLINE;
	item->mon.deadline = 50000;
	item->attr.sched_period = 50000;
	item->attr.sched_runtime = 100;

	// Competing DL task: cdf_period must be ignored for this policy.
	node_push(&nhead);
	nhead->pid = 2;
	nhead->mon.assigned = 0;
	nhead->attr.sched_policy = SCHED_DEADLINE;
	nhead->mon.deadline = 10000;
	nhead->attr.sched_period = 10000;
	nhead->attr.sched_runtime = 1000;
	nhead->mon.cdf_period = 10000;

	ck_assert_int_eq(1, pickPidCheckBuffer(item, 45500, 1000));
}
END_TEST

/// TEST CASE -> candidate admission must include all container siblings
/// EXPECTED -> main alone fits, aggregate container load does not
START_TEST(orchestrator_manage_siblingsfit)
{
	cont_t * cont = calloc(1, sizeof(cont_t));
	pidc_t * mainParam = calloc(1, sizeof(pidc_t));
	pidc_t * rtParam = calloc(1, sizeof(pidc_t));
	pidc_t * helperParam = calloc(1, sizeof(pidc_t));
	mainParam->cont = cont;
	rtParam->cont = cont;
	helperParam->cont = cont;

	// Main RT task: U=0.4, which fits with the candidate load.
	node_push(&nhead);
	node_t * main = nhead;
	main->pid = 1;
	main->param = mainParam;
	main->mon.assigned = 0;
	main->attr.sched_policy = SCHED_DEADLINE;
	main->attr.sched_runtime = 40;
	main->attr.sched_period = 100;

	// A second RT task is always included: U=0.2.
	node_push(&nhead);
	node_t * rtSibling = nhead;
	rtSibling->pid = 3;
	rtSibling->param = rtParam;
	rtSibling->mon.assigned = 0;
	rtSibling->attr.sched_policy = SCHED_FIFO;
	rtSibling->mon.cdf_runtime = 20;
	rtSibling->mon.cdf_period = 100;

	// Connected non-RT helper: measured U=0.4 makes aggregate load U=1.1.
	node_push(&nhead);
	node_t * helper = nhead;
	helper->pid = 2;
	helper->param = helperParam;
	helper->mon.assigned = 0;
	helper->attr.sched_policy = SCHED_OTHER;
	helper->mon.cdf_runtime = 40;
	helper->mon.cdf_period = 100;

	resTracer_t candidate = { 0 };
	candidate.affinity = numa_allocate_cpumask();
	numa_bitmask_setbit(candidate.affinity, 1);
	candidate.status = MSK_STATHRMC;
	candidate.usedPeriod = 30;
	candidate.basePeriod = 100;
	candidate.U = 0.3;

	ck_assert_int_eq(-1, pidSiblingsFit(&candidate, main)); // fails due to overload, U=1.1
	helperParam->rscs = calloc(1, sizeof(rscs_t));

	ck_assert_int_eq(-1, pidSiblingsFit(&candidate, main)); // fails because hard affinity does not allow rescheduling to CPU 1

	// Reducing the helper to U=0.1 makes the enabled group fit at U=1.0.
	helperParam->rscs->affinity = -1;
	helper->mon.cdf_runtime = 10;
	ck_assert_int_eq(0, pidSiblingsFit(&candidate, main));

	numa_free_cpumask(candidate.affinity);
	main->param = NULL;
	rtSibling->param = NULL;
	helper->param = NULL;
	free(mainParam);
	free(rtParam);
	free(helperParam);
	free(cont);
}
END_TEST

/// TEST CASE -> exercise no-op and rejected PID reallocation paths
/// EXPECTED -> unchanged resources return one and invalid candidates are rejected
START_TEST(orchestrator_manage_realloc_reject)
{
	resTracer_t tracer = { 0 };
	tracer.affinity = numa_allocate_cpumask();
	numa_bitmask_setbit(tracer.affinity, 0);
	node_t item = { 0 };
	item.pid = 1;
	item.attr.sched_policy = SCHED_OTHER;

	ck_assert_int_eq(1, pidReallocAndTest(NULL, &tracer, &item));
	ck_assert_int_eq(1, pidReallocAndTest(&tracer, &tracer, &item));
	resTracer_t candidate = { 0 };
	candidate.affinity = numa_allocate_cpumask();
	numa_bitmask_setbit(candidate.affinity, 1);
	ck_assert_int_eq(-1, pidReallocAndTest(&candidate, &tracer, &item));
	ck_assert_int_eq(-1, pickPidReallocCPU(999, 0));
	ck_assert_int_eq(1, updateSiblings(&item));

	numa_free_cpumask(candidate.affinity);
	numa_free_cpumask(tracer.affinity);
}
END_TEST

/// TEST CASE -> percentile runtime follows the task's role in an RT allocation
/// EXPECTED -> RT tasks and their non-RT container helpers use the percentile
START_TEST(orchestrator_manage_runtime_percentile)
{
	cont_t cont = { 0 };
	cont_t unrelatedCont = { 0 };
	pidc_t anchorParam = { 0 };
	pidc_t helperParam = { 0 };
	anchorParam.cont = &cont;
	helperParam.cont = &cont;

	node_push(&nhead);
	node_t * anchor = nhead;
	anchor->pid = 1;
	anchor->param = &anchorParam;
	anchor->attr.sched_policy = SCHED_DEADLINE;

	node_t helper = { 0 };
	helper.pid = 2;
	helper.param = &helperParam;
	helper.attr.sched_policy = SCHED_OTHER;

	prgset->sched_mode = SM_DYNSIMPLE;
	ck_assert_int_eq(1, pickPidUseRuntimePercentile(anchor));
	ck_assert_int_eq(1, pickPidUseRuntimePercentile(&helper));

	helperParam.cont = &unrelatedCont;
	ck_assert_int_eq(0, pickPidUseRuntimePercentile(&helper));

	prgset->sched_mode = SM_PADAPTIVE;
	ck_assert_int_eq(0, pickPidUseRuntimePercentile(anchor));
	ck_assert_int_eq(0, pickPidUseRuntimePercentile(&helper));

	anchor->param = NULL;
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
	tcase_add_test(tc4, orchestrator_manage_ftrc_ppswitch_migration);
	tcase_add_test(tc4, orchestrator_manage_ftrc_ppwakeup);
	suite_add_tcase(s, tc4);

	TCase *tc5 = tcase_create("manage_ftrace_pickpid_acc");
	tcase_add_checked_fixture(tc5, orchestrator_manage_setup, orchestrator_manage_teardown);
	tcase_add_test(tc5, orchestrator_manage_ppconsrt);
	tcase_add_test(tc5, orchestrator_manage_ppckbuf);
	tcase_add_test(tc5, orchestrator_manage_ppckbuf_dlperiod);
	tcase_add_test(tc5, orchestrator_manage_siblingsfit);
	tcase_add_test(tc5, orchestrator_manage_realloc_reject);
	tcase_add_test(tc5, orchestrator_manage_runtime_percentile);
	tcase_add_test(tc5, orchestrator_manage_ftrc_loss);
	tcase_add_test(tc5, orchestrator_manage_ftrc_missingpipe);
	tcase_add_test(tc5, orchestrator_manage_ftrc_append);
	tcase_add_test(tc5, orchestrator_manage_resource_usage);
	tcase_add_test(tc5, orchestrator_manage_cpustat);
	tcase_add_test(tc5, orchestrator_manage_hist_scope);
	suite_add_tcase(s, tc5);

	return;
}
