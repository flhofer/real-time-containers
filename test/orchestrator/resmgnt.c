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
	while (nhead)
		node_pop(&nhead);

	freePrgSet(prgset);
	freeContParm(contparm);
}

/// TEST CASE -> check correct creation of resource allcation
/// EXPECTED -> one resource matching the selected cpu
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

void orchestrator_resmgnt (Suite * s) {
	TCase *tc1 = tcase_create("resmgnt_fitting");
	tcase_add_test(tc1, orchestrator_resmgnt_checkValue);

    suite_add_tcase(s, tc1);

    TCase *tc2 = tcase_create("resmgnt_tracing");
	tcase_add_test(tc2, orchestrator_resmgnt_createTracer);
	tcase_add_checked_fixture(tc2, orchestrator_resmgnt_setup, orchestrator_resmgnt_teardown);

    suite_add_tcase(s, tc2);

	return;
}
