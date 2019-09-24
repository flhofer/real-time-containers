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
//#include <malloc.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update

/*
extern void __builtin_free(void *__ptr);

struct list {
	struct list * next;
	void * ptr;
};

struct list * lst;

void
free (void *ptr)
{
  if (ptr){
	push((void **)&lst, sizeof(struct list));
	lst->ptr = ptr;
  }
  return __builtin_free (ptr);
}
*/

/// TEST CASE -> push node elements and test
/// EXPECTED -> pushing, no matter where, should keep order, data is initialized
START_TEST(orchdata_ndpush)
{
	ck_assert(!head);
	node_push(&head);

	ck_assert_ptr_eq(head->next, NULL);
	ck_assert_int_eq(head->pid, 0);
	ck_assert_int_eq(head->det_mode, 0);
	ck_assert_ptr_eq(head->psig, NULL);
	ck_assert_ptr_eq(head->contid, NULL);
	ck_assert_ptr_eq(head->imgid, NULL);
	ck_assert_int_eq(head->attr.size, 48);
	ck_assert_int_eq(head->attr.sched_policy, SCHED_NODATA);
	ck_assert_int_eq(head->mon.rt_min, INT64_MAX);
	ck_assert_int_eq(head->mon.rt_avg, 0);
	ck_assert_int_eq(head->mon.rt_max, INT64_MIN);
	ck_assert_int_eq(head->mon.dl_count, 0);
	ck_assert_int_eq(head->mon.dl_scanfail, 0);
	ck_assert_int_eq(head->mon.dl_overrun, 0);
	ck_assert_int_eq(head->mon.dl_deadline, 0);
	ck_assert_int_eq(head->mon.dl_rt, 0);
	ck_assert_int_eq(head->mon.dl_diff, 0);
	ck_assert_int_eq(head->mon.dl_diffmin, INT64_MAX);
	ck_assert_int_eq(head->mon.dl_diffavg, 0);
	ck_assert_int_eq(head->mon.dl_diffmax, INT64_MIN);
	ck_assert_ptr_eq(head->param, NULL);

	// verify default settings

	node_t * a = head;
	node_push(&head);
	node_t * b = head;
	node_push(&head);
	node_t * c = head;

	// head |-> c -> b -> a
	ck_assert_ptr_eq(head,c);
	ck_assert_ptr_eq(head->next,b);
	ck_assert_ptr_eq(head->next->next,a);

	// head |-> c -> a
	node_pop(&c->next); // drop after C
	
	ck_assert_ptr_eq(head,c);
	ck_assert_ptr_eq(head->next,a);

	node_push(&head);
	node_push(&c->next);

	// head |->x -> c -> y -> a
	ck_assert_ptr_eq(head->next,c);
	ck_assert_ptr_eq(head->next->next->next,a);

	// cleanup
	node_pop(&head);
	node_pop(&head);
	node_pop(&head);
	node_pop(&head);

	ck_assert(!head);
}
END_TEST

/// TEST CASE -> pop node elements and test
/// EXPECTED -> should free without issues also NULL values
START_TEST(orchdata_ndpop)
{
	static const char *a[] = {NULL, "12", "34"}; // diff values to avoid
	static const char *b[] = {"56", NULL, "78"}; // compiler optimization
	static const char *c[] = {"90", "ab", NULL};

	node_push(&head);
	head->psig = a == NULL ? strdup(a[_i]) : NULL;
	head->contid = b == NULL ? strdup(b[_i]) : NULL;
	head->imgid = c == NULL ? strdup(c[_i]) : NULL;
	node_pop(&head);

	ck_assert(!head);
}
END_TEST

/// TEST CASE -> pop node elements and test
/// EXPECTED -> should not free elements as they are pointing to conf values
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
	node_t * p = head;	// save for test
	node_pop(&head);

	ck_assert(!head);

	// waring! accessing unallocated memrory
	ck_assert(p);
	ck_assert(p->psig);
	ck_assert(p->contid);
	ck_assert(p->imgid);

	free(a);
	free(b);
	free(c);
}
END_TEST

/// TEST CASE -> pop node elements and test
/// EXPECTED -> should free elements, they differ from conf values
START_TEST(orchdata_ndpop3)
{
	char *a = strdup("aa");
	char *b = strdup("bb");
	char *c = strdup("cc");

	cont_t d = {NULL, "sss"};
	img_t e = {NULL, "xxx"};
	pidc_t f = {NULL, "ss", NULL, NULL, &d, &e};
	
	node_push(&head);
	head->psig= a;
	head->contid = b;
	head->imgid = c;
	head->param = &f;
	node_t * p = head;	// save for test
	node_pop(&head);

	ck_assert(!head);

	// waring! accessing unallocated memrory
	ck_assert(p);
	ck_assert(!p->psig);
	ck_assert(!p->contid);
	ck_assert(!p->imgid);
}
END_TEST

// for qsort, descending order
static int cmpPidItem (const void * a, const void * b) {
	return (((node_t *)b)->pid - ((node_t *)a)->pid);
}

/// TEST CASE -> sort a element by pid
/// EXPECTED -> nodes are sorted in descending order, all present
START_TEST(orchdata_qsort)
{	
	long sum = 0;
	int no;
	int cnt = rand() % 20 + 3;

	ck_assert(!head);

	for (int i = 0; i < cnt; i ++) {
		node_push(&head);
		no = rand() % 32768;
		sum += no;
		head->pid = no;
	}

	// apply quick sort!
	qsortll((void **)&head, cmpPidItem);

	no = 32768;
	for (node_t * curr = head; ((curr)); curr=curr->next) {
		ck_assert_int_le (curr->pid, no);	// old < new -> order verify
		sum -= curr->pid; 					// create diff, -> sum must go to 0, mismatch verify
		cnt--; 								// count down -> must go to 0, count verify
		no = curr->pid;
	}

	ck_assert_int_eq(sum, 0);
	ck_assert_int_eq(cnt, 0);

	// cleanup	
	while ((head))
		node_pop(&head);
}
END_TEST

/// TEST CASE -> sort basic tests
/// EXPECTED -> should not fail
START_TEST(orchdata_qsort2)
{	
	// apply quick sort!
	qsortll(NULL, cmpPidItem);
}
END_TEST

/// TEST CASE -> sort basic tests
/// EXPECTED -> should not fail
START_TEST(orchdata_qsort3)
{	
	node_push(&head);	
	node_push(&head);	
	// apply quick sort!
	qsortll((void **)&head, NULL);
	node_pop(&head);
	node_pop(&head);
}
END_TEST


/// Static setup for all tests in the following batch
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
								"weep",
								"hard 5",
								NULL };

		cont->rscs = contparm->rscs;
		cont->attr = contparm->attr;

		const char ** pidsig = pids;
		while (*pidsig) {
			// new pid
			push((void**)&contparm->pids, sizeof(pidc_t));
			push((void**)&cont->pids, sizeof(pids_t));
			cont->pids->pid = contparm->pids; // add new empty item -> pid list, container pids list
			contparm->pids->psig = strdup(*pidsig);
			contparm->pids->rscs = cont->rscs;
			contparm->pids->attr = cont->attr;
			pidsig++;
		}

	}
	
	// image by digest
	push((void**)&contparm->img, sizeof(img_t));
	img_t * img = contparm->img;
	img->imgid= strdup("51c3cc77fcf051c3cc77fcf0");
	img->rscs = contparm->rscs;
	img->attr = contparm->attr;

	// image by tag
	push((void**)&contparm->img, sizeof(img_t));
	img = contparm->img;
	img->imgid= strdup("testimg");
	img->rscs = contparm->rscs;
	img->attr = contparm->attr;

	{	
		// add one more container
		push((void**)&contparm->cont, sizeof(cont_t));
		cont = contparm->cont;
		cont->contid=strdup("d7408531a3b4d7408531a3b4");
		cont->rscs = img->rscs;
		cont->attr = img->attr;

		const char *pids[] = {	"p 4",
								NULL };
		const char ** pidsig = pids;
		while (*pidsig) {
			// new pid
			push((void**)&contparm->pids, sizeof(pidc_t));
			push((void**)&cont->pids, sizeof(pids_t));
			cont->pids->pid = contparm->pids; // add new empty item -> pid list, container pids list
			contparm->pids->psig = strdup(*pidsig);
			contparm->pids->rscs = cont->rscs;
			contparm->pids->attr = cont->attr;			
			pidsig++;
		}
	}

	// relate to last container - image by tag
	push((void**)&img->conts, sizeof(conts_t));
	img->conts->cont = cont;

}

static void orchdata_tc2_teardown () {

	while (contparm->img){
		while (contparm->img->conts)
			pop((void**)&contparm->img->conts);

		while (contparm->img->pids)
			pop((void**)&contparm->img->pids);

		free (contparm->img->imgid);
		pop((void**)&contparm->img);
	}

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
/// EXPECTED -> verifies that all parameters are found as expected 
START_TEST(orchdata_findparams)
{	
	// complete match, center, beginning, end
	static const char *sigs[] = { "hard 5", "do we sleep or more", "weep 1", "keep 4"};

	node_push(&head);
	head->psig = strdup(sigs[_i]);
	int retv = node_findParams(head , contparm);
	
	ck_assert_int_eq(retv, 0);
	ck_assert(head->param);
	ck_assert(head->param->rscs);
	ck_assert(head->param->attr);

	node_pop(&head);
}
END_TEST

/// TEST CASE -> test configuration find 
/// EXPECTED -> verifies that all parameters are found as expected 
START_TEST(orchdata_findparams_cont)
{	
	// complete match, center, beginning, end
	static const char *sigs[] = { "test123", "command", "weep 1", "keep 4"};

	node_push(&head);
	head->psig = strdup(sigs[_i]);
	head->contid = (_i % 2) == 1 ? strdup("a2aa8c37ce4ca2aa8c37ce4c") : strdup("d7408531a3b4d7408531a3b4");
	int retv = node_findParams(head , contparm);
	
	ck_assert_int_eq(retv, 0);
	ck_assert(head->param);
	ck_assert(head->param->cont);
	ck_assert(head->param->cont->contid);
	ck_assert_str_eq(head->param->cont->contid, head->contid);
	ck_assert(head->param->rscs);
	ck_assert(head->param->attr);

	node_pop(&head);
}
END_TEST

/// TEST CASE -> test configuration find 
/// EXPECTED -> verifies that all parameters are found as expected 
START_TEST(orchdata_findparams_image)
{	
	// complete match, center, beginning, end
	static const char *sigs[] = { "test123", "command", "weep 1", "keep 4"};

	node_push(&head);
	head->psig = strdup(sigs[_i]);
	head->contid = _i  >= 2 ? NULL : strdup("d7408531a3b4d7408531a3b4");
	head->imgid = (_i % 2) == 1 ? strdup("testimg") : strdup("51c3cc77fcf051c3cc77fcf0");
	int retv = node_findParams(head , contparm);
	
	ck_assert_int_eq(retv, 0);
	ck_assert(head->param);
	ck_assert(head->param->rscs);
	ck_assert(head->param->attr);

	ck_assert(head->param->img);
	ck_assert(head->param->img->imgid);
	ck_assert_str_eq(head->param->img->imgid, head->imgid);
	ck_assert(head->param->img->rscs);
	ck_assert(head->param->img->attr);

	ck_assert_int_eq(retv, 0);
	ck_assert(head->param);
	ck_assert(head->param->cont);
	ck_assert(head->param->cont->contid || _i >= 2);
	if (_i<2) {
		ck_assert_str_eq(head->param->cont->contid, head->contid);
		ck_assert(head->param->cont->rscs);
		ck_assert(head->param->cont->attr);
	}
	
	node_pop(&head);
}
END_TEST

/// TEST CASE -> test configuration find 
/// EXPECTED -> verifies that all parameters are found as expected 
START_TEST(orchdata_findparams_fail)
{	
	// complete match, center, beginning, end
	node_push(&head);
	head->psig = _i 		? NULL : strdup("wleep 1 as");
	head->imgid = _i == 2 	? NULL : strdup("32aeede2352d57f52");
	head->contid = _i == 3 	? NULL : strdup("32aeede2352d57f52");
	int retv = node_findParams(head , contparm);
	
	ck_assert_int_eq(retv, -1);
	ck_assert(!head->param);

	node_pop(&head);
}
END_TEST

void library_orchdata (Suite * s) {

	TCase *tc0 = tcase_create("orchdata_memory");
	tcase_add_test(tc0, orchdata_ndpush);
	tcase_add_test(tc0, orchdata_ndpop);
	tcase_add_test(tc0, orchdata_ndpop2);
	tcase_add_test(tc0, orchdata_ndpop3);
	tcase_add_test(tc0, orchdata_qsort);
	tcase_add_test(tc0, orchdata_qsort2);
	tcase_add_test(tc0, orchdata_qsort3);
    suite_add_tcase(s, tc0);

	TCase *tc1 = tcase_create("orchdata_pidcont");
	tcase_add_unchecked_fixture(tc1, orchdata_setup, orchdata_teardown);
	tcase_add_test(tc1, orchdata_pidcont);
    suite_add_tcase(s, tc1);

	TCase *tc2 = tcase_create("orchdata_findparams");
	tcase_add_unchecked_fixture(tc2, orchdata_setup, orchdata_teardown);
	tcase_add_checked_fixture(tc2, orchdata_tc2_setup, orchdata_tc2_teardown);
	tcase_add_loop_test(tc2, orchdata_findparams, 0, 4);
	tcase_add_loop_test(tc2, orchdata_findparams_cont, 0, 4);
	tcase_add_loop_test(tc2, orchdata_findparams_image, 0, 4);
	tcase_add_loop_test(tc2, orchdata_findparams_fail, 0, 4);
    suite_add_tcase(s, tc2);

	return;
}
