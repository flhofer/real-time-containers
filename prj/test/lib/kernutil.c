/* 
###############################
# test script by Florian Hofer
# last change: 17/12/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/include/kernutil.h"

// TODO: msr test functions!

START_TEST(kernutil_check_kernel)
{	
	// NOTE, kernel version of local system, should be >= 5.0
	ck_assert_int_eq(check_kernel(), KV_50);
}
END_TEST

struct kernvar_test {
	char * path;
	char * var;
	char * val;
	int count;
	int errcode;
};

static const struct kernvar_test getkernvar_var[6] = {
		{"/proc/", "version_signature", "Ubuntu", 0, 0},	// standard read - len = 0, changes by kernel version
		{"/proc/","meminfo", "MemTotal", 50, 0},			// buffer too small
		{"/proc/","noexist", "", 0, ENOENT},				// entry does not exist
		{"/sys/devices/system/cpu/", "isolated","\0", 1, 0},// empty entry
		{"/sys/devices/system/cpu/sys/devices/system/cpu/sys/devices/system/cpu/sys/devices/system/cpu/sys/devices/system/cpu/sys/",
			"devices1", "", 0, ENOMEM},						// too long filename+varname > 128
		{"/sys/devices/system/cpu/isolated/",
			"thread_siblings_list", "", 0, ENOTDIR}			// not a valid dir string
	}; 

START_TEST(kernutil_getkernvar)
{
	char * value;
	value = malloc(50);		
	
	int read = getkernvar(getkernvar_var[_i].path, getkernvar_var[_i].var, value, 50);
	if ( 0 < getkernvar_var[_i].count)
		ck_assert_int_eq(read, getkernvar_var[_i].count); // ignore if expected == 0
	ck_assert_str_ge(value, getkernvar_var[_i].val);
	ck_assert_int_eq(errno, getkernvar_var[_i].errcode);

	free(value);
}
END_TEST

static const struct kernvar_test setkernvar_var[5] = {
		{"/dev/", "null", "Ubuntu", 6, 0},					// write to var
		{"/proc/","version_signature", "test", 0, EACCES},	// write protected
		{"/dev/", "null", "", 0, 0},						// write empty -> special case TODO
		{"/dev/", "null", NULL, 0, EINVAL},					// write NULL
		{"/dev/", NULL, "", 0, EINVAL},						// write to NULL
	}; 

START_TEST(kernutil_setkernvar)
{
	int written = setkernvar(setkernvar_var[_i].path, setkernvar_var[_i].var,
		setkernvar_var[_i].val, 0);
	ck_assert_int_eq(written, setkernvar_var[_i].count);
	ck_assert_int_eq(errno, setkernvar_var[_i].errcode);
}
END_TEST

// TODO: bitmask test functions!

void library_kernutil (Suite * s) {

	TCase *tc1 = tcase_create("kernutil");
 
    tcase_add_loop_test(tc1, kernutil_getkernvar, 0, 6);
    tcase_add_loop_test(tc1, kernutil_setkernvar, 0, 5);
	tcase_add_test(tc1, kernutil_check_kernel);

    suite_add_tcase(s, tc1);

	return;
}
