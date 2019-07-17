/* 
###############################
# test script by Florian Hofer
# last change: 17/07/2019
# ©2019 all rights reserved ☺
###############################
*/

START_TEST(orchdata_testpush)
{	

}
END_TEST


void library_orchdata (Suite * s) {
	TCase *tc1 = tcase_create("orchdata");

 
	tcase_add_test(tc1, orchdata_testpush);

    suite_add_tcase(s, tc1);

	return;
}
