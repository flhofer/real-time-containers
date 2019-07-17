/* 
###############################
# test runner main script by Florian Hofer
# last change: 28/02/2019
# ©2019 all rights reserved ☺
###############################
*/

#include <check.h>
#include "lib/library_suite.c"

// debug output file
FILE  * dbg_out;

int main(void)
{

	Suite * s1 = library_suite();

    SRunner *sr = srunner_create(s1);
    int nf;

    srunner_run_all(sr, CK_VERBOSE);
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);

    return nf == 0 ? 0 : 1;
}


