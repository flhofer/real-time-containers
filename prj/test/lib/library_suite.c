/* 
###############################
# test script by Florian Hofer
# last change: 17/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "kernutil.c"
#include "orchdata.c"
#include "parse_config.c"

Suite * library_suite(void) {

	Suite *s = suite_create("Libraries");

	// call tests and append test cases	
	library_kernutil(s);
	library_orchdata(s);

	// these use dbgprint. check first
	if (!dbg_out) {
		dbg_out = (_IO_FILE *)stderr;
	}
	library_parse_config(s);

	return s;
}
