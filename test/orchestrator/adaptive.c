/*
###############################
# test script by Florian Hofer
# last change: 20/01/2020
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/orchestrator/adaptive.h"
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

	adaptFreeTracer();

	free(prgset->logdir);
	free(prgset->logbasename);

	// signatures and folders
	free(prgset->cont_ppidc);
	free(prgset->cont_pidc);
	free(prgset->cont_cgrp);

	// filepaths virtual file system
	free(prgset->procfileprefix);
	free(prgset->cpusetfileprefix);
	free(prgset->cpusystemfileprefix);

	free(prgset->cpusetdfileprefix);

	free(prgset);


	// free resources!!
	while (contparm->img){
		while (contparm->img->conts) {
			while (contparm->img->conts->cont->pids){
				// free and pop
				freeParm ((cont_t**)&contparm->img->conts->cont->pids->pid, contparm->attr, contparm->rscs, 2);
				pop((void**)&contparm->img->conts->cont->pids);
			}
			// free and pop
			freeParm (&contparm->img->conts->cont, contparm->attr, contparm->rscs, 1);
			pop((void**)&contparm->img->conts);
		}
		while (contparm->img->pids){
			freeParm ((cont_t**)&contparm->img->pids->pid, contparm->attr, contparm->rscs, 1);
			pop ((void**)&contparm->img->pids);
		}
		freeParm ((cont_t**)&contparm->img, contparm->attr, contparm->rscs, 0);
	}

	while (contparm->cont){
		while (contparm->cont->pids){
			freeParm ((cont_t**)&contparm->cont->pids->pid, contparm->attr, contparm->rscs, 1);
			pop ((void**)&contparm->cont->pids);
		}
		freeParm (&contparm->cont, contparm->attr, contparm->rscs, 0);
	}

	while (contparm->pids)
		freeParm ((cont_t**)&contparm->pids, contparm->attr, contparm->rscs, 0);

	free(contparm->attr);
	free(contparm->rscs);
	free(contparm);
}

/// TEST CASE -> create resources for the adaptive schedule
/// EXPECTED -> exit with no error and created resources
START_TEST(orchestrator_adaptive_resources)
{

	prgset = calloc (sizeof(prgset_t),1);
	contparm = calloc (sizeof(containers_t),1);
	prgset->affinity_mask = parse_cpumask("1-2"); // limited by tester's cpu :/

	// prepare and compute schedule
	adaptPrepareSchedule();

	// get result
	struct resTracer * rhead = adaptGetTracers();

	// check result

	// valid and exact 3 elements, CPU 1 and 2
	ck_assert((rhead));
	ck_assert((rhead->next));
	ck_assert(!(rhead->next->next));

	// check CPU assignments
	ck_assert_int_eq(1, rhead->affinity);
	ck_assert_int_eq(2, rhead->next->affinity);

	// check element initialization values
	ck_assert_int_eq(0, rhead->basePeriod);
	ck_assert_int_eq(0, rhead->usedPeriod);

	numa_bitmask_free(prgset->affinity_mask);
	free(prgset);
	free(contparm);
	adaptFreeTracer();
}
END_TEST

/// TEST CASE -> create an adaptive allocation schedule for the loaded configuration
/// EXPECTED -> exit with no error and a created schedule in memory
START_TEST(orchestrator_adaptive_schedule)
{
	// read config from file
	parse_config_file("test/adaptive-test.json", prgset, contparm);
	prgset->affinity_mask = parse_cpumask(prgset->affinity);

	// prepare and compute schedule
	adaptPrepareSchedule();
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
	// container -2, but one of it's pids 2 -> 2
	ck_assert_int_eq(-2, cont->rscs->affinity);
	// second (first in list) pid in container on -2 -> gets 1 for period match
	ck_assert_int_eq(0, cont->pids->pid->rscs->affinity);
	// first pid in container on 2 -> stays
	ck_assert_int_eq(2, cont->pids->next->pid->rscs->affinity);

	// get result
	struct resTracer * rhead = adaptGetTracers();

	// check result of CPU assignments
	ck_assert_int_eq(5000000, rhead->basePeriod);
	ck_assert_int_eq(900000, rhead->usedPeriod);
	//check >= 0.11 has ck_assert_float
	ck_assert((float)((double)900000/(double)5000000) == rhead->U);

	ck_assert_int_eq(4000000, rhead->next->basePeriod);
	ck_assert_int_eq(3000000, rhead->next->usedPeriod);
	ck_assert((float)((double)3000000/(double)4000000) == rhead->next->U);

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
	if (numcpu < 4){
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

    suite_add_tcase(s, tc2);

	return;
}
