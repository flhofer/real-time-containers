/* 
###############################
# test runner main script by Florian Hofer
# last change: 28/02/2019
# ©2019 all rights reserved ☺
###############################
*/

#include <check.h>
#include "lib/kernutil.c"

// debug output file
FILE  * dbg_out;


Suite * library_suite(void) {

	Suite *s = suite_create("Libraries");
	TCase *tc1 = tcase_create("kernutil");
 
    tcase_add_test(tc1, kernutil_getkernvar);
    tcase_add_test(tc1, kernutil_setkernvar);

    suite_add_tcase(s, tc1);

	return s;
}

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


