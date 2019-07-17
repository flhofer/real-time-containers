/* 
###############################
# test script by Florian Hofer
# last change: 15/04/2019
# ©2019 all rights reserved ☺
###############################
*/

#include <check.h>
#include "../../src/include/error.h"
#include "../../src/include/kernutil.h"

START_TEST(kernutil_check_kernel)
{	
	ck_assert_int_eq(check_kernel(), KV_413);
}
END_TEST

struct kernvar_test {
	char * path;
	char * var;
	char * val;
	int count;
};

static const struct kernvar_test getkernvar_var[4] = {
		{"/proc/", "version_signature", "Ubuntu", 36},
		{"/proc/","meminfo", "MemTotal", 50},
		{"/proc/","noexist", "", -1},
		{"/sys/devices/systems/cpu/", "isolated", 0}
	}; 

START_TEST(kernutil_getkernvar)
{
	char * value;
	value = malloc(50);		
	
	int read = getkernvar(getkernvar_var[_i].path, getkernvar_var[_i].var, value, 50);
	ck_assert_int_eq(read, getkernvar_var[_i].count);
	ck_assert_str_ge(value, getkernvar_var[_i].val);

	free(value);
}
END_TEST

static const struct kernvar_test setkernvar_var[2] = {
		{"/dev/", "null", "Ubuntu", 6},
		{"/proc/","noexist", "", -1},
	}; 

START_TEST(kernutil_setkernvar)
{
	int written = setkernvar(setkernvar_var[_i].path, setkernvar_var[_i].var,
		setkernvar_var[_i].val, 0);
	ck_assert_int_eq(written, setkernvar_var[_i].count);
}
END_TEST

TCase * library_kernutil () {
	TCase *tc = tcase_create("kernutil");
 
    tcase_add_loop_test(tc, kernutil_getkernvar, 0, 4);
    tcase_add_loop_test(tc, kernutil_setkernvar, 0, 2);
	tcase_add_test(tc, kernutil_check_kernel);

	return tc;
}
