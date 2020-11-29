/* 
###############################
# test script by Florian Hofer
# last change: 27/11/2020
# ©2020 all rights reserved ☺
###############################
*/

#include "../../src/orchestrator/resmgnt.h"
#include <check.h>
#include <sys/resource.h>
#include <linux/sched.h>	// linux specific scheduling

// for MUSL based systems
#ifndef RLIMIT_RTTIME
	#define RLIMIT_RTTIME 15
#endif

/// TEST CASE -> check return value with different resources & schedules
/// EXPECTED -> comparison value OK
START_TEST(orchestrator_resmgnt_checkValue)
{	
	resTracer_t res = {

	};
	struct sched_attr par = {
		48,
		SCHED_DEADLINE,
		0, 0, 0,
		10000,
		100000,
		100000
	};

	int rv;

	// Empty CPU
	rv = checkUvalue(&res, &par, 1);
	ck_assert_int_eq(3, rv);
	// perfect fit
	rv = checkUvalue(&res, &par, 0);
	ck_assert_int_eq(4, rv);

	// GCD match, resource is GCD = stays same
	par.sched_period = 200000;
	rv = checkUvalue(&res, &par, 0);
	ck_assert_int_eq(2, rv);

	// GCD match, task is GCD
	par.sched_period = 50000;
	rv = checkUvalue(&res, &par, 1);
	ck_assert_int_eq(1, rv);

	// GCD match, new GCD
	par.sched_period = 20000;
	rv = checkUvalue(&res, &par, 1);
	ck_assert_int_eq(-5, rv);

	// No space left
	par.sched_period = 15000;
	rv = checkUvalue(&res, &par, 0);
	ck_assert_int_eq(INT_MIN, rv);
}
END_TEST

static void orchestrator_resmgnt_setup() {
	prgset = calloc (1, sizeof(prgset_t));
	parse_config_set_default(prgset);

	prgset->affinity= "0";
	prgset->affinity_mask = parse_cpumask(prgset->affinity);

	prgset->ftrace = 1;

	// signatures and folders
	prgset->cont_ppidc = strdup(CONT_PPID);
	prgset->cont_pidc = strdup(CONT_PID);
	prgset->cont_cgrp = strdup(CONT_DCKR);

	// filepaths virtual file system
	prgset->procfileprefix = strdup("/proc/sys/kernel/");
	prgset->cpusetfileprefix = strdup("/sys/fs/cgroup/cpuset/");
	prgset->cpusystemfileprefix = strdup("/sys/devices/system/cpu/");

	prgset->cpusetdfileprefix = malloc(strlen(prgset->cpusetfileprefix) + strlen(prgset->cont_cgrp)+1);
	prgset->cpusetdfileprefix = strcat(strcpy(prgset->cpusetdfileprefix, prgset->cpusetfileprefix), prgset->cont_cgrp);

	contparm = calloc (1, sizeof(containers_t));
}

static void orchestrator_resmgnt_teardown() {
	// free memory
	while (rHead)
		pop((void**)&rHead);

	freePrgSet(prgset);
	freeContParm(contparm);
}

/// TEST CASE -> check correct creation of resource allocation
/// EXPECTED -> one resource matching the selected CPU
START_TEST(orchestrator_resmgnt_createTracer)
{

	// test filling CPU0
	createResTracer();

	ck_assert_ptr_ne(NULL, rHead);
	ck_assert_int_eq(rHead->affinity, 0);
	ck_assert_int_eq((int)rHead->basePeriod,0);
	ck_assert_int_eq((int)rHead->usedPeriod,0);
	ck_assert_int_eq((int)rHead->U,0);

	pop((void**)&rHead);

	// test no CPU
	(void)numa_bitmask_clearbit(prgset->affinity_mask, 0);

	createResTracer();

	ck_assert_ptr_eq(NULL, rHead);
}
END_TEST

/// TEST CASE -> check grepping of low resource tracer
/// EXPECTED -> one resource matching the lowest U-CPU
START_TEST(orchestrator_resmgnt_grepTracer)
{

	// test filling CPU0
	createResTracer();
	// add one more, UL 0.5
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->U = 0.5;

	ck_assert_ptr_eq(grepTracer(), rHead->next);

	// invert values
	pop((void**)&rHead);
	rHead->U = 0.5;
	push((void**)&rHead, sizeof(struct resTracer));

	ck_assert_ptr_eq(grepTracer(), rHead);
}
END_TEST

/// TEST CASE -> check getting of resource tracer
/// EXPECTED -> one resource matching the CPU id
START_TEST(orchestrator_resmgnt_getTracer)
{

	// test filling CPU0
	createResTracer();
	// add one more, CPU2
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->affinity = 6;
	// add one more, CPU1
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->affinity = 3;

	ck_assert_ptr_eq(getTracer(3), rHead);
	ck_assert_ptr_eq(getTracer(4), NULL);
	ck_assert_ptr_eq(getTracer(0), rHead->next->next);
}
END_TEST

/// TEST CASE -> check best fit for a certain period
/// EXPECTED -> one resource matching the CPU id
START_TEST(orchestrator_resmgnt_checkPeriod)
{
	// test filling CPU0
	createResTracer();
	rHead->basePeriod = 100000;
	// add one more, CPU1
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->basePeriod = 50000;
	// add one more, CPU2
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->basePeriod = 70000;
	rHead->affinity = 1;

	struct sched_attr par ={
			48,
			SCHED_DEADLINE,
			0, 0, 0,
			1000,
			10000,
			100000
	};

	ck_assert_ptr_eq(checkPeriod(&par, -99), rHead->next->next);// exact period match
	par.sched_period = 10000;
	ck_assert_ptr_eq(checkPeriod(&par, -99), rHead->next);		// par is new period, prefer higher GCD
	par.sched_period = 140000;
	ck_assert_ptr_eq(checkPeriod(&par, -99), rHead);			// par is double period ;)
	par.sched_period = 75000;
	ck_assert_ptr_eq(checkPeriod(&par, -99), rHead->next);		// no perfect fit, prefer higher GCD
	rHead->basePeriod = 100000;
	par.sched_period = 100000;
	ck_assert_ptr_eq(checkPeriod(&par, -1), rHead);				// par, prefer affinity
	rHead->U = 0.7;
	ck_assert_ptr_eq(checkPeriod(&par, -99), rHead->next->next);// par, prefer lower U
}
END_TEST

/// TEST CASE -> check best fit for a certain period
/// EXPECTED -> one resource matching the CPU id
START_TEST(orchestrator_resmgnt_checkPeriod_R)
{

	// test filling CPU0
	createResTracer();
	rHead->basePeriod = 100000;
	// add one more, CPU1
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->basePeriod = 50000;
	// add one more, CPU2
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->basePeriod = 70000;
	rHead->affinity = 1;

	node_t * item = NULL;
	node_push(&item);\
	item->param = calloc(1, sizeof(pidc_t));
	item->param->rscs = calloc(1, sizeof(struct sched_rscs));
	struct sched_attr par ={
			48,
			SCHED_DEADLINE,
			0, 0, 0,
			1000,
			10000,
			100000
	};
	item->attr = par;
	item->param->rscs->affinity = -99;

	ck_assert_ptr_eq(checkPeriod_R(item), rHead->next->next);// exact period match

	item->attr.sched_policy = SCHED_FIFO;
	item->mon.cdf_period = 50000;
	item->mon.cdf_runtime = 560;
	ck_assert_ptr_eq(checkPeriod_R(item), rHead->next);// exact period match

	node_pop(&item);
}
END_TEST

void orchestrator_resmgnt (Suite * s) {
	TCase *tc1 = tcase_create("resmgnt_periodFitting");
	tcase_add_test(tc1, orchestrator_resmgnt_checkValue);

    suite_add_tcase(s, tc1);

    TCase *tc2 = tcase_create("resmgnt_tracing");
	tcase_add_checked_fixture(tc2, orchestrator_resmgnt_setup, orchestrator_resmgnt_teardown);
	tcase_add_test(tc2, orchestrator_resmgnt_createTracer);
	tcase_add_test(tc2, orchestrator_resmgnt_getTracer);
	tcase_add_test(tc2, orchestrator_resmgnt_grepTracer);

    suite_add_tcase(s, tc2);

    TCase *tc3 = tcase_create("resmgnt_checkPeriod");
	tcase_add_checked_fixture(tc3, orchestrator_resmgnt_setup, orchestrator_resmgnt_teardown);
	tcase_add_test(tc3, orchestrator_resmgnt_checkPeriod);
	tcase_add_test(tc3, orchestrator_resmgnt_checkPeriod_R);

    suite_add_tcase(s, tc3);

	return;
}
