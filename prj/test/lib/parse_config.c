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

static char * files [5] = {
	"",			// empty
	"\n",		// CR
	"\0",		// null string
	"{}",		// empty declaration

	"{\n \"containers\" : NULL\n }",	// container section should be present, but empty
//	"{\n \"containers\" : []\n }", valid ??? TODO
//	"{\n \"containers\" : [{\n }]\n }",	valid ??? 
//	"{\n \"containers\" : [{\n \"contid\" : NULL }]\n }", valid	
//	"{\n \"containers\" : [{\n \"contid\" : \"\" }]\n }", valid

//	"{\n\"global\" : {\n	\"lock_pages\" : true,\n	\"setdflag\" : true,\n	\"interval\" : 6000,\n	},\n }"
	};

static void parse_config_tc1_startup() {
	set = malloc(sizeof(prgset_t));
	conts = malloc(sizeof(containers_t));
}

START_TEST(parse_config_err_conf)
{	
	char buf[4096] = "echo '";
	pp = popen (strcat(strcat(buf, files[_i]), "'"), "r");
	parse_config_pipe(pp, set, conts);
}
END_TEST

static void parse_config_tc1_teardown() {
	pclose(pp);
	free(set);
	free(conts);
}


void library_parse_config (Suite * s) {
	TCase *tc1 = tcase_create("parse_config_err_conf");
 
	tcase_add_checked_fixture(tc1, parse_config_tc1_startup, parse_config_tc1_teardown);
	tcase_add_loop_exit_test(tc1, parse_config_err_conf, EXIT_INV_CONFIG, 0, 5);

    suite_add_tcase(s, tc1);

	return;
}
