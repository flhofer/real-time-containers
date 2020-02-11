/*
###############################
# test script by Florian Hofer
# last change: 20/01/2020
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/orchestrator/orchestrator.h"
#include "../../src/orchestrator/adaptive.h"
#include "../../src/include/parse_config.h"
#include "../../src/include/kernutil.h"
#include "../../src/include/rt-sched.h"
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <limits.h>
#include <sys/resource.h>
#include <linux/sched.h>	// linux specific scheduling
#include <check.h>

// for MUSL based systems
#ifndef RLIMIT_RTTIME
	#define RLIMIT_RTTIME 15
#endif

static void orchestrator_adaptive_setup() {
	// load test configuration from file
	prgset = malloc (sizeof(prgset_t));
	contparm = malloc (sizeof(containers_t));

	parse_config_set_default(prgset);
	parse_config_file("test/adaptive-test.json", prgset, contparm);

	prgset->affinity_mask = parse_cpumask(prgset->affinity, 4); // TODO: hardcoded

}

static void orchestrator_adaptive_teardown() {
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
	/*
	// can not know if I can free it..
	while (contparm->img){
		while (contparm->img->conts)
			pop (&contparm->img->conts);
		while (contparm->img->pids)
			pop (&contparm->img->pids);
		free (contparm->img->rscs);
		free (contparm->img->attr);
		pop(&contparm->img);
	}

	// can not know if I can free it..
	while (contparm->cont){
		while (contparm->cont->pids)
			pop (&contparm->cont->pids);
		free (contparm->cont->rscs);
		free (contparm->cont->attr);
		pop(&contparm->cont);
	}

	// can not know if I can free it..
	while (contparm->pids){
		free (contparm->pids->rscs);
		free (contparm->pids->attr);
		pop(&contparm->cont);
	}
	*/

	free(contparm);
}

/// TEST CASE -> create an adaptive allocation schedule for the loaded configuration
/// EXPECTED -> exit with no error and a created schedule in memory
START_TEST(orchestrator_adaptive_schedule)
{
	// prepare and compute schedule
	adaptPrepareSchedule();

	// get result
	struct resTracer * rhead = adaptGetAllocations();

	// check result
	ck_assert((rhead));

}
END_TEST


void orchestrator_adaptive (Suite * s) {
	TCase *tc1 = tcase_create("adaptive_schedule");

	tcase_add_checked_fixture(tc1, orchestrator_adaptive_setup, orchestrator_adaptive_teardown);
	tcase_add_test(tc1, orchestrator_adaptive_schedule);

    suite_add_tcase(s, tc1);

	return;
}
