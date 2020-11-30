/* 
###############################
# test script by Florian Hofer
# last change: 17/02/2020
# ©2019 all rights reserved ☺
###############################
*/
#include "library_suite.h"
#include "../test.h"

#include "dockerlinkTest.c"
#include "kernutilTest.c"
#include "orchdataTest.c"
#include "parse_configTest.c"

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
