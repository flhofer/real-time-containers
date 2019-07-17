/* 
###############################
# test script by Florian Hofer
# last change: 17/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "../../src/include/parse_config.h"

//TODO: all tests!!!

START_TEST(parse_config_basic)
{	

}
END_TEST


void library_parse_config (Suite * s) {
	TCase *tc1 = tcase_create("parse_config");
 
	tcase_add_test(tc1, parse_config_basic);

    suite_add_tcase(s, tc1);

	return;
}
