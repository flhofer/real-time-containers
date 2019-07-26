/* 
###############################
# test script by Florian Hofer
# last change: 25/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "update.c"

Suite * schedstat_suite(void) {

	Suite *s = suite_create("Schedstat");

	// call tests and append test cases	

	// these use dbgprint. check first
	if (!dbg_out) {
		dbg_out = (_IO_FILE *)stderr;
	}
	schedstat_update(s);

	return s;
}
