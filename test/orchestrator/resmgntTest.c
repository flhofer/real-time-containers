/* 
###############################
# test script by Florian Hofer
# last change: 27/11/2020
# ©2020 all rights reserved ☺
###############################
*/

#include "resmgntTest.h"
#include "../test.h"

// Includes from orchestrator library
#include "../../src/include/parse_config.h"
#include "../../src/include/kernutil.h"

// tested
#include "../../src/orchestrator/resmgnt.c"

#include <limits.h>
#include <sys/resource.h>
#include <linux/sched.h>	// linux specific scheduling

// for MUSL based systems
#ifndef RLIMIT_RTTIME
	#define RLIMIT_RTTIME 15
#endif

/// TEST CASE -> check return value with different resources & schedules
/// EXPECTED -> comparison value OK
START_TEST(checkValueTest)
{	
	resTracer_t res = {
			NULL, 0, 0.0, MSK_STATHRMC, 0, 0
	};
	struct sched_attr par = {
		48,
		SCHED_DEADLINE,
		0, 0, 0,
		10000,
		100000,
		100000
	};

	int rv;

	// Empty CPU
	rv = checkUvalue(&res, &par, 1);
	ck_assert_int_eq(2, rv);
	// perfect fit using same period (+ harmonic)
	rv = checkUvalue(&res, &par, 0);
	ck_assert_int_eq(0, rv);

	// fitting match, task is LCM, integer multiple ( + harmonic )
	par.sched_period = 200000;
	rv = checkUvalue(&res, &par, 0);
	ck_assert_int_eq(1, rv);

	// fitting match, resource is LCM, integer multiple ( + harmonic )
	par.sched_period = 50000;
	rv = checkUvalue(&res, &par, 1);
	ck_assert_int_eq(2, rv);

	// no direct match, LCM = 10ns ( loss of harmonic property )
	par.sched_period = 40000;
	rv = checkUvalue(&res, &par, 1);
	ck_assert_int_eq(3, rv); // score fit 0div + offset NHARM (2)

	// fitting match, task = resource = LCM = 10ns ( but not harmonic anymore )
	par.sched_period = 100000;
	rv = checkUvalue(&res, &par, 0);
	ck_assert_int_eq(4, rv); // score fit 1div + offset NHARM (2)

	// No space left
	par.sched_period = 15000;
	rv = checkUvalue(&res, &par, 0);
	ck_assert_int_eq(-1, rv);
}
END_TEST

static void
setup() {
	prgset = calloc (1, sizeof(prgset_t));
	parse_config_set_default(prgset);

	prgset->affinity = strdup("0");
	prgset->affinity_mask = parse_cpumask(prgset->affinity);
}

static void
teardown() {
	// free memory
	while (rHead)
		pop((void**)&rHead);
	while (nhead)
		pop((void**)&nhead);

	freePrgSet(prgset);
}

/// TEST CASE -> check correct creation of resource allocation
/// EXPECTED -> one resource matching the selected CPU
START_TEST(createTracerTest)
{

	// test filling CPU0
	createResTracer();

	ck_assert_ptr_ne(NULL, rHead);
	ck_assert_int_eq(rHead->affinity, 0);
	ck_assert_int_eq((int)rHead->basePeriod,0);
	ck_assert_int_eq((int)rHead->usedPeriod,0);
	ck_assert_int_eq((int)rHead->U,0);

	pop((void**)&rHead);

	// test no CPU
	(void)numa_bitmask_clearbit(prgset->affinity_mask, 0);

	createResTracer();

	ck_assert_ptr_eq(NULL, rHead);
}
END_TEST

/// TEST CASE -> check grepping of low resource tracer
/// EXPECTED -> one resource matching the lowest U-CPU
START_TEST(grepTracerTest)
{

	// test filling CPU0
	createResTracer();
	// add one more, UL 0.5
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->U = 0.5;

	ck_assert_ptr_eq(grepTracer(), rHead->next);

	rHead->U = 0.8; // test harmonic preference
	rHead->next->U=0.9;
	rHead->next->status |= MSK_STATHRMC;
	ck_assert_ptr_eq(grepTracer(), rHead->next);

	// invert values
	pop((void**)&rHead);
	rHead->U = 0.5;
	push((void**)&rHead, sizeof(struct resTracer));

	ck_assert_ptr_eq(grepTracer(), rHead);
}
END_TEST

/// TEST CASE -> check getting of resource tracer
/// EXPECTED -> one resource matching the CPU id
START_TEST(getTracerTest)
{

	// test filling CPU0
	createResTracer();
	// add one more, CPU2
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->affinity = 6;
	// add one more, CPU1
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->affinity = 3;

	ck_assert_ptr_eq(getTracer(3), rHead);
	ck_assert_ptr_eq(getTracer(4), NULL);
	ck_assert_ptr_eq(getTracer(0), rHead->next->next);
}
END_TEST

/// TEST CASE -> check best fit for a certain period
/// EXPECTED -> one resource matching the CPU id
START_TEST(checkPeriodTest)
{
	// test filling CPU0
	createResTracer();
	rHead->basePeriod = 100000;
	// add one more, CPU1
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->basePeriod = 50000;
	rHead->status = MSK_STATHRMC;
	// add one more, CPU2
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->basePeriod = 70000;
	rHead->affinity = 1;
	rHead->status = MSK_STATHRMC;

	struct sched_attr par ={
			48,
			SCHED_DEADLINE,
			0, 0, 0,
			1000,
			10000,
			100000
	};

	ck_assert_ptr_eq(checkPeriod(&par, -99), rHead->next->next);// exact period match
//  TODO: temporarly skipped as harmonic matches have no number preference in simple version
//	par.sched_period = 10000;
//	ck_assert_ptr_eq(checkPeriod(&par, -99), rHead->next);		// par is new period, prefer higher GCD
	par.sched_period = 140000;
	ck_assert_ptr_eq(checkPeriod(&par, -99), rHead);			// par is double period ;)
	par.sched_period = 75000;
	ck_assert_ptr_eq(checkPeriod(&par, -99), rHead);			// no perfect fit, prefer better U fit
	rHead->U = 0.1;
	ck_assert_ptr_eq(checkPeriod(&par, -99), rHead->next);		// no perfect fit, prefer better U fit
	rHead->basePeriod = 100000;
	par.sched_period = 100000;
	ck_assert_ptr_eq(checkPeriod(&par, -1), rHead);				// par, prefer affinity
	rHead->U = 0.7;
	ck_assert_ptr_eq(checkPeriod(&par, -99), rHead->next->next);// par, prefer lower U

	// TODO: all full returns NULL
}
END_TEST

/// TEST CASE -> check best fit for a certain period
/// EXPECTED -> one resource matching the CPU id
START_TEST(checkPeriod_RTest)
{

	// test filling CPU0
	createResTracer();
	rHead->basePeriod = 100000;
	// add one more, CPU1
	push((void**)&rHead, sizeof(struct resTracer));
	rHead->basePeriod = 50000;
	rHead->status = MSK_STATHRMC;

	node_t * item = NULL;
	pidc_t * param = calloc(1, sizeof(pidc_t));
	param->rscs = calloc(1, sizeof(struct sched_rscs));
	node_push(&item);
	item->param = param;
	struct sched_attr par ={
			48,
			SCHED_DEADLINE,
			0, 0, 0,
			1000,
			10000,
			100000
	};
	item->attr = par;
	item->param->rscs->affinity = -99;
	item->mon.assigned = 1;

	ck_assert_ptr_eq(checkPeriod_R(item, 1), rHead->next);// exact period match

	item->attr.sched_policy = SCHED_FIFO;
	item->mon.cdf_period = 50000;
	item->mon.cdf_runtime = 560;
	ck_assert_ptr_eq(checkPeriod_R(item, 1), rHead);// exact period match

	ck_assert_ptr_eq(checkPeriod_R(item, 0), rHead);// test include/exclude test

	item->mon.assigned = -1;
	ck_assert_ptr_eq(checkPeriod_R(item, 0), NULL);// test include/exclude test

	node_pop(&item);
	free(param->rscs);
	free(param);
}
END_TEST

/// TEST CASE -> check best period fit for CDF captured values
/// EXPECTED -> closest period in up-to 1/16th of a sec steps

struct {
	uint64_t expected;
	uint64_t measured;
} findPeriodVal[6] = {
	{NSEC_PER_SEC, 0},		// default case if nothing has been measured or set yet
	{200000000, 198402123}, // bit lower internal 10s
	{62500000, 63234114},	// bit higher lower 3/4 of 10s
	{875000, 874345},		// bit lower upper 3/4 of 10s
	{125000000, 121212343},	// bit lower 100s + 1/40th
	{925000000, 929473927},	// bit higher 100s + 1/40th
};

START_TEST(findPeriodTest)
{
	uint64_t ms = findPeriodMatch(findPeriodVal[_i].measured);

	ck_assert( ms == findPeriodVal[_i].expected);
}
END_TEST

START_TEST(recomputeTimesTest)
{
	createResTracer();

	struct sched_attr attr = {48, SCHED_DEADLINE,
						0, 0, 0,
						100000,
						1000000,
						1000000};
	resTracer_t * ftrc = getTracer(0);

	for (int i = 0; i<8; i++){
		push((void**)&nhead, sizeof(node_t));
		nhead->attr = attr;
		nhead->mon.assigned = 0;
	}
	// check normal
	ck_assert_int_eq(0, recomputeCPUTimes(0));
	ck_assert(ftrc->U >= 0.799 && ftrc->U < 0.801);

	attr.sched_deadline = 250000;
	attr.sched_period = 250000; // = 20% load
	push((void**)&nhead, sizeof(node_t));
	nhead->attr = attr;
	nhead->mon.assigned = 0;

	// check full
	ck_assert_int_eq(-1, recomputeCPUTimes_u(0, NULL));
	ck_assert(ftrc->U >= 1);

	ck_assert_int_eq(0, recomputeTimes_u(ftrc, nhead));

}
END_TEST

static void tc5_setupUnchecked() {
	contparm = malloc (sizeof(containers_t));
	contparm->img = NULL; // locals are not initialized
	contparm->pids = NULL;
	contparm->cont = NULL;
	contparm->nthreads = 0;
	contparm->num_cont = 0;

	contparm->rscs = malloc(sizeof(struct sched_rscs)); // allocate dummy structures
	contparm->attr = malloc(sizeof(struct sched_attr)); // allocate
	contparm->rscs->affinity_mask = NULL;

	cont_t * cont;
	{
	// container
	push((void**)&contparm->cont, sizeof(cont_t));
	cont = contparm->cont;
	cont->contid= strdup("a2aa8c37ce4ca2aa8c37ce4c");


		const char *pids[] = {	"sleep",
								"weep",
								"hard 5",
								NULL };

		cont->status = MSK_STATSHAT | MSK_STATSHRC;
		cont->rscs = contparm->rscs;
		cont->attr = contparm->attr;

		const char ** pidsig = pids;
		while (*pidsig) {
			// new pid
			push((void**)&contparm->pids, sizeof(pidc_t));
			push((void**)&cont->pids, sizeof(pids_t));
			cont->pids->pid = contparm->pids; // add new empty item -> pid list, container pids list
			contparm->pids->psig = strdup(*pidsig);
			contparm->pids->status = MSK_STATSHAT | MSK_STATSHRC;
			contparm->pids->rscs = cont->rscs;
			contparm->pids->attr = cont->attr;
			pidsig++;
		}

	}

	// image by digest
	push((void**)&contparm->img, sizeof(img_t));
	img_t * img = contparm->img;
	img->imgid= strdup("51c3cc77fcf051c3cc77fcf0");
	img->status = MSK_STATSHAT | MSK_STATSHRC;
	img->rscs = contparm->rscs;
	img->attr = contparm->attr;

	// image by tag
	push((void**)&contparm->img, sizeof(img_t));
	img = contparm->img;
	img->imgid= strdup("testimg");
	img->status = MSK_STATSHAT | MSK_STATSHRC;
	img->rscs = contparm->rscs;
	img->attr = contparm->attr;

	{
		// add one more container
		push((void**)&contparm->cont, sizeof(cont_t));
		cont = contparm->cont;
		cont->contid=strdup("d7408531a3b4d7408531a3b4");
		cont->status = MSK_STATSHAT | MSK_STATSHRC;
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
			contparm->pids->status = MSK_STATSHAT | MSK_STATSHRC;
			contparm->pids->rscs = cont->rscs;
			contparm->pids->attr = cont->attr;
			pidsig++;
		}
	}


	{
		// add one more container
		push((void**)&contparm->cont, sizeof(cont_t));
		cont = contparm->cont;
		cont->contid=strdup("mytestcontainer");
		cont->status = MSK_STATSHAT | MSK_STATSHRC;
		cont->rscs = img->rscs;
		cont->attr = img->attr;

		const char *pids[] = {	"rt-testapp",
								NULL };
		const char ** pidsig = pids;
		while (*pidsig) {
			// new pid
			push((void**)&contparm->pids, sizeof(pidc_t));
			push((void**)&cont->pids, sizeof(pids_t));
			cont->pids->pid = contparm->pids; // add new empty item -> pid list, container pids list
			contparm->pids->psig = strdup(*pidsig);
			contparm->pids->status = MSK_STATSHAT | MSK_STATSHRC;
			contparm->pids->rscs = cont->rscs;
			contparm->pids->attr = cont->attr;
			pidsig++;
		}
	}

	// relate to last container - image by tag
	push((void**)&img->conts, sizeof(conts_t));
	img->conts->cont = cont;

}

static void tc5_teardownUnchecked() {
	while (nhead)
		node_pop(&nhead);
	freeContParm(contparm);
}

static void findparamsCheck (int imgtest, int conttest) {

	int retv = findPidParameters(nhead , contparm);
	ck_assert_int_eq(retv, 0);
	if (!nhead->pid) {
		ck_assert_ptr_null(nhead->param);
		return; // no pid number -> dockerlink. No pid config
	}
	ck_assert_ptr_nonnull(nhead->param);
	ck_assert_ptr_nonnull(nhead->param->rscs);
	ck_assert_ptr_nonnull(nhead->param->attr);

	ck_assert((NULL != nhead->param->img) ^ !(imgtest));
	if (imgtest){
		ck_assert(nhead->param->img->imgid);
		ck_assert_str_eq(nhead->param->img->imgid, nhead->imgid);
		ck_assert_ptr_nonnull(nhead->param->img->rscs);
		ck_assert_ptr_nonnull(nhead->param->img->attr);
	}

	if (!(conttest) && !(imgtest)){
		ck_assert_ptr_null(nhead->param->cont);
		// both neg-> nothing to do here. Exit
		return;
	}

	// if only container off, container is created. Test for presence
	ck_assert_ptr_nonnull(nhead->param->cont);
	// if test, tesst for id only. Rest test anyway as created for img
	if (conttest) {
		ck_assert_str_eq(nhead->param->cont->contid, nhead->contid);
		ck_assert_ptr_nonnull(nhead->param->cont->contid);
	}

	ck_assert_ptr_nonnull(nhead->param->cont->rscs);
	ck_assert_ptr_nonnull(nhead->param->cont->attr);
}



/// TEST CASE -> test configuration duplication/update for find
/// EXPECTED -> duplicates or updates existing image or container p-arameters
START_TEST(findparams_dup_Test)
{

	node_push(&nhead);
	nhead->pid = 0;
	nhead->psig = strdup("mytestcontainer");
	nhead->contid = strdup("234432423432343225124126");

	duplicateOrRefreshContainer(nhead, contparm, contparm->cont);

	ck_assert_str_eq(contparm->cont->contid, nhead->contid);
	ck_assert_str_eq(contparm->cont->next->contid, nhead->psig);
	ck_assert_str_ne(contparm->cont->contid, contparm->cont->next->contid);
	ck_assert_ptr_null(contparm->cont->pids->next);
	ck_assert_ptr_nonnull(contparm->cont->pids->pid->psig);

	// Test with existing container from ID, - Created through PID
	contparm->cont->status |= MSK_STATCCRT;
	// also, associate an image
	contparm->cont->next->img = contparm->img;
	// and reset shared resources
	contparm->cont->next->status = 0;
	duplicateOrRefreshContainer(nhead, contparm, contparm->cont->next);

	ck_assert_str_eq(contparm->cont->contid, nhead->contid); // should not change
	ck_assert_str_eq(contparm->cont->next->contid, nhead->psig);
	ck_assert_str_ne(contparm->cont->contid, contparm->cont->next->contid); // no new config
	ck_assert_ptr_nonnull(contparm->cont->pids->next);
	ck_assert_ptr_eq(contparm->img->pids->pid, contparm->pids->next);
	ck_assert_ptr_eq(contparm->img, contparm->pids->next->img);

	// check for resource duplication
	ck_assert_ptr_ne(contparm->cont->attr, contparm->cont->next->attr);
	ck_assert_ptr_ne(contparm->cont->rscs, contparm->cont->next->rscs);
	ck_assert_ptr_ne(contparm->cont->rscs->affinity_mask, contparm->cont->next->rscs->affinity_mask);
	// free again
	contparm->cont->status |= MSK_STATCCRT;
	free (contparm->cont->attr);
	numa_free_cpumask(contparm->cont->rscs->affinity_mask);
	free(contparm->cont->rscs);

	// same test for pid resources
	contparm->cont->next->pids->pid->status = 0;
	duplicateOrRefreshContainer(nhead, contparm, contparm->cont->next);

	ck_assert_ptr_ne(contparm->cont->pids->pid->attr, contparm->cont->next->pids->pid->attr);
	ck_assert_ptr_ne(contparm->cont->pids->pid->rscs, contparm->cont->next->pids->pid->rscs);
	ck_assert_ptr_ne(contparm->cont->pids->pid->rscs->affinity_mask, contparm->cont->next->pids->pid->rscs->affinity_mask);

	// reset
	contparm->cont->next->status = MSK_STATSHRC | MSK_STATSHAT;
	contparm->cont->next->pids->pid->status = MSK_STATSHRC | MSK_STATSHAT;
	contparm->pids->status = MSK_STATSHRC | MSK_STATSHAT;
	free (contparm->pids->attr);
	numa_free_cpumask(contparm->pids->rscs->affinity_mask);
	free(contparm->pids->rscs);

	// check impage association pid
	contparm->cont->status |= MSK_STATCCRT;
	contparm->cont->next->pids->pid->img = contparm->img;
	nhead->status |= MSK_STATUPD;
	nhead->param = contparm->pids; // for update simulation
	duplicateOrRefreshContainer(nhead, contparm, contparm->cont->next);
	ck_assert_ptr_eq(contparm->img->pids->pid, contparm->pids);
	ck_assert_ptr_eq(contparm->img, contparm->pids->img);
	ck_assert_int_eq(0, nhead->status & MSK_STATUPD);
}
END_TEST

/// TEST CASE -> test configuration find
/// EXPECTED -> verifies that all parameters are found as expected
START_TEST(findparamsTest)
{
	// complete match, center, beginning, end
	static const char *sigs[] = { "hard 5", "do we sleep or more", "weep 1", "keep 4"};

	node_push(&nhead);
	nhead->pid = 1;
	nhead->psig = strdup(sigs[_i]);

	findparamsCheck( 0, 0 );

	node_pop(&nhead);
}
END_TEST

/// TEST CASE -> test configuration find
/// EXPECTED -> verifies that all parameters are found as expected
START_TEST(findparamsContTest)
{
	// TODO!!!
	static const char *sigs[] = { "test123", "command", "weep 1", "keep 4"};

	node_push(&nhead);
	nhead->pid = 1;
	nhead->psig = strdup(sigs[_i]);
	nhead->contid = (_i % 2) == 1 ? strdup("a2aa8c37ce4ca2aa8c37ce4c") : strdup("d7408531a3b4d7408531a3b4");

	findparamsCheck( 0, 1 );

	node_pop(&nhead);
}
END_TEST

/// TEST CASE -> test configuration find
/// EXPECTED -> verifies that all parameters are found as expected
START_TEST(findparamsImageTest)
{
	// some match, 2,3
	static const char *sigs[] = { "test123", "command", "weep 1", "keep 4", "rt-testapp", "p 4"};

	node_push(&nhead);
	nhead->pid = 1;
	nhead->psig = strdup(sigs[_i]);
	nhead->contid = (_i >= 2) ? NULL : strdup("d7408531a3b4d7408531a3b4");
	nhead->imgid  = (_i % 2) == 1 ? strdup("testimg") : strdup("51c3cc77fcf051c3cc77fcf0");

	findparamsCheck( 1, (_i<2) );

	node_pop(&nhead);
}
END_TEST

/// TEST CASE -> test configuration find
/// EXPECTED -> verifies that all parameters are found as expected
START_TEST(findparamsFailTest)
{
	// sometimes null, sometimes with id, but never fitting -> check segfaults
	node_push(&nhead);
	nhead->pid = _i ? 1 : 0;
	nhead->psig = 	_i 		? NULL : strdup("wleep 1 as");
	nhead->contid = _i == 3 ? NULL : strdup("32aeede2352d57f52");
	nhead->imgid  = _i == 2 ? NULL : strdup("32aeede2352d57f52");
	int retv = findPidParameters(nhead , contparm);

	ck_assert_int_eq(retv, -1);
	ck_assert(!nhead->param);

	node_pop(&nhead);
}
END_TEST


/// TEST CASE -> test configuration find
/// EXPECTED -> verifies that with data from dockerlink the function finds the parameters
START_TEST(findparams_linkTest)
{
	node_push(&nhead);
	nhead->pid = 0;
	nhead->psig = NULL;
	nhead->contid = strdup("d7408531a3b4d7408531a3b4");
	nhead->imgid  = strdup("51c3cc77fcf051c3cc77fcf0");

	findparamsCheck( 1, 1 );

	node_pop(&nhead);
}
END_TEST


/// TEST CASE -> test configuration find
/// EXPECTED -> verifies that with data from dockerlink the function finds the parameters, container name
START_TEST(findparams_link2Test)
{
	node_push(&nhead);
	nhead->pid = 0;
	nhead->psig = strdup("mytestcontainer");
	nhead->contid = strdup("32aeede2352d57f52");
	nhead->imgid  = strdup("c3cc77fcf051c3cc7");

	findparamsCheck( 0, 1 );

	node_pop(&nhead);
}
END_TEST

void orchestrator_resmgnt (Suite * s) {
	TCase *tc1 = tcase_create("resmgnt_periodFitting");
	tcase_add_test(tc1, checkValueTest);

    suite_add_tcase(s, tc1);

    TCase *tc2 = tcase_create("resmgnt_tracing");
	tcase_add_checked_fixture(tc2, setup, teardown);
	tcase_add_test(tc2, createTracerTest);
	tcase_add_test(tc2, getTracerTest);
	tcase_add_test(tc2, grepTracerTest);

    suite_add_tcase(s, tc2);

    TCase *tc3 = tcase_create("resmgnt_checkPeriod");
	tcase_add_checked_fixture(tc3, setup, teardown);
	tcase_add_test(tc3, checkPeriodTest);
	tcase_add_test(tc3, checkPeriod_RTest);
	tcase_add_loop_test(tc3, findPeriodTest, 0, 6);

    suite_add_tcase(s, tc3);

    TCase *tc4 = tcase_create("resmgnt_recomputeTimes");
	tcase_add_checked_fixture(tc4, setup, teardown);
	tcase_add_test(tc4, recomputeTimesTest);

    suite_add_tcase(s, tc4);

	TCase *tc5 = tcase_create("resmgnt_findparams");
	tcase_add_unchecked_fixture(tc5, tc5_setupUnchecked, tc5_teardownUnchecked);
	tcase_add_test(tc5, findparams_dup_Test);
	tcase_add_loop_test(tc5, findparamsTest, 0, 4);
	tcase_add_loop_test(tc5, findparamsContTest, 0, 4);
	tcase_add_loop_test(tc5, findparamsImageTest, 0, 4);
	tcase_add_loop_test(tc5, findparamsFailTest, 0, 4);
	tcase_add_test(tc5, findparams_linkTest);
	tcase_add_test(tc5, findparams_link2Test);

	suite_add_tcase(s, tc5);


	return;
}
