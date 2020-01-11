/* 
###############################
# test script by Florian Hofer
# last change: 11/01/2020
# ©2019 all rights reserved ☺
###############################
*/

#include "update.c"
#include "manage.c"

Suite * orchestrator_suite(void) {

	Suite *s = suite_create("Orchestrator");

	// call tests and append test cases	

	// these use dbgprint. check first
	if (!dbg_out) {
		dbg_out = (FILE *)stderr;
	}
	orchestrator_update(s);
	orchestrator_manage(s);

	return s;
}
