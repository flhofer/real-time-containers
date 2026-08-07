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

void library_parse_config (Suite * s) {
	TCase *tc1 = tcase_create("parse_config_def");

	tcase_add_checked_fixture(tc1, parse_config_tc1_startup, parse_config_tc1_teardown);
	tcase_add_loop_exit_test(tc1, parse_config_err_conf, EXIT_INV_CONFIG, 0, 3);
	tcase_add_loop_test(tc1, parse_config_def_config, 3, 7);
	tcase_add_loop_test(tc1, parse_config_def_config2, 7, 10);

    suite_add_tcase(s, tc1);

	TCase *tc2 = tcase_create("parse_config_blocks");
	tcase_add_checked_fixture(tc2, parse_config_tc1_startup, parse_config_tc1_teardown);
	tcase_add_test(tc2, parse_config_tst1);
	tcase_add_test(tc2, parse_config_tst2);
	tcase_add_test(tc2, parse_config_tst3);
	tcase_add_test(tc2, parse_config_defaults);

	suite_add_tcase(s, tc2);

	return;
}
