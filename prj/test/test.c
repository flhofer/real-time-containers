/* 
###############################
# test runner main script by Florian Hofer
# last change: 28/02/2019
# ©2019 all rights reserved ☺
###############################
*/

#include <check.h>
#include <stdio.h>
#include <errno.h>

// debug output file
FILE  * dbg_out;

#include "lib/library_suite.c"

// generic..
#include "../src/include/error.h"

int main(void)
{

	Suite * s1 = library_suite();

    SRunner *sr = srunner_create(s1);
	// uncomment below for debugging
//	srunner_set_fork_status (sr, CK_NOFORK);
    int nf;

    srunner_run_all(sr, CK_VERBOSE);
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);

    return nf == 0 ? 0 : 1;
}


