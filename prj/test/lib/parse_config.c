/* 
###############################
# test script by Florian Hofer
# last change: 17/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/include/parse_config.h"

//TODO: all tests!!!

static prgset_t * set;
static containers_t * conts;
static FILE * pp;

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
}

START_TEST(parse_config_err_conf)
{	
	char buf[200] = "echo '";
	pp = popen (strcat(strcat(buf, files[_i]), "'"), "r");
	parse_config_pipe(pp, set, conts);
}
END_TEST

static void checkConfigDefault(containers_t * conts) {

	struct sched_rscs _def_rscs = {-1, -1, -1, -1, -1};
	struct sched_attr _def_attr = {48, 0, 0, 0, 0, 0, 0, 0}; // sched_other == 0 

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

}
END_TEST

static void parse_config_tc1_teardown() {
	pclose(pp);
	free(set);
	free(conts->rscs);
	free(conts->attr);
	free(conts);
}


void library_parse_config (Suite * s) {
	TCase *tc1 = tcase_create("parse_config_conf");
 
	tcase_add_checked_fixture(tc1, parse_config_tc1_startup, parse_config_tc1_teardown);
//	tcase_add_loop_exit_test(tc1, parse_config_err_conf, EXIT_INV_CONFIG, 0, 5);
	tcase_add_loop_test(tc1, parse_config_def_config, 5, 7);
	tcase_add_loop_test(tc1, parse_config_def_config2, 7, 10);

    suite_add_tcase(s, tc1);

	return;
}
