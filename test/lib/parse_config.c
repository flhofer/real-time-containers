/* 
###############################
# test script by Florian Hofer
# last change: 17/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/include/parse_config.h"
#include <check.h>

//TODO: all tests!!!

static prgset_t * set;
static containers_t * conts;
static FILE * pp;

static struct sched_rscs _def_rscs = {-1, -1, -1, -1, -1};
static struct sched_attr _def_attr = {48, 0, 0, 0, 0, 0, 0, 0}; // sched_other == 0 


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
	set = malloc(sizeof(prgset_t));
	conts = malloc(sizeof(containers_t));
	conts->img = NULL; // locals are not initialized
	conts->pids = NULL;
	conts->cont = NULL;
	conts->nthreads = 0;
	conts->num_cont = 0;
}

static void parse_config_tc1_teardown() {
	pclose(pp);
	free(set);
	free(conts->rscs);
	free(conts->attr);
	free(conts);
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

	free(conts->cont);
}
END_TEST

START_TEST(parse_config_tst1)
{	
	pp = popen ("echo '{\n \"images\" : [{\n }]\n} '", "r");
	parse_config_pipe(pp, set, conts);

	ck_assert(conts->img);
	free(conts->img);
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
	free(conts->img);
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
	free(conts->pids);
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

	suite_add_tcase(s, tc2);

	return;
}
