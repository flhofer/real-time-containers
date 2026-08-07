/* 
###############################
# test script by Florian Hofer
# last change: 17/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "parse_configTest.h"

// tested
#include "../../src/lib/parse_config.c"

#include <unistd.h>

static prgset_t * set;
static containers_t * conts;
static FILE * pp;

static struct sched_rscs _def_rscs = {INT_MIN, NULL, -1, -1, -1, -1};
static struct sched_attr _def_attr = {SCHED_ATTR_SIZE, SCHED_NODATA, 0, 0, 0, 0, 0, 0};


static char * files [10] = {
	"",			// empty
	"\n",		// CR
	"\0",		// null string

	"{}",		// empty declaration
	"{\n \"containers\" : NULL\n }",	// container section should be present, but empty
	"{\n \"containers\" : []\n }",
	"{\n\"global\" : {\n	\"lock_pages\" : true,\n	\"setdflag\" : true,\n	\"interval\" : 6000,\n	},\n \"containers\" : [] }",

	"{\n \"containers\" : [{\n }]\n }",
	"{\n \"containers\" : [{\n \"contid\" : NULL }]\n }", 
	"{\n \"containers\" : [{\n \"contid\" : \"\" }]\n }" 
	};

static void parse_config_tc1_startup() {
	set = calloc(sizeof(prgset_t), 1);
	conts = calloc(sizeof(containers_t), 1);
	pp = NULL;
}

static void parse_config_tc1_teardown() {
	if (pp)
		pclose(pp);
	freePrgSet(set);
	freeContParm(conts);
}

static void parse_config_json(const char * text) {
	struct json_object * root = json_tokener_parse(text);
	ck_assert_ptr_ne(root, NULL);
	parse_config(root, set, conts);
	ck_assert_int_eq(json_object_put(root), 1);
}

static pidc_t * find_pid(const char * signature) {
	for (pidc_t * pid = conts->pids; pid; pid = pid->next)
		if (pid->psig && !strcmp(pid->psig, signature))
			return pid;
	return NULL;
}

static cont_t * find_container(const char * identifier) {
	for (cont_t * container = conts->cont; container; container = container->next)
		if (container->contid && !strcmp(container->contid, identifier))
			return container;
	return NULL;
}

START_TEST(parse_config_err_conf)
{	
	char buf[200] = "echo '";
	pp = popen (strcat(strcat(buf, files[_i]), "'"), "r");
	parse_config_pipe(pp, set, conts);
}
END_TEST

static void checkConfigDefault(containers_t * conts) {

	ck_assert(conts);
	ck_assert(!conts->img);
	ck_assert(!conts->pids);
	ck_assert(conts->rscs);
	ck_assert(conts->attr);

	{	// clear unallocated part of memory 'template'
		int32_t * a = (void *)conts->rscs + sizeof(conts->rscs->affinity);
		*a = 0;
	}

	ck_assert(!memcmp(conts->rscs, &_def_rscs, sizeof(struct sched_rscs)));
	ck_assert(!memcmp(conts->attr, &_def_attr, sizeof(struct sched_attr)));

}

START_TEST(parse_config_def_config)
{	
	char buf[200] = "echo '";
	pp = popen (strcat(strcat(buf, files[_i]), "'"), "r");
	parse_config_pipe(pp, set, conts);

	checkConfigDefault(conts);
	ck_assert(!conts->cont);
}
END_TEST

START_TEST(parse_config_def_config2)
{	
	char buf[200] = "echo '";
	pp = popen (strcat(strcat(buf, files[_i]), "'"), "r");
	parse_config_pipe(pp, set, conts);

	checkConfigDefault(conts);
	ck_assert(conts->cont);
}
END_TEST

START_TEST(parse_config_tst1)
{	
	pp = popen ("echo '{\n \"images\" : [{\n }]\n} '", "r");
	parse_config_pipe(pp, set, conts);

	ck_assert(conts->img);
}
END_TEST

START_TEST(parse_config_tst2)
{	
	pp = popen ("echo '{\n \"images\" : [{\n \"imgid\" : \"123121312\" }]\n} '", "r");
	parse_config_pipe(pp, set, conts);

	ck_assert(!conts->pids);
	ck_assert(!conts->cont);
	ck_assert(conts->img);
	ck_assert(!conts->img->next);
	ck_assert_str_eq(conts->img->imgid, "123121312");
}
END_TEST

START_TEST(parse_config_tst3)
{	
	pp = popen ("echo '{\n \"pids\" : [{\n \"cmd\" : \"psp\" }]\n} '", "r");
	parse_config_pipe(pp, set, conts);

	ck_assert(conts->pids);
	ck_assert(!conts->cont);
	ck_assert(!conts->img);
	ck_assert(!conts->pids->next);
	ck_assert_str_eq(conts->pids->psig, "psp");
}
END_TEST

/// TEST CASE -> initialize all program defaults
/// EXPECTED -> scalar defaults and optional pointers match their documented values
START_TEST(parse_config_defaults)
{
	memset(set, 0xA5, sizeof(*set));
	parse_config_set_default(set);

	ck_assert_ptr_eq(set->logdir, NULL);
	ck_assert_ptr_eq(set->logbasename, NULL);
	ck_assert_ptr_eq(set->cont_ppidc, NULL);
	ck_assert_ptr_eq(set->cont_pidc, NULL);
	ck_assert_ptr_eq(set->cont_cgrp, NULL);
	ck_assert_ptr_eq(set->procfileprefix, NULL);
	ck_assert_ptr_eq(set->cgroupfileprefix, NULL);
	ck_assert_ptr_eq(set->cpusystemfileprefix, NULL);
	ck_assert_ptr_eq(set->cpusetdfileprefix, NULL);
	ck_assert_ptr_eq(set->affinity, NULL);
	ck_assert_ptr_eq(set->affinity_mask, NULL);
	ck_assert_ptr_eq(set->numa, NULL);
	ck_assert_int_eq(set->priority, 0);
	ck_assert_int_eq(set->clocksel, 0);
	ck_assert_uint_eq(set->policy, SCHED_OTHER);
	ck_assert_int_eq(set->interval, TSCAN);
	ck_assert_int_eq(set->update_wcet, TWCET);
	ck_assert_int_eq(set->loops, TDETM);
	ck_assert_int_eq(set->kernelversion, KV_NOT_SUPPORTED);
	ck_assert_int_eq(set->setaffinity, AFFINITY_UNSPECIFIED);
	ck_assert_int_eq(set->use_cgroup, DM_CGRP);
	ck_assert_int_eq(set->sched_mode, SM_STATIC);
	ck_assert_double_eq_tol(set->ptresh, 0.9, 0.000001);
	ck_assert_int_eq(set->quiet | set->setdflag | set->runtime |
		set->psigscan | set->trackpids | set->dryrun | set->blindrun |
		set->lock_pages | set->force | set->smi | set->rrtime |
		set->ftrace, 0);
}
END_TEST

/// TEST CASE -> compose the Docker cpuset path from default and custom prefixes
/// EXPECTED -> controller, cgroup prefix and container group are joined in order
START_TEST(parse_config_dockerprefix)
{
	set->cont_cgrp = strdup("group/");
	parse_dockerfileprefix(set);
	char expected[128];
	snprintf(expected, sizeof(expected), "/sys/fs/cgroup/%sgroup/", CGRP_CSET);
	ck_assert_str_eq(set->cgroupfileprefix, "/sys/fs/cgroup/");
	ck_assert_str_eq(set->cpusetdfileprefix, expected);

	free(set->cgroupfileprefix);
	free(set->cpusetdfileprefix);
	set->cgroupfileprefix = strdup("/custom/cgroup/");
	set->cpusetdfileprefix = NULL;
	parse_dockerfileprefix(set);
	snprintf(expected, sizeof(expected), "/custom/cgroup/%sgroup/", CGRP_CSET);
	ck_assert_str_eq(set->cpusetdfileprefix, expected);
}
END_TEST

/// TEST CASE -> parse every global configuration value
/// EXPECTED -> strings, switches, numbers and affinity mode are stored verbatim
START_TEST(parse_config_global)
{
	parse_config_json(
		"{\"global\":{"
		"\"logdir\":\"/logs/\",\"log_basename\":\"runtime.log\","
		"\"prc_kernel\":\"/kernel/\",\"sys_cgroup\":\"/cgroup/\","
		"\"sys_cpu\":\"/cpu/\",\"cont_ppidc\":\"parent\","
		"\"cont_pidc\":\"init\",\"cont_cgrp\":\"containers/\","
		"\"priority\":17,\"clock\":2,\"default_policy\":\"fifo\","
		"\"quiet\":true,\"setdflag\":true,\"interval\":1234,"
		"\"dl_wcet\":55,\"loops\":7,\"runtime\":90,"
		"\"psigscan\":true,\"trackpids\":true,\"lock_pages\":true,"
		"\"smi\":true,\"rrtime\":42,\"setaffinity\":\"user-specified\","
		"\"affinity\":\"2-3\",\"numa\":\"1\",\"ftrace\":true,"
		"\"ptresh\":0.975}}"
	);

	ck_assert_str_eq(set->logdir, "/logs/");
	ck_assert_str_eq(set->logbasename, "runtime.log");
	ck_assert_str_eq(set->procfileprefix, "/kernel/");
	ck_assert_str_eq(set->cgroupfileprefix, "/cgroup/");
	ck_assert_str_eq(set->cpusystemfileprefix, "/cpu/");
	ck_assert_str_eq(set->cont_ppidc, "parent");
	ck_assert_str_eq(set->cont_pidc, "init");
	ck_assert_str_eq(set->cont_cgrp, "containers/");
	char expected[128];
	snprintf(expected, sizeof(expected), "/cgroup/%scontainers/", CGRP_CSET);
	ck_assert_str_eq(set->cpusetdfileprefix, expected);
	ck_assert_int_eq(set->priority, 17);
	ck_assert_int_eq(set->clocksel, 2);
	ck_assert_uint_eq(set->policy, SCHED_FIFO);
	ck_assert_int_eq(set->quiet, 1);
	ck_assert_int_eq(set->setdflag, 1);
	ck_assert_int_eq(set->interval, 1234);
	ck_assert_int_eq(set->update_wcet, 55);
	ck_assert_int_eq(set->loops, 7);
	ck_assert_int_eq(set->runtime, 90);
	ck_assert_int_eq(set->psigscan, 1);
	ck_assert_int_eq(set->trackpids, 1);
	ck_assert_int_eq(set->lock_pages, 1);
	ck_assert_int_eq(set->smi, 1);
	ck_assert_int_eq(set->rrtime, 42);
	ck_assert_int_eq(set->setaffinity, AFFINITY_USERSPECIFIED);
	ck_assert_str_eq(set->affinity, "2-3");
	ck_assert_str_eq(set->numa, "1");
	ck_assert_int_eq(set->ftrace, 1);
	ck_assert_double_eq_tol(set->ptresh, 0.975, 0.000001);
}
END_TEST

/// TEST CASE -> parse an affinity mask without an explicit affinity mode
/// EXPECTED -> the mode is promoted to user-specified
START_TEST(parse_config_affinity_fallback)
{
	parse_config_json("{\"global\":{\"affinity\":\"1,3\"}}");
	ck_assert_int_eq(set->setaffinity, AFFINITY_USERSPECIFIED);
	ck_assert_str_eq(set->affinity, "1,3");
}
END_TEST

/// TEST CASE -> preserve values supplied before configuration parsing
/// EXPECTED -> pointer settings, scheduler policy and affinity selection keep CLI values
START_TEST(parse_config_precedence)
{
	set->logdir = strdup("cli-log");
	set->logbasename = strdup("cli-name");
	set->cont_ppidc = strdup("cli-parent");
	set->cont_pidc = strdup("cli-init");
	set->cont_cgrp = strdup("cli-group/");
	set->procfileprefix = strdup("cli-proc");
	set->cpusystemfileprefix = strdup("cli-cpu");
	set->policy = SCHED_RR;
	set->setaffinity = AFFINITY_USEALL;
	set->affinity = strdup("cli-affinity");

	parse_config_json(
		"{\"global\":{\"logdir\":\"file-log\","
		"\"log_basename\":\"file-name\",\"cont_ppidc\":\"file-parent\","
		"\"cont_pidc\":\"file-init\",\"cont_cgrp\":\"file-group/\","
		"\"prc_kernel\":\"file-proc\",\"sys_cpu\":\"file-cpu\","
		"\"default_policy\":\"deadline\",\"setaffinity\":\"numa-balanced\","
		"\"affinity\":\"file-affinity\"}}"
	);

	ck_assert_str_eq(set->logdir, "cli-log");
	ck_assert_str_eq(set->logbasename, "cli-name");
	ck_assert_str_eq(set->cont_ppidc, "cli-parent");
	ck_assert_str_eq(set->cont_pidc, "cli-init");
	ck_assert_str_eq(set->cont_cgrp, "cli-group/");
	ck_assert_str_eq(set->procfileprefix, "cli-proc");
	ck_assert_str_eq(set->cpusystemfileprefix, "cli-cpu");
	ck_assert_uint_eq(set->policy, SCHED_RR);
	ck_assert_int_eq(set->setaffinity, AFFINITY_USEALL);
	ck_assert_str_eq(set->affinity, "cli-affinity");
}
END_TEST

/// TEST CASE -> parse explicit global scheduling and resource limits
/// EXPECTED -> scheduler fallback fields and all resource values are populated
START_TEST(parse_config_resources)
{
	parse_config_json(
		"{\"scheduling\":{\"policy\":\"deadline\",\"flags\":5,"
		"\"nice\":-3,\"prio\":8,\"runtime\":1000},"
		"\"resources\":{\"affinity\":4,\"rt-soft\":10,\"rt-hard\":20,"
		"\"data-soft\":30,\"data-hard\":40}}"
	);

	ck_assert_uint_eq(conts->attr->sched_policy, SCHED_DEADLINE);
	ck_assert_uint_eq(conts->attr->sched_flags, 5);
	ck_assert_int_eq(conts->attr->sched_nice, -3);
	ck_assert_uint_eq(conts->attr->sched_priority, 8);
	ck_assert_uint_eq(conts->attr->sched_runtime, 1000);
	ck_assert_uint_eq(conts->attr->sched_deadline, 1000);
	ck_assert_uint_eq(conts->attr->sched_period, 1000);
	ck_assert_int_eq(conts->rscs->affinity, 4);
	ck_assert_ptr_eq(conts->rscs->affinity_mask, NULL);
	ck_assert_int_eq(conts->rscs->rt_timew, 10);
	ck_assert_int_eq(conts->rscs->rt_time, 20);
	ck_assert_int_eq(conts->rscs->mem_dataw, 30);
	ck_assert_int_eq(conts->rscs->mem_data, 40);
}
END_TEST

/// TEST CASE -> parse nested image, container and PID configurations
/// EXPECTED -> counts, ownership links and inherited resource pointers are consistent
START_TEST(parse_config_hierarchy)
{
	parse_config_json(
		"{\"scheduling\":{\"policy\":\"other\",\"runtime\":10,"
		"\"deadline\":20,\"period\":30},\"resources\":{\"affinity\":0},"
		"\"images\":[{\"imgid\":\"image\","
		"\"params\":{\"policy\":\"rr\",\"runtime\":100},"
		"\"res\":{\"affinity\":1,\"rt-hard\":70},"
		"\"pids\":[{\"cmd\":\"image-task\"}],"
		"\"cont\":[{\"contid\":\"image-container\","
		"\"pids\":[{\"cmd\":\"image-container-task\"}]}]}],"
		"\"containers\":[{\"contid\":\"root-container\","
		"\"params\":{\"policy\":\"fifo\",\"prio\":20},"
		"\"res\":{\"affinity\":2},\"pids\":[{\"cmd\":\"root-task\","
		"\"params\":{\"policy\":\"fifo\",\"prio\":30}}]}],"
		"\"pids\":[{\"cmd\":\"global-task\"}]}"
	);

	ck_assert_uint_eq(conts->nthreads, 3);
	ck_assert_uint_eq(conts->num_cont, 3);
	ck_assert_ptr_ne(conts->img, NULL);
	ck_assert_str_eq(conts->img->imgid, "image");
	ck_assert_uint_eq(conts->img->attr->sched_policy, SCHED_RR);
	ck_assert_uint_eq(conts->img->attr->sched_runtime, 100);
	ck_assert_uint_eq(conts->img->attr->sched_deadline, 100);
	ck_assert_uint_eq(conts->img->attr->sched_period, 100);
	ck_assert_int_eq(conts->img->rscs->affinity, 1);

	cont_t * image_container = find_container("image-container");
	cont_t * root_container = find_container("root-container");
	pidc_t * image_pid = find_pid("image-task");
	pidc_t * image_container_pid = find_pid("image-container-task");
	pidc_t * root_pid = find_pid("root-task");
	pidc_t * global_pid = find_pid("global-task");
	ck_assert_ptr_ne(image_container, NULL);
	ck_assert_ptr_ne(root_container, NULL);
	ck_assert_ptr_ne(image_pid, NULL);
	ck_assert_ptr_ne(image_container_pid, NULL);
	ck_assert_ptr_ne(root_pid, NULL);
	ck_assert_ptr_ne(global_pid, NULL);

	ck_assert_ptr_eq(image_container->img, conts->img);
	ck_assert_ptr_eq(image_container->attr, conts->img->attr);
	ck_assert_ptr_eq(image_container->rscs, conts->img->rscs);
	ck_assert_int_eq(image_container->status & (MSK_STATSHAT | MSK_STATSHRC),
		MSK_STATSHAT | MSK_STATSHRC);
	ck_assert_ptr_eq(image_pid->img, conts->img);
	ck_assert_ptr_eq(image_pid->attr, conts->img->attr);
	ck_assert_ptr_eq(image_pid->rscs, conts->img->rscs);
	ck_assert_ptr_eq(image_container_pid->cont, image_container);
	ck_assert_ptr_eq(image_container_pid->img, conts->img);
	ck_assert_ptr_eq(image_container_pid->attr, image_container->attr);
	ck_assert_ptr_eq(root_pid->cont, root_container);
	ck_assert_ptr_ne(root_pid->attr, root_container->attr);
	ck_assert_ptr_eq(root_pid->rscs, root_container->rscs);
	ck_assert_int_eq(root_pid->status & MSK_STATSHAT, 0);
	ck_assert_int_eq(root_pid->status & MSK_STATSHRC, MSK_STATSHRC);
	ck_assert_ptr_eq(global_pid->attr, conts->attr);
	ck_assert_ptr_eq(global_pid->rscs, conts->rscs);
	ck_assert_ptr_eq(global_pid->cont, NULL);
	ck_assert_ptr_eq(global_pid->img, NULL);
}
END_TEST

static const char * invalid_config[] = {
	"{\"containers\":{}}",
	"{\"images\":{}}",
	"{\"scheduling\":{\"policy\":\"invalid\"}}",
	"{\"global\":{\"default_policy\":\"invalid\"}}",
};

/// TEST CASE -> reject invalid section types and scheduler names
/// EXPECTED -> parser exits with the configuration error code
START_TEST(parse_config_invalid_values)
{
	parse_config_json(invalid_config[_i]);
}
END_TEST

/// TEST CASE -> load configuration through the file wrapper
/// EXPECTED -> file contents are parsed into the supplied structures
START_TEST(parse_config_from_file)
{
	char path[] = "/tmp/parse-config-XXXXXX";
	int fd = mkstemp(path);
	ck_assert_int_ge(fd, 0);
	const char data[] = "{\"pids\":[{\"cmd\":\"file-task\"}]}";
	ck_assert_int_eq(write(fd, data, sizeof(data) - 1), sizeof(data) - 1);
	ck_assert_int_eq(close(fd), 0);

	parse_config_file(path, set, conts);
	ck_assert_ptr_ne(find_pid("file-task"), NULL);
	ck_assert_int_eq(unlink(path), 0);
}
END_TEST

/// TEST CASE -> reject an unavailable configuration file
/// EXPECTED -> parser exits with the configuration error code
START_TEST(parse_config_missing_file)
{
	parse_config_file("/tmp/parse-config-file-does-not-exist", set, conts);
}
END_TEST

/// TEST CASE -> load configuration through the stdin wrapper
/// EXPECTED -> redirected stdin contents are parsed normally
START_TEST(parse_config_from_stdin)
{
	int stdin_copy = dup(STDIN_FILENO);
	FILE * input = tmpfile();
	ck_assert_int_ge(stdin_copy, 0);
	ck_assert_ptr_ne(input, NULL);
	ck_assert_int_gt(fputs("{\"pids\":[{\"cmd\":\"stdin-task\"}]}", input), 0);
	rewind(input);
	ck_assert_int_ge(dup2(fileno(input), STDIN_FILENO), 0);
	clearerr(stdin);

	parse_config_stdin(set, conts);
	ck_assert_ptr_ne(find_pid("stdin-task"), NULL);

	ck_assert_int_ge(dup2(stdin_copy, STDIN_FILENO), 0);
	close(stdin_copy);
	fclose(input);
}
END_TEST

void library_parse_config (Suite * s) {

	TCase *tc1 = tcase_create("parse_config_reading");
	tcase_add_checked_fixture(tc1, parse_config_tc1_startup, parse_config_tc1_teardown);
	tcase_add_test(tc1, parse_config_from_file);
	tcase_add_exit_test(tc1, parse_config_missing_file, EXIT_INV_CONFIG);
	tcase_add_test(tc1, parse_config_from_stdin);

	suite_add_tcase(s, tc1);

	TCase *tc2 = tcase_create("parse_config_blocks");
	tcase_add_checked_fixture(tc2, parse_config_tc1_startup, parse_config_tc1_teardown);
	tcase_add_test(tc2, parse_config_tst1);
	tcase_add_test(tc2, parse_config_tst2);
	tcase_add_test(tc2, parse_config_tst3);
	tcase_add_loop_exit_test(tc2, parse_config_invalid_values,
		EXIT_INV_CONFIG, 0, sizeof(invalid_config) / sizeof(invalid_config[0]));
	tcase_add_test(tc2, parse_config_defaults);
	tcase_add_test(tc2, parse_config_global);
	tcase_add_test(tc2, parse_config_resources);

	TCase *tc3 = tcase_create("parse_config_def");
	tcase_add_checked_fixture(tc3, parse_config_tc1_startup, parse_config_tc1_teardown);
	tcase_add_loop_exit_test(tc3, parse_config_err_conf, EXIT_INV_CONFIG, 0, 3);
	tcase_add_loop_test(tc3, parse_config_def_config, 3, 7);
	tcase_add_loop_test(tc3, parse_config_def_config2, 7, 10);

    suite_add_tcase(s, tc3);

	TCase *tc4 = tcase_create("parse_config_extras");
	tcase_add_checked_fixture(tc4, parse_config_tc1_startup, parse_config_tc1_teardown);	
	tcase_add_test(tc4, parse_config_dockerprefix);
	tcase_add_test(tc4, parse_config_affinity_fallback);
	tcase_add_test(tc4, parse_config_precedence);
	tcase_add_test(tc4, parse_config_hierarchy);

	suite_add_tcase(s, tc4);

	return;
}
