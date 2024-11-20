/* 
###############################
# test script by Florian Hofer
# last change: 11/01/2020
# ©2019 all rights reserved ☺
###############################
*/

#include "orchestrator_suite.h"
#include "../test.h"

#include "adaptiveTest.h"
#include "manageTest.h"
#include "resmgntTest.h"
#include "updateTest.h"

Suite * orchestrator_suite(void) {

	Suite *s = suite_create("Orchestrator");

	// call tests and append test cases	

	// these use dbgprint. check first
	if (!dbg_out) {
		dbg_out = (FILE *)stderr;
	}
	if (!stats_out) {
		stats_out = (FILE *)stderr;
	}
	orchestrator_update(s);
	orchestrator_manage(s);
	orchestrator_adaptive(s);
	orchestrator_resmgnt(s);

	return s;
}
