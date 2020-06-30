/*
###############################
# test script by Florian Hofer
# last change: 20/01/2020
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/orchestrator/adaptive.h"
#include "../../src/include/orchdata.h"
#include "../../src/include/parse_config.h"
#include "../../src/include/kernutil.h"
#include "../../src/include/rt-sched.h"
#include <check.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <limits.h>
#include <sys/resource.h>
#include <linux/sched.h>	// linux specific scheduling

// for MUSL based systems
#ifndef RLIMIT_RTTIME
	#define RLIMIT_RTTIME 15
#endif

static void orchestrator_adaptive_setup() {
	// load test configuration from file
	prgset = malloc (sizeof(prgset_t));
	contparm = calloc (sizeof(containers_t),1);
	parse_config_set_default(prgset);
}

static void orchestrator_adaptive_teardown() {

	adaptFree();
	freeTracer(&rHead);
	freePrgSet(prgset);
	freeContParm(contparm);
}

/// TEST CASE -> create resources for the adaptive schedule
/// EXPECTED -> exit with no error and created resources
START_TEST(orchestrator_adaptive_resources)
{

	prgset = calloc (sizeof(prgset_t),1);
	prgset->affinity_mask = parse_cpumask("1-2"); // limited by tester's cpu :/

	contparm = calloc (sizeof(containers_t),1);
	contparm->rscs = malloc(sizeof(struct sched_rscs));
	contparm->rscs->affinity = -1;
	contparm->rscs->affinity_mask = numa_allocate_cpumask();
	copy_bitmask_to_bitmask(prgset->affinity_mask, contparm->rscs->affinity_mask);

	// prepare and compute schedule
	adaptPrepareSchedule();
	adaptPlanSchedule();

	// check result

	// valid and exact 3 elements, CPU 0, 1 and 2
	ck_assert((rHead));
	ck_assert((rHead->next));
	ck_assert(!(rHead->next->next));

	// check CPU assignments
	ck_assert_int_eq(1, rHead->affinity);
	ck_assert_int_eq(2, rHead->next->affinity);

	// check element initialization values
	ck_assert_int_eq(0, rHead->basePeriod);
	ck_assert_int_eq(0, rHead->usedPeriod);

	adaptFree();
	freeTracer(&rHead);

	freePrgSet(prgset);
	freeContParm(contparm);
}
END_TEST

/// TEST CASE -> create an adaptive allocation schedule for the loaded configuration
/// EXPECTED -> exit with no error and a created schedule in memory
START_TEST(orchestrator_adaptive_schedule)
{
	// read config from file
	parse_config_file("test/adaptive-test.json", prgset, contparm);
	prgset->affinity_mask = parse_cpumask(prgset->affinity);
	prgset->rrtime = 100; // set to 100 ms default for sample

	// prepare and compute schedule
	adaptPrepareSchedule();
	adaptPlanSchedule();
	// apply to resources
	adaptExecute();

	// verify memory result in parameters

	// image 1, -1 -> 2 for match period
	ck_assert_int_eq(2, contparm->img->rscs->affinity);

	cont_t * cont = contparm->cont;
	// container set to other -1
	ck_assert_int_eq(2, cont->rscs->affinity);

	cont=cont->next;
	// unknown container set to other -1
	ck_assert_int_eq(-2, cont->rscs->affinity);

	cont=cont->next;
	// container -2, but one of it's pids 2, the other 0-> 0+2, stays -2 for now
	ck_assert_int_eq(-2, cont->rscs->affinity);
	// second (first in list) pid in container on -2 -> gets 0 for period match
	ck_assert_int_eq(0, cont->pids->pid->rscs->affinity);
	// first pid in container on 2 -> stays
	ck_assert_int_eq(2, cont->pids->next->pid->rscs->affinity);

	// check result of CPU assignments
	ck_assert_int_eq(5000000, rHead->basePeriod);
	ck_assert_int_eq(900000, rHead->usedPeriod);
	//check >= 0.11 has ck_assert_float
	ck_assert((float)((double)900000/(double)5000000) == rHead->U);

	ck_assert_int_eq(4000000, rHead->next->basePeriod);
	ck_assert_int_eq(3000000, rHead->next->usedPeriod);
	ck_assert((float)((double)3000000/(double)4000000) == rHead->next->U);

}
END_TEST

/// TEST CASE -> create an adaptive allocation schedule with RR and FIFO
/// EXPECTED -> exit with no error and a created schedule in memory
START_TEST(orchestrator_adaptive_schedule2)
{
	// read config from file
	parse_config_file("test/adaptive-test2.json", prgset, contparm);
	prgset->affinity_mask = parse_cpumask(prgset->affinity);
	prgset->rrtime = 100; // set to 100ms default for sample

	// prepare and compute schedule
	adaptPrepareSchedule();
	adaptPlanSchedule();
	// apply to resources
	adaptExecute();

	adaptScramble();

	// verify memory result in parameters

	// image 1, -1 -> 0 for empty cpu
	ck_assert_int_eq(0, contparm->img->rscs->affinity);

	cont_t * cont = contparm->cont;
	// container set to other -1, fits to 1
	ck_assert_int_eq(1, cont->rscs->affinity);

	cont=cont->next;
	// unknown container set to other -1
	ck_assert_int_eq(-2, cont->rscs->affinity);

	cont=cont->next;
	// container -2, but one of it's pids 2, the other 0-> 0+2, stays -2 for now
	ck_assert_int_eq(-2, cont->rscs->affinity);
	// second (first in list) pid in container on -2 -> gets 1 for period match
	ck_assert_int_eq(1, cont->pids->pid->rscs->affinity);
	// first pid in container on 2 -> stays
	ck_assert_int_eq(2, cont->pids->next->pid->rscs->affinity);


	// container -2, fits period 400, 2
	ck_assert_int_eq(2, contparm->pids->rscs->affinity);
	// set to -99, no period, 0 has only 1 task U0.1 period 100
	ck_assert_int_eq(0, contparm->pids->next->rscs->affinity);


	// get result

	// check result of CPU assignments
	ck_assert_int_eq(1000000000, rHead->basePeriod);
	ck_assert_int_eq(220000000, rHead->usedPeriod);
	//check >= 0.11 has ck_assert_float
	ck_assert((float)((double)220000000/(double)1000000000) == rHead->U);

	ck_assert_int_eq(500000000, rHead->next->basePeriod);
	ck_assert_int_eq(100000000, rHead->next->usedPeriod);
	ck_assert((float)((double)100000000/(double)500000000) == rHead->next->U);  // 90+10

	ck_assert_int_eq(400000000, rHead->next->next->basePeriod);
	ck_assert_int_eq(220000000, rHead->next->next->usedPeriod);
	ck_assert((float)((double)22000000/(double)40000000) == rHead->next->next->U);

}
END_TEST

/// TEST CASE -> create an adaptive allocation schedule for the loaded configuration
/// EXPECTED -> exit with no error and a created schedule in memory
START_TEST(orchestrator_adaptive_error_schedule)
{
	// read config from file
	parse_config_file("test/adaptive-terr.json", prgset, contparm);
	prgset->affinity_mask = parse_cpumask(prgset->affinity);

	// prepare and compute schedule
	adaptPrepareSchedule();

}
END_TEST

void orchestrator_adaptive (Suite * s) {

	int numcpu = numa_num_configured_cpus();
	if (numcpu < 3){
		warn("adapt: Not enough CPU's for this test. Skipping");
		return;
	}

	TCase *tc1 = tcase_create("adaptive_resources");
	tcase_add_test(tc1, orchestrator_adaptive_resources);

	suite_add_tcase(s, tc1);

	TCase *tc2 = tcase_create("adaptive_schedule");
	tcase_add_checked_fixture(tc2, orchestrator_adaptive_setup, orchestrator_adaptive_teardown);
    tcase_add_exit_test(tc2, orchestrator_adaptive_error_schedule, EXIT_FAILURE);
    tcase_add_test(tc2, orchestrator_adaptive_schedule);
    tcase_add_test(tc2, orchestrator_adaptive_schedule2);

    suite_add_tcase(s, tc2);

	return;
}
