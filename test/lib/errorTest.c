/*
###############################
# test script by Florian Hofer
# last change: 01/12/2020
# ©2020 all rights reserved ☺
###############################
*/

#include "errorTest.h"

// tested
#include "../../src/lib/error.c"



void library_error (Suite * s) {

	TCase *tc1 = tcase_create("error");

    suite_add_tcase(s, tc1);

	return;
}
