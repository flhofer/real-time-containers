/* 
###############################
# test script by Florian Hofer
# last change: 17/07/2019
# ©2019 all rights reserved ☺
###############################
*/
#include "orchdataTest.h"
#include "../test.h"

// Includes from orchestrator library
#include "../../src/include/parse_config.h"
#include "../../src/include/kernutil.h"

// tested
#include "../../src/lib/orchdata.c"

#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update

/// TEST CASE -> push node elements and test
/// EXPECTED -> pushing, no matter where, should keep order, data is initialized
START_TEST(orchdata_ndpush)
{
	ck_assert(!nhead);
	node_push(&nhead);

	ck_assert_ptr_eq(nhead->next, NULL);
	ck_assert_int_eq(nhead->pid, 0);
	ck_assert_int_eq(nhead->status, 0);
	ck_assert_ptr_eq(nhead->psig, NULL);
	ck_assert_ptr_eq(nhead->contid, NULL);
	ck_assert_ptr_eq(nhead->imgid, NULL);
	ck_assert_int_eq(nhead->attr.size, SCHED_ATTR_SIZE);
	ck_assert_int_eq(nhead->attr.sched_policy, SCHED_NODATA);
	ck_assert_int_eq(nhead->mon.rt_min, UINT64_MAX);
	ck_assert_int_eq(nhead->mon.rt_avg, 0);
	ck_assert_int_eq(nhead->mon.rt_max, 0);
	ck_assert_int_eq(nhead->mon.dl_count, 0);
	ck_assert_int_eq(nhead->mon.dl_scanfail, 0);
	ck_assert_int_eq(nhead->mon.dl_overrun, 0);
	ck_assert_int_eq(nhead->mon.deadline, 0);
	ck_assert_int_eq(nhead->mon.rt, 0);
	ck_assert_int_eq(nhead->mon.last_cpu, -1);
	ck_assert_int_eq(nhead->mon.dl_diff, 0);
	ck_assert_int_eq(nhead->mon.dl_diffmin, INT64_MAX);
	ck_assert_int_eq(nhead->mon.dl_diffavg, 0);
	ck_assert_int_eq(nhead->mon.dl_diffmax, INT64_MIN);
	ck_assert_ptr_eq(nhead->param, NULL);

	// verify default settings

	node_t * a = nhead;
	node_push(&nhead);
	node_t * b = nhead;
	node_push(&nhead);
	node_t * c = nhead;

	// nhead |-> c -> b -> a
	ck_assert_ptr_eq(nhead,c);
	ck_assert_ptr_eq(nhead->next,b);
	ck_assert_ptr_eq(nhead->next->next,a);

	// nhead |-> c -> a
	node_pop(&c->next); // drop after C
	
	ck_assert_ptr_eq(nhead,c);
	ck_assert_ptr_eq(nhead->next,a);

	node_push(&nhead);
	node_push(&c->next);

	// nhead |->x -> c -> y -> a
	ck_assert_ptr_eq(nhead->next,c);
	ck_assert_ptr_eq(nhead->next->next->next,a);

	// cleanup
	node_pop(&nhead);
	node_pop(&nhead);
	node_pop(&nhead);
	node_pop(&nhead);

	ck_assert(!nhead);
}
END_TEST

/// TEST CASE -> duplicate owned resource configuration
/// EXPECTED -> values and affinity are copied into independent allocations
START_TEST(orchdata_copyresources)
{
	struct sched_attr attr = {.size = SCHED_ATTR_SIZE, .sched_policy = SCHED_FIFO};
	struct sched_rscs rscs = {.affinity = 3, .rt_timew = 4, .rt_time = 5};
	rscs.affinity_mask = numa_allocate_cpumask();
	ck_assert_ptr_ne(rscs.affinity_mask, NULL);
	numa_bitmask_setbit(rscs.affinity_mask, 1);
	cont_t source = {.attr = &attr, .rscs = &rscs};
	cont_t target = {0};

	copyResourceConfigC(&source, &target);
	ck_assert_ptr_ne(target.attr, source.attr);
	ck_assert_ptr_ne(target.rscs, source.rscs);
	ck_assert_ptr_ne(target.rscs->affinity_mask, source.rscs->affinity_mask);
	ck_assert_int_eq(memcmp(target.attr, source.attr, sizeof(attr)), 0);
	ck_assert_int_eq(target.rscs->affinity, source.rscs->affinity);
	ck_assert_int_eq(numa_bitmask_isbitset(target.rscs->affinity_mask, 1), 1);

	freeParm(&target);
	numa_free_cpumask(rscs.affinity_mask);
}
END_TEST

/// TEST CASE -> copy shared resource configuration for a PID
/// EXPECTED -> shared pointers are retained and not freed with the destination
START_TEST(orchdata_copyresources_shared)
{
	struct sched_attr attr = {.size = SCHED_ATTR_SIZE};
	struct sched_rscs rscs = {0};
	pidc_t source = {.status = MSK_STATSHAT | MSK_STATSHRC,
		.attr = &attr, .rscs = &rscs};
	pidc_t target = {0};

	copyResourceConfigP(&source, &target);
	ck_assert_int_eq(target.status, source.status);
	ck_assert_ptr_eq(target.attr, source.attr);
	ck_assert_ptr_eq(target.rscs, source.rscs);
	freeParm((cont_t *)&target);
}
END_TEST

/// TEST CASE -> release a node containing runtime and period statistics
/// EXPECTED -> all subordinate statistics and affinity allocations are accepted
START_TEST(orchdata_ndpop_statistics)
{
	node_push(&nhead);
	ck_assert_int_eq(runstats_histInit(&nhead->mon.pdf_hist, 1.0), 0);
	ck_assert_int_eq(runstats_histInit(&nhead->mon.pdf_phist, 2.0), 0);
	ck_assert_int_eq(runstats_histAdd(nhead->mon.pdf_hist,
		&nhead->mon.pdf_scope, 1.0), 0);
	ck_assert_int_eq(runstats_histAdd(nhead->mon.pdf_phist,
		&nhead->mon.pdf_pscope, 2.0), 0);
	ck_assert_int_eq(runstats_cdfCreate(&nhead->mon.pdf_hist,
		&nhead->mon.pdf_cdf), 0);
	ck_assert_int_eq(runstats_cdfCreate(&nhead->mon.pdf_phist,
		&nhead->mon.pdf_pcdf), 0);
	nhead->mon.assigned_mask = numa_allocate_cpumask();
	ck_assert_ptr_ne(nhead->mon.assigned_mask, NULL);

	node_pop(&nhead);
	ck_assert_ptr_eq(nhead, NULL);
}
END_TEST

/// TEST CASE -> release program settings and a list of resource tracers
/// EXPECTED -> all owned strings and CPU masks are accepted
START_TEST(orchdata_free_settings)
{
	prgset_t * settings = calloc(1, sizeof(*settings));
	ck_assert_ptr_ne(settings, NULL);
	settings->logdir = strdup("logdir");
	settings->logbasename = strdup("logbase");
	settings->cont_ppidc = strdup("ppid");
	settings->cont_pidc = strdup("pid");
	settings->cont_cgrp = strdup("cgroup");
	settings->procfileprefix = strdup("proc");
	settings->cgroupfileprefix = strdup("cgroupfs");
	settings->cpusystemfileprefix = strdup("cpusystem");
	settings->cpusetdfileprefix = strdup("cpuset");
	settings->affinity = strdup("0-1");
	settings->affinity_mask = numa_allocate_cpumask();
	freePrgSet(settings);

	resTracer_t * tracers = NULL;
	push((void **)&tracers, sizeof(*tracers));
	tracers->affinity = numa_allocate_cpumask();
	push((void **)&tracers, sizeof(*tracers));
	tracers->affinity = numa_allocate_cpumask();
	freeTracer(&tracers);
	ck_assert_ptr_eq(tracers, NULL);
}
END_TEST

/// TEST CASE -> pop node elements and test
/// EXPECTED -> should free without issues also NULL values
START_TEST(orchdata_ndpop)
{
	static const char *a[] = {NULL, "12", "34"}; // diff values to avoid
	static const char *b[] = {"56", NULL, "78"}; // compiler optimization
	static const char *c[] = {"90", "ab", NULL};

	node_push(&nhead);
	nhead->psig = a[_i] != NULL ? strdup(a[_i]) : NULL;
	nhead->contid = b[_i] != NULL ? strdup(b[_i]) : NULL;
	nhead->imgid = c[_i] != NULL ? strdup(c[_i]) : NULL;
	node_pop(&nhead);

	ck_assert(!nhead);
}
END_TEST

#ifdef DEBUG // not testable if pointers are not reset. Do in debug build only

/// TEST CASE -> pop node elements and test
/// EXPECTED -> should not free elements as they are pointing to conf values
START_TEST(orchdata_ndpop2)
{
	char *a = strdup("aa");
	char *b = strdup("bb");
	char *c = strdup("cc");

	cont_t d = {NULL, b};
	img_t e = {NULL, c};
	pidc_t f = {NULL, a, 0, NULL, NULL, &d, &e};
	
	node_push(&nhead);
	nhead->psig= a;
	nhead->contid = b;
	nhead->imgid = c;
	nhead->param = &f;
	void * p = (void *)nhead;	// save for test
	node_pop(&nhead);

	ck_assert(!nhead);

	// waring! accessing unallocated memrory
	ck_assert(p);
	p+=3; // move to psig
	ck_assert(p++);
	ck_assert(p++);
	ck_assert(p);

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
	pidc_t f = {NULL, "ss", 0, NULL, NULL, &d, &e};

	node_push(&nhead);
	nhead->psig= a;
	nhead->contid = b;
	nhead->imgid = c;
	nhead->param = &f;
	void * p = (void *)nhead;	// save for test
	node_pop(&nhead);

	ck_assert(!nhead);

	// waring! accessing unallocated memory
	ck_assert(p);
	p+=3; // move to psig
	ck_assert(p++);
	ck_assert(p++);
	ck_assert(p);
}
END_TEST
#endif

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

	ck_assert(!nhead);

	for (int i = 0; i < cnt; i ++) {
		node_push(&nhead);
		no = rand() % 32768;
		sum += no;
		nhead->pid = no;
	}

	// apply quick sort!
	qsortll((void **)&nhead, cmpPidItem);

	no = 32768;
	for (node_t * curr = nhead; ((curr)); curr=curr->next) {
		ck_assert_int_le (curr->pid, no);	// old < new -> order verify
		sum -= curr->pid; 					// create diff, -> sum must go to 0, mismatch verify
		cnt--; 								// count down -> must go to 0, count verify
		no = curr->pid;
	}

	ck_assert_int_eq(sum, 0);
	ck_assert_int_eq(cnt, 0);

	// cleanup	
	while ((nhead))
		node_pop(&nhead);
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
	node_push(&nhead);
	node_push(&nhead);
	// apply quick sort!
	qsortll((void **)&nhead, NULL);
	node_pop(&nhead);
	node_pop(&nhead);
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

void library_orchdata (Suite * s) {

	TCase *tc0 = tcase_create("orchdata_memory");
	tcase_add_test(tc0, orchdata_ndpush);
	tcase_add_test(tc0, orchdata_ndpop);
	tcase_add_test(tc0, orchdata_ndpop_statistics);
#ifdef DEBUG // not testable if pointers are not reset. Do in debug build only
	tcase_add_test(tc0, orchdata_ndpop2);
	tcase_add_test(tc0, orchdata_ndpop3);
#endif
	tcase_add_test(tc0, orchdata_qsort);
	tcase_add_test(tc0, orchdata_qsort2);
	tcase_add_test(tc0, orchdata_qsort3);
	tcase_add_test(tc0, orchdata_copyresources);
	tcase_add_test(tc0, orchdata_copyresources_shared);
	tcase_add_test(tc0, orchdata_free_settings);
    suite_add_tcase(s, tc0);

    // FIXME: copyresources tested in duplicateOrRefreshContainer

	TCase *tc1 = tcase_create("orchdata_pidcont");
	tcase_add_unchecked_fixture(tc1, orchdata_setup, orchdata_teardown);
	tcase_add_test(tc1, orchdata_pidcont);
    suite_add_tcase(s, tc1);

	return;
}
