/* 
###############################
# test script by Florian Hofer
# last change: 17/07/2019
# ©2019 all rights reserved ☺
###############################
*/

//TODO: all tests!!!

#include "../../src/schedstat/schedstat.h"
#include "../../src/schedstat/update.h"
#include "../../src/include/orchdata.h"
#include "../../src/include/parse_config.h"
#include "../../src/include/kernutil.h"
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update

/// TEST CASE -> push node elements and test
/// EXPECTED -> 
START_TEST(orchdata_ndpush)
{

}
END_TEST

/// TEST CASE -> pop node elements and test
/// EXPECTED -> should free without issues 
START_TEST(orchdata_ndpop)
{
	static const char *a[] = {NULL, "", ""};
	static const char *b[] = {"", NULL, ""};
	static const char *c[] = {"", "", NULL};

	node_push(&head);
	head->psig = a == NULL ? strdup(a[_i]) : NULL;
	head->contid = b == NULL ? strdup(b[_i]) : NULL;
	head->imgid = c == NULL ? strdup(c[_i]) : NULL;
	node_pop(&head);

	ck_assert(!head);
}
END_TEST

START_TEST(orchdata_ndpop2)
{
	char *a = strdup("aa");
	char *b = strdup("bb");
	char *c = strdup("cc");

	cont_t d = {NULL, b};
	img_t e = {NULL, c};
	pidc_t f = {NULL, a, NULL, NULL, &d, &e};
	
	node_push(&head);
	head->psig= a;
	head->contid = b;
	head->imgid = c;
	head->param = &f;
	node_pop(&head);

	ck_assert(!head);

	ck_assert(a);
	ck_assert(b);
	ck_assert(c);

	free(a);
	free(b);
	free(c);
}
END_TEST

START_TEST(orchdata_ndpop3)
{
	char *a = strdup("aa");
	char *b = strdup("bb");
	char *c = strdup("cc");

	cont_t d = {NULL, "sss"};
	img_t e = {NULL, "xxx"};
	pidc_t f = {NULL, a, NULL, NULL, &d, &e};
	
	node_push(&head);
	head->psig= a;
	head->contid = b;
	head->imgid = c;
	head->param = &f;
	node_pop(&head);

	ck_assert(!head);

	ck_assert(!a);
	ck_assert(!b);
	ck_assert(!c);
}
END_TEST

/// Static setup for all tests in this batch
static void orchdata_setup() {
	contparm = malloc (sizeof(containers_t));
	contparm->img = NULL; // locals are not initialized
	contparm->pids = NULL;
	contparm->cont = NULL;
	contparm->nthreads = 0;
	contparm->num_cont = 0;

	contparm->rscs = malloc(sizeof(struct sched_rscs)); // allocate dummy structures
	contparm->attr = malloc(sizeof(struct sched_attr)); // allocate 
}

static void orchdata_teardown() {
	free(contparm->rscs);
	free(contparm->attr);
	free(contparm);
}

/// TEST CASE -> test element push, add contianer push, pop all
/// EXPECTED -> adding different pid and container stuff, imd is identical
START_TEST(orchdata_pidcont)
{	
	// container
	push((void**)&contparm->cont, sizeof(cont_t));
	cont_t * cont = contparm->cont;

	// new pid
	push((void**)&contparm->pids, sizeof(pidc_t));
	push((void**)&cont->pids, sizeof(pids_t));
	cont->pids->pid = contparm->pids; // add new empty item -> pid list, container pids list

	// test link container and pid
	ck_assert(contparm->pids);
	ck_assert(!contparm->pids->next);
	ck_assert(contparm->cont);
	ck_assert(!contparm->cont->next);
	ck_assert(contparm->cont->pids);
	ck_assert(!contparm->cont->pids->next);

	ck_assert_ptr_eq(contparm->cont->pids->pid,contparm->pids);
	
	// one more pid
	push((void**)&contparm->pids, sizeof(pidc_t));
	push((void**)&cont->pids, sizeof(pids_t));
	cont->pids->pid = contparm->pids; // add new empty item -> pid list, container pids list

	// test list push
	ck_assert(contparm->pids);
	ck_assert(contparm->pids->next);
	ck_assert(!contparm->pids->next->next);
	ck_assert(contparm->cont);
	ck_assert(!contparm->cont->next);
	ck_assert(contparm->cont->pids);
	ck_assert(contparm->cont->pids->next);
	ck_assert(!contparm->cont->pids->next->next);

	ck_assert_ptr_eq(contparm->cont->pids->pid,contparm->pids);
	ck_assert_ptr_eq(contparm->cont->pids->next->pid,contparm->pids->next);
	
	// add one more container
	push((void**)&contparm->cont, sizeof(cont_t));
	cont = contparm->cont;

	// verify stuff has moved
	ck_assert(contparm->cont->next);
	ck_assert(!contparm->cont->next->next);
	ck_assert(!contparm->cont->pids);
	ck_assert(contparm->cont->next->pids);

	// pop elements, verfy empty
	ck_assert(contparm->cont);
	pop((void**)&contparm->cont);
	ck_assert(contparm->cont);
	ck_assert(contparm->cont->pids);
	pop((void**)&contparm->cont->pids);
	ck_assert(contparm->cont->pids);
	pop((void**)&contparm->cont->pids);
	ck_assert(!contparm->cont->pids);
	pop((void**)&contparm->cont);
	ck_assert(!contparm->cont);

	// pop pids
	ck_assert(contparm->pids);
	pop((void**)&contparm->pids);
	pop((void**)&contparm->pids);
	ck_assert(!contparm->pids);
}
END_TEST

static void orchdata_tc2_setup () {
	// container
	push((void**)&contparm->cont, sizeof(cont_t));
	cont_t * cont = contparm->cont;
	cont->contid= strdup("a2aa8c37ce4ca2aa8c37ce4c");

	{
		const char *pids[] = {	"sleep",
								"sleep 1",
								"sleep 5",
								NULL };

		const char ** pidsig = pids;
		while (*pidsig) {
			// new pid
			push((void**)&contparm->pids, sizeof(pidc_t));
			push((void**)&cont->pids, sizeof(pids_t));
			cont->pids->pid = contparm->pids; // add new empty item -> pid list, container pids list
			contparm->pids->psig = strdup(*pidsig);
			pidsig++;
		}
	}
	
	{	
		// add one more container
		push((void**)&contparm->cont, sizeof(cont_t));
		cont = contparm->cont;
		cont->contid=strdup("d7408531a3b4d7408531a3b4");

		const char *pids[] = {	"sleep 4",
								NULL };

		const char ** pidsig = pids;
		while (*pidsig) {
			// new pid
			push((void**)&contparm->pids, sizeof(pidc_t));
			push((void**)&cont->pids, sizeof(pids_t));
			cont->pids->pid = contparm->pids; // add new empty item -> pid list, container pids list
			contparm->pids->psig = strdup(*pidsig);
			pidsig++;
		}
	}

}

static void orchdata_tc2_teardown () {

	while (contparm->cont){
		while (contparm->cont->pids)
			pop((void**)&contparm->cont->pids);

		free (contparm->cont->contid);
		pop((void**)&contparm->cont);
	}

	while (contparm->pids) {
		free(contparm->pids->psig);
		pop((void **)&contparm->pids);
	}

	node_pop(&head);
}

/// TEST CASE -> test configuration find 
/// EXPECTED -> 
START_TEST(orchdata_findparams)
{	
	node_push(&head);
	head->psig = strdup("sleep 5");
	int retv = node_findParams(head , contparm);
	
	ck_assert_int_eq(retv, 0);
	ck_assert(head->param);
}
END_TEST

void library_orchdata (Suite * s) {

	TCase *tc0 = tcase_create("orchdata_memory");
	tcase_add_test(tc0, orchdata_ndpush);
	tcase_add_test(tc0, orchdata_ndpop);
	tcase_add_test(tc0, orchdata_ndpop2);
	tcase_add_test(tc0, orchdata_ndpop3);
    suite_add_tcase(s, tc0);


	TCase *tc1 = tcase_create("orchdata_pidcont");
	tcase_add_unchecked_fixture(tc1, orchdata_setup, orchdata_teardown);
	tcase_add_test(tc1, orchdata_pidcont);
    suite_add_tcase(s, tc1);

	TCase *tc2 = tcase_create("orchdata_findparams");
	tcase_add_unchecked_fixture(tc2, orchdata_setup, orchdata_teardown);
	tcase_add_checked_fixture(tc2, orchdata_tc2_setup, orchdata_tc2_teardown);
	tcase_add_test(tc2, orchdata_findparams);
    suite_add_tcase(s, tc2);

	return;
}
