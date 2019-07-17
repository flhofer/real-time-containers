/* 
###############################
# test script by Florian Hofer
# last change: 15/04/2019
# ©2019 all rights reserved ☺
###############################
*/

#include <stdlib.h>
#include <check.h>
#include "../../src/include/error.h"
#include "../../src/include/kernutil.h"

START_TEST(kernutil_getkernvar)
{
	char * value;
	value = malloc(50);		
	
	int read = getkernvar("/proc/", "version_signature", value, 50);
	ck_assert_int_gt(read, 20);
	ck_assert_str_gt(value, "Ubuntu");

	free(value);
}
END_TEST

START_TEST(kernutil_setkernvar)
{
	char * value;
	value = strdup("-1");
	
	int written = setkernvar("/proc/", "version_signature", value, 0);
	ck_assert_int_eq(written, -1);

	free(value);
}
END_TEST

