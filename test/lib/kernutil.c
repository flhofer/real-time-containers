/* 
###############################
# test script by Florian Hofer
# last change: 08/06/2020
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/include/kernutil.h"

START_TEST(kernutil_check_kernel)
{	
	char buf[256];
	ck_assert_int_gt(getkernvar("/proc/", "version", buf, sizeof(buf)), 0);

	int maj, min,rev;
	ck_assert_int_eq(sscanf(buf, "%*s %*s %d.%d.%d-%*d-%*s %*s", &maj, &min, &rev), 3);

	int kv = KV_NOT_SUPPORTED;
	if (3 == maj && 14 <=  min)
		kv = KV_314;
	else if (4 == maj) {
		if (min >= 16)
			kv = KV_416;
		else if (min >= 13)
			kv = KV_413;
		else
			kv = KV_40;
	} else if (5 == maj)
		kv = KV_50;

    ck_assert_int_eq(check_kernel(), kv);
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
		{"/proc/", "version", "Linux", 0, 0},	// standard read - len = 0, changes by kernel version
		{"/proc/","meminfo", "MemTotal", 50, 0},			// buffer too small
		{"/proc/","noexist", "", -1, ENOENT},				// entry does not exist
		{"/sys/devices/system/cpu/", "isolated","\0", 1, 0},// empty entry
		{"/sys/devices/system/cpu/sys/devices/system/cpu/sys/devices/system/cpu/sys/devices/system/cpu/sys/devices/system/cpu/sys/",
			"devices1", "", -1, ENOMEM},						// too long filename+varname > 128
		{"/sys/devices/system/cpu/isolated/",
			"thread_siblings_list", "", -1, ENOTDIR}			// not a valid dir string
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
		{"/proc/","version", "test", -1, EIO},				// write protected
		{"/dev/", "null", "", 0, 0},						// write empty -> special case
		{"/dev/", "null", NULL, -1, EINVAL},					// write NULL
		{"/dev/", NULL, "", -1, EINVAL},						// write to NULL
	}; 

START_TEST(kernutil_setkernvar)
{
	int written = setkernvar(setkernvar_var[_i].path, setkernvar_var[_i].var,
		setkernvar_var[_i].val, 0);
	ck_assert_int_eq(written, setkernvar_var[_i].count);
	ck_assert_int_eq(errno, setkernvar_var[_i].errcode);
}
END_TEST


START_TEST(kernutil_parse_bitmask)
{
	struct bitmask * test = NULL;
	int ret;

	char str[18];
	ret = parse_bitmask(test, str, sizeof(str));
	ck_assert_int_eq(ret, -1);

	test = numa_bitmask_alloc(40);

	ret = parse_bitmask(test, NULL, sizeof(str));
	ck_assert_int_eq(ret, -1);

	ret = parse_bitmask(test, str, 0);
	ck_assert_int_eq(ret, -1);


	numa_bitmask_setbit(test,0);
	numa_bitmask_setbit(test,1);
	numa_bitmask_setbit(test,33);

	ret = parse_bitmask(test, str, sizeof(str));

	ck_assert_int_eq(ret, 0);
	ck_assert_str_eq(str, "0-1,33");

	numa_bitmask_free(test);
}
END_TEST

START_TEST(kernutil_parse_bitmask_hex)
{
	struct bitmask * test = NULL;
	int ret;

	{
		char str[18];
		ret = parse_bitmask_hex(test, str, sizeof(str));
		ck_assert_int_eq(ret, -1);

		test = numa_bitmask_alloc(40);

		ret = parse_bitmask_hex(test, NULL, sizeof(str));
		ck_assert_int_eq(ret, -1);

		ret = parse_bitmask_hex(test, str, 0);
		ck_assert_int_eq(ret, -1);


		numa_bitmask_setbit(test,0);
		numa_bitmask_setbit(test,1);
		numa_bitmask_setbit(test,33);

		ret = parse_bitmask_hex(test, str, sizeof(str));

		ck_assert_int_eq(ret, 0);
		ck_assert_str_eq(str, "0000000200000003");
	}

	numa_bitmask_free(test);

	test = numa_bitmask_alloc(67);

	{
		char str[33];
		numa_bitmask_setbit(test,0);
		numa_bitmask_setbit(test,1);
		numa_bitmask_setbit(test,32);
		numa_bitmask_setbit(test,66);

		ret = parse_bitmask_hex(test, str, sizeof(str));

		ck_assert_int_eq(ret, 0);
		ck_assert_str_eq(str, "00000000000000040000000100000003");
	}
	numa_bitmask_free(test);
}
END_TEST

void library_kernutil (Suite * s) {

	TCase *tc1 = tcase_create("kernutil");
 
    tcase_add_loop_test(tc1, kernutil_getkernvar, 0, 6);
    tcase_add_loop_test(tc1, kernutil_setkernvar, 0, 5);
	tcase_add_test(tc1, kernutil_check_kernel);
	tcase_add_test(tc1, kernutil_parse_bitmask);
	tcase_add_test(tc1, kernutil_parse_bitmask_hex);

    suite_add_tcase(s, tc1);

	return;
}
