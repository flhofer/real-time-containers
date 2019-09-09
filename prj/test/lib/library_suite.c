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
#include "dockerlink.c"
#include "../../src/include/orchdata.h"

containers_t * contparm; // container parameter settings
prgset_t * prgset; // programm setings structure

// mutex to avoid read while updater fills or empties existing threads
pthread_mutex_t dataMutex;
// head of pidlist - PID runtime and configuration details
node_t * head = NULL;

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
