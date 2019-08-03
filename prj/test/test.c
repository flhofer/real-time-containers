/* 
###############################
# test runner main script by Florian Hofer
# last change: 25/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include <check.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

// debug output file
FILE  * dbg_out;

#include "lib/library_suite.c"
#include "schedstat/schedstat_suite.c"

// generic..
#include "../src/include/error.h"

int main(void)
{
	// init pseudo-random tables
	srand(time(NULL));

    int nf=0;
    SRunner *sr;

	Suite * s1 = library_suite();
	sr = srunner_create(s1);
	// uncomment below for debugging
//	srunner_set_fork_status (sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    nf += srunner_ntests_failed(sr);
    srunner_free(sr);

	Suite * s2 = schedstat_suite();
    sr = srunner_create(s2);
	// uncomment below for debugging
//	srunner_set_fork_status (sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    nf += srunner_ntests_failed(sr);
    srunner_free(sr);

    return nf == 0 ? 0 : 1;
}


