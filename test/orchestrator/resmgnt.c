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
	ck_assert_int_eq(0, rv);

	// No space left
	par.sched_period = 15000;
	rv = checkUvalue(&res, &par, 0);
	ck_assert_int_eq(INT_MIN, rv);
}
END_TEST

void orchestrator_resmgnt (Suite * s) {
	TCase *tc1 = tcase_create("resmgnt_fitting");
	tcase_add_test(tc1, orchestrator_resmgnt_checkValue);

    suite_add_tcase(s, tc1);

	return;
}
