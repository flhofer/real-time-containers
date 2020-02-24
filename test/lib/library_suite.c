/* 
###############################
# test script by Florian Hofer
# last change: 17/02/2020
# ©2019 all rights reserved ☺
###############################
*/

#include "kernutil.c"
#include "orchdata.c"
#include "parse_config.c"
#include "dockerlink.c"

Suite * library_suite(void) {

	Suite *s = suite_create("Library");

	// call tests and append test cases	
	library_kernutil(s);
	library_orchdata(s);

	// these use dbgprint. check first
//	dbg_out = fopen("/dev/null", "w");
	if (!dbg_out) {
		dbg_out = (FILE *)stderr;
	}
	library_parse_config(s);
	library_dockerlink(s);

	return s;
}
