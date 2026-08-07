/*
###############################
# test script by Florian Hofer
# last change: 01/12/2020
# ©2020 all rights reserved ☺
###############################
*/

#include "errorTest.h"

// tested
#include "error.h"

#include <errno.h>
#include <signal.h>
#include <unistd.h>

/// TEST CASE -> write all non-terminating message variants to stderr
/// EXPECTED -> each message has its prefix, formatted value and optional errno text
START_TEST(error_messages)
{
	char output[1024] = {0};
	int stderr_copy = dup(STDERR_FILENO);
	FILE * capture = tmpfile();

	ck_assert_int_ge(stderr_copy, 0);
	ck_assert_ptr_ne(capture, NULL);
	ck_assert_int_ge(dup2(fileno(capture), STDERR_FILENO), 0);

	err_msg("message %d", 1);
	err_msg_n(ENOENT, "missing");
	debug("message %d", 2);
	cont("message %d", 3);
	info("message %d", 4);
	warn("message %d", 5);

	ck_assert_int_eq(fseek(capture, 0, SEEK_SET), 0);
	ck_assert_int_gt(fread(output, 1, sizeof(output) - 1, capture), 0);
	ck_assert_int_ge(dup2(stderr_copy, STDERR_FILENO), 0);
	close(stderr_copy);
	fclose(capture);

	ck_assert_ptr_ne(strstr(output, "ERROR: message 1\n"), NULL);
	ck_assert_ptr_ne(strstr(output, "ERROR: missing"), NULL);
	ck_assert_ptr_ne(strstr(output, strerror(ENOENT)), NULL);
	ck_assert_ptr_ne(strstr(output, "DEBUG: message 2\n"), NULL);
	ck_assert_ptr_ne(strstr(output, "... message 3\n"), NULL);
	ck_assert_ptr_ne(strstr(output, "INFO: message 4\n"), NULL);
	ck_assert_ptr_ne(strstr(output, "WARN: message 5\n"), NULL);
}
END_TEST

/// TEST CASE -> exit through the generic fatal error helper
/// EXPECTED -> process exits with EXIT_FAILURE
START_TEST(error_exit_default)
{
	err_exit("fatal error");
}
END_TEST

/// TEST CASE -> exit through the errno-aware fatal error helper
/// EXPECTED -> process exits with the supplied error number
START_TEST(error_exit_errno)
{
	err_exit_n(EINVAL, "invalid value");
}
END_TEST

/// TEST CASE -> abort through both immediate fatal error helpers
/// EXPECTED -> process terminates with SIGABRT
START_TEST(error_abort)
{
	if (_i)
		fatal_n(EIO, "fatal error");
	else
		fatal("fatal error");
}
END_TEST


void library_error (Suite * s) {

	TCase *tc1 = tcase_create("error");
	tcase_add_test(tc1, error_messages);
	tcase_add_exit_test(tc1, error_exit_default, EXIT_FAILURE);
	tcase_add_exit_test(tc1, error_exit_errno, EINVAL);
	tcase_add_loop_test_raise_signal(tc1, error_abort, SIGABRT, 0, 2);

    suite_add_tcase(s, tc1);

	return;
}
