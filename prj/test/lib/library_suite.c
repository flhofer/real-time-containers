/* 
###############################
# test script by Florian Hofer
# last change: 17/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "kernutil.c"
#include "orchdata.c"

Suite * library_suite(void) {

	Suite *s = suite_create("Libraries");

	// call tests and append test cases	
	library_kernutil(s);
	library_orchdata(s);

	return s;
}
