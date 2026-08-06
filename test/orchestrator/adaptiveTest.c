/*
###############################
# test script by Florian Hofer
# last change: 20/01/2020
# ©2019 all rights reserved ☺
###############################
*/

#include "adaptiveTest.h"
#include "../test.h"

// Includes from orchestrator library
#include "../../src/include/orchdata.h"
#include "../../src/include/parse_config.h"
#include "../../src/include/kernutil.h"
#include "../../src/include/rt-sched.h"

// tested
#include "../../src/orchestrator/adaptive.c"
// dependent includes - part of resmgntTest.h
#include "../../src/orchestrator/resmgnt.h"

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
	contparm = calloc (1, sizeof(containers_t));
	parse_config_set_default(prgset);
}

static void orchestrator_adaptive_teardown() {

	adaptFree();
	freeTracer(&rHead);
	freePrgSet(prgset);
	freeContParm(contparm);
}

/// TEST CASE -> test affinity mask creation based on dependencies
/// EXPECTED -> if specific, empty but 1, AND- mapped otherwise
START_TEST(orchestrator_adaptive_createAffinity)
{
	prgset->affinity_mask = parse_cpumask("0-3");

	// first default to a value
	struct sched_rscs rscs = { 2 };
	struct bitmask * bDep = numa_allocate_cpumask();

	// set dependency mask to 1,3,5,7, including cpus outside allocated mask
	numa_bitmask_setbit(bDep, 1);
	numa_bitmask_setbit(bDep, 3);
	numa_bitmask_setbit(bDep, 5);
	numa_bitmask_setbit(bDep, 7);

	createAffinityMask(&rscs, bDep);

	// assert we used the specific affinity and not the dependency mask
	ck_assert_int_eq(1, numa_bitmask_weight(rscs.affinity_mask));
	ck_assert(numa_bitmask_isbitset(rscs.affinity_mask, 2));

	numa_free_cpumask(rscs.affinity_mask);

	// reset to default, and check AND- mapping with dependency mask
	rscs.affinity = -1;

	createAffinityMask(&rscs, bDep);

	// test that the resulting mask does not contain any invalid or masked cpus
	ck_assert_int_eq(2, numa_bitmask_weight(rscs.affinity_mask));
	ck_assert(numa_bitmask_isbitset(rscs.affinity_mask, 1));
	ck_assert(numa_bitmask_isbitset(rscs.affinity_mask, 3));

	numa_free_cpumask(rscs.affinity_mask);
	numa_free_cpumask(bDep);
}
END_TEST

/// TEST CASE -> compare allocations by period, runtime and scheduler
/// EXPECTED -> ordering remains exact for small utilization differences and large values
START_TEST(orchestrator_adaptive_compare)
{
	struct sched_attr attrA = { SCHED_ATTR_SIZE, SCHED_DEADLINE };
	struct sched_attr attrB = { SCHED_ATTR_SIZE, SCHED_DEADLINE };
	cont_t itemA = { 0 };
	cont_t itemB = { 0 };
	resAlloc_t allocA = { 0 };
	resAlloc_t allocB = { 0 };
	itemA.attr = &attrA;
	itemB.attr = &attrB;
	allocA.item = &itemA;
	allocB.item = &itemB;

	// test period and runtime comparison
	attrA.sched_period = 1000;
	attrA.sched_runtime = 500;
	attrB.sched_period = 2000;
	attrB.sched_runtime = 500;
	ck_assert_int_eq(1000, cmpPidItemS(&allocA, &allocB));
	ck_assert_int_eq(-1000, cmpPidItemS(&allocB, &allocA));
	attrA.sched_period = 2000;
	ck_assert_int_eq(0, cmpPidItemS(&allocA, &allocB));
	attrA.sched_runtime = 200;
	ck_assert_int_eq(-300, cmpPidItemS(&allocA, &allocB));

	// test period comparison with extreme values
	attrA.sched_period = UINT64_MAX;
	attrB.sched_period = 1;
	ck_assert_int_eq(2, cmpPidItemS(&allocA, &allocB));
	ck_assert_int_eq(-2, cmpPidItemS(&allocB, &allocA));

	// test scheduler comparison
	attrA.sched_period = attrB.sched_period = 0;
	attrA.sched_runtime = attrB.sched_runtime = 0;
	attrA.sched_policy = SCHED_FIFO;
	attrB.sched_policy = SCHED_RR;
	ck_assert_int_lt(0, cmpPidItemS(&allocA, &allocB));
	ck_assert_int_eq(0, cmpPidItemS(&allocA, &allocA));
}
END_TEST

/// TEST CASE -> rebuild a tracer from all allocations assigned to it
/// EXPECTED -> base period, used period and utilization contain the complete load
START_TEST(orchestrator_adaptive_recompute)
{
	struct sched_attr attrA = { SCHED_ATTR_SIZE, SCHED_DEADLINE };
	struct sched_attr attrB = { SCHED_ATTR_SIZE, SCHED_DEADLINE };
	attrA.sched_runtime = 200;
	attrA.sched_deadline = attrA.sched_period = 1000;
	attrB.sched_runtime = 300;
	attrB.sched_deadline = attrB.sched_period = 1000;
	cont_t itemA = { .attr = &attrA };
	cont_t itemB = { .attr = &attrB };
	resTracer_t tracer = { .status = MSK_STATHRMC };

	push((void**)&aHead, sizeof(*aHead));
	aHead->item = &itemA;
	aHead->assigned = &tracer;
	push((void**)&aHead, sizeof(*aHead));
	aHead->item = &itemB;
	aHead->assigned = &tracer;

	ck_assert_int_eq(0, recomputeTimes_S(&tracer));
	ck_assert_uint_eq(1000, tracer.basePeriod);
	ck_assert_uint_eq(500, tracer.usedPeriod);
	ck_assert_float_eq_tol(0.5, tracer.U, 0.0001);

	attrB.sched_deadline = attrB.sched_period = 1500;
	ck_assert_int_eq(0, recomputeTimes_S(&tracer));
	ck_assert_uint_eq(3000, tracer.basePeriod);
	ck_assert_uint_eq(1200, tracer.usedPeriod);
	ck_assert_float_eq_tol(0.4, tracer.U, 0.0001);
}
END_TEST

/// TEST CASE -> reassign a flexible task away from an overloaded resource
/// EXPECTED -> the new tracer gains the load and the old tracer is recomputed
START_TEST(orchestrator_adaptive_scramble)
{
	prgset->affinity_mask = parse_cpumask("0-1");
	createResTracer();
	resTracer_t * old = getTracer(0);
	resTracer_t * replacement = getTracer(1);
	old->basePeriod = 1000;
	old->usedPeriod = 800;
	old->U = 0.8;

	struct sched_attr attr = { SCHED_ATTR_SIZE, SCHED_DEADLINE };
	attr.sched_runtime = 600;
	attr.sched_deadline = attr.sched_period = 1000;
	rscs_t resources = { .affinity = -1 };
	resources.affinity_mask = parse_cpumask("0-1");
	cont_t item = { .attr = &attr, .rscs = &resources };
	push((void**)&aHead, sizeof(*aHead));
	aHead->item = &item;
	aHead->assigned = old;

	adaptScramble();
	ck_assert_ptr_eq(replacement, aHead->assigned);
	ck_assert_float_eq_tol(0.0, old->U, 0.0001);
	ck_assert_uint_eq(0, old->usedPeriod);
	ck_assert_float_eq_tol(0.6, replacement->U, 0.0001);

	adaptExecute();
	ck_assert_int_eq(1, resources.affinity);
	numa_free_cpumask(resources.affinity_mask);
}
END_TEST

/// TEST CASE -> allocate tasks without timing information by scheduler class
/// EXPECTED -> every class is assigned and tasks in the same class stay together
START_TEST(orchestrator_adaptive_policy_fallback)
{
	prgset->affinity_mask = parse_cpumask("0-2");
}
END_TEST

/// TEST CASE -> create resources for the adaptive schedule
/// EXPECTED -> exit with no error and created resources
START_TEST(orchestrator_adaptive_resources)
{
	prgset = calloc (1, sizeof(prgset_t));
	prgset->affinity_mask = parse_cpumask("1-2"); // limited by tester's cpu :/

	contparm = calloc (1, sizeof(containers_t));
	contparm->rscs = malloc(sizeof(struct sched_rscs));
	contparm->rscs->affinity = -1;

	// prepare and compute schedule
	adaptPrepareSchedule();
	adaptPlanSchedule();

	// check result

	// valid and exact 3 elements, CPU 0, 1 and 2
	ck_assert((rHead));
	ck_assert((rHead->next));
	ck_assert(!(rHead->next->next));

	// check CPU assignments
	ck_assert_int_eq(1, getTracerMainCPU(rHead));
	ck_assert_int_eq(2, getTracerMainCPU(rHead->next));

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
	parse_config_file("test/resources/adaptive-test.json", prgset, contparm);
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
	// unknown container set to free tracer
	ck_assert_int_eq(0, cont->rscs->affinity);

	cont=cont->next;
	// container -2, but one of it's pids 2, the other 0-> 0+2, stays -2 for now
	ck_assert_int_eq(0, cont->rscs->affinity);
	// second (first in list) pid in container on -2 -> gets 0 for period match
	ck_assert_int_eq(0, cont->pids->pid->rscs->affinity);
	// first pid in container on 2 -> stays
	ck_assert_int_eq(2, cont->pids->next->pid->rscs->affinity);

	// check result of CPU assignments
	ck_assert_int_eq(1000000000, rHead->basePeriod);
	ck_assert_int_eq(380000000, rHead->usedPeriod);
	ck_assert_float_eq_tol(0.38, rHead->U, 0.0001);

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
	parse_config_file("test/resources/adaptive-test2.json", prgset, contparm);
	prgset->affinity_mask = parse_cpumask(prgset->affinity);
	prgset->rrtime = 100; // set to 100ms default for sample

	// prepare and compute schedule
	adaptPrepareSchedule();
	adaptPlanSchedule();
	// apply to resources
	adaptExecute();

	// verify memory result in parameters

	// image 1, -3 -> 1 for cpu assigned to RR w/o period
	ck_assert_int_eq(1, contparm->img->rscs->affinity);

	cont_t * cont = contparm->cont;
	// container set to RR -1, fits to 1's RR w/o period
	ck_assert_int_eq(1, cont->rscs->affinity);

	cont=cont->next;
	// unknown container set to other -2, => 0  w/o period
	ck_assert_int_eq(0, cont->rscs->affinity);

	cont=cont->next;
	// container -2, but one of it's pids 2, the other 1 -> 1+2, no preferemnce, helper assignmetn stays -2 for now => 0
	ck_assert_int_eq(0, cont->rscs->affinity);
	// second (first in list) pid in container on -2 -> gets 1 for period match
	ck_assert_int_eq(1, cont->pids->pid->rscs->affinity);
	// first pid in container on 2 -> stays
	ck_assert_int_eq(2, cont->pids->next->pid->rscs->affinity);

	// container -2, fits period 400, 2
	ck_assert_int_eq(2, contparm->pids->rscs->affinity);
	// set to -99, no period, 0 has only 1 task U0.1 period 100
	ck_assert_int_eq(1, contparm->pids->next->rscs->affinity);


	// get result

	// check result of CPU assignments
	ck_assert_int_eq(1000000000, rHead->basePeriod);
	ck_assert_int_eq(200000000, rHead->usedPeriod);
	ck_assert_float_eq_tol(0.2, rHead->U, 0.0001);

	ck_assert_int_eq(1000000000, rHead->next->basePeriod);
	ck_assert_int_eq(420000000, rHead->next->usedPeriod);
	ck_assert_float_eq_tol(0.42, rHead->next->U, 0.0001);

	ck_assert_int_eq(400000000, rHead->next->next->basePeriod);
	ck_assert_int_eq(220000000, rHead->next->next->usedPeriod);
	ck_assert_float_eq_tol(0.55, rHead->next->next->U, 0.0001);
}
END_TEST

/// TEST CASE -> create an adaptive allocation schedule for the loaded configuration
/// EXPECTED -> exit with no error and a created schedule in memory
START_TEST(orchestrator_adaptive_error_schedule)
{
	// read config from file
	parse_config_file("test/resources/adaptive-terr.json", prgset, contparm);
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

	TCase *tc1 = tcase_create("adaptive_helper");
	tcase_add_checked_fixture(tc1, orchestrator_adaptive_setup, orchestrator_adaptive_teardown);
	tcase_add_test(tc1, orchestrator_adaptive_createAffinity);
	tcase_add_test(tc1, orchestrator_adaptive_compare);
	tcase_add_test(tc1, orchestrator_adaptive_recompute);
	tcase_add_test(tc1, orchestrator_adaptive_scramble);
	tcase_add_test(tc1, orchestrator_adaptive_policy_fallback);

	suite_add_tcase(s, tc1);

	TCase *tc2 = tcase_create("adaptive_resources");
	tcase_add_test(tc2, orchestrator_adaptive_resources);

	suite_add_tcase(s, tc2);

	TCase *tc3 = tcase_create("adaptive_schedule");
	tcase_add_checked_fixture(tc3, orchestrator_adaptive_setup, orchestrator_adaptive_teardown);
    tcase_add_exit_test(tc3, orchestrator_adaptive_error_schedule, EXIT_FAILURE);
    tcase_add_test(tc3, orchestrator_adaptive_schedule);
    tcase_add_test(tc3, orchestrator_adaptive_schedule2);

    suite_add_tcase(s, tc3);

	return;
}
