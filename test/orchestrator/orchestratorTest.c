/*
###############################
# test script by Florian Hofer
# last change: 06/08/2026
# ©2026 all rights reserved ☺
###############################
*/

#include "orchestratorTest.h"
#include "../test.h"

#include "parse_config.h"

/* Keep the program entry point and globals separate from the test runner. */
#define main orchestrator_program_main
#define contparm orchestrator_test_contparm
#define prgset orchestrator_test_prgset
#define dataMutex orchestrator_test_dataMutex
#define nhead orchestrator_test_nhead
#define resMutex orchestrator_test_resMutex
#define rHead orchestrator_test_rHead
#ifdef DEBUG
#define dbg_out orchestrator_test_dbg_out
#define stats_out orchestrator_test_stats_out
#endif

#include "../../src/orchestrator/orchestrator.c"

#ifdef DEBUG
#undef stats_out
#undef dbg_out
#endif
#undef rHead
#undef resMutex
#undef nhead
#undef dataMutex
#undef prgset
#undef contparm
#undef main

#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

static void
orchestratorTestConfig(char * path, size_t size){
	ck_assert_int_lt(snprintf(path, size, "/tmp/orchestrator-options-XXXXXX"), (int)size);
	int file = mkstemp(path);
	ck_assert_int_ge(file, 0);
	ck_assert_int_eq(2, write(file, "{}", 2));
	ck_assert_int_eq(0, close(file));
}

static void
orchestratorTestFreeArgs(char ** argv, int argc){
	for (int i=0; i<argc; i++)
		free(argv[i]);
}

static void
orchestratorTestCloseDebug(void){
#ifdef DEBUG
	fclose(orchestrator_test_dbg_out);
	fclose(orchestrator_test_stats_out);
	orchestrator_test_dbg_out = NULL;
	orchestrator_test_stats_out = NULL;
#endif
}

/// TEST CASE -> deliver a termination signal to the main-loop handler
/// EXPECTED -> the stop flag is raised independent of signal metadata
START_TEST(orchestratorSignalTest)
{
	main_stop = 0;
	inthand(SIGTERM, NULL, NULL);
	ck_assert_int_eq(1, main_stop);
}
END_TEST

/// TEST CASE -> parse combined command-line scheduling and discovery options
/// EXPECTED -> values are stored and incompatible deadline/PID mode is normalized
START_TEST(orchestratorOptionsTest)
{
	char configPath[PATH_MAX];
	orchestratorTestConfig(configPath, sizeof(configPath));

	const char * values[] = {
		"orchestrator", "-a1", "-F", "-i", "7000", "-k", "-l", "4",
		"-nworker", "-P", "-q", "-r", "9", "--rr=7", "--system=1",
		"--policy=deadline", "-p", "8", "-w", "1500", "-d", configPath
	};
	int argc = sizeof(values) / sizeof(values[0]);
	char * argv[sizeof(values) / sizeof(values[0]) + 1];
	for (int i=0; i<argc; i++)
		argv[i] = strdup(values[i]);
	argv[argc] = NULL;

	prgset_t * set = calloc(1, sizeof(*set));
	ck_assert_ptr_nonnull(set);
	optind = 1;
	opterr = 0;
	process_options(set, argc, argv, 4);

	ck_assert_int_eq(AFFINITY_USERSPECIFIED, set->setaffinity);
	ck_assert_str_eq("1", set->affinity);
	ck_assert_int_eq(1, numa_bitmask_weight(set->affinity_mask));
	ck_assert(numa_bitmask_isbitset(set->affinity_mask, 1));
	ck_assert_int_eq(1, set->ftrace);
	ck_assert_int_eq(7000, set->interval);
	ck_assert_int_eq(1, set->trackpids);
	ck_assert_int_eq(4, set->loops);
	ck_assert_int_eq(DM_CMDLINE, set->use_cgroup);
	ck_assert_str_eq("worker", set->cont_pidc);
	ck_assert_int_eq(1, set->psigscan);
	ck_assert_int_eq(1, set->quiet);
	ck_assert_int_eq(9, set->runtime);
	ck_assert_int_eq(7, set->rrtime);
	ck_assert_int_eq(SM_DYNMCBIN, set->sched_mode);
	ck_assert_int_eq(SCHED_FIFO, set->policy);
	ck_assert_int_eq(8, set->priority);
	ck_assert_int_eq(1500, set->update_wcet);
	ck_assert_int_eq(1, set->setdflag);
	ck_assert_ptr_nonnull(orchestrator_test_contparm);

	/* These two values point inside argv strings and are not owned by set. */
	set->affinity = NULL;
	set->cont_pidc = NULL;
	freePrgSet(set);
	freeContParm(orchestrator_test_contparm);
	orchestrator_test_contparm = NULL;
	orchestratorTestCloseDebug();
	orchestratorTestFreeArgs(argv, argc);
	ck_assert_int_eq(0, unlink(configPath));
}
END_TEST

/// TEST CASE -> parse adaptive mode and a real-time policy without priority
/// EXPECTED -> adaptive selection is bounded and RR receives its default priority
START_TEST(orchestratorPolicyDefaultsTest)
{
	char configPath[PATH_MAX];
	orchestratorTestConfig(configPath, sizeof(configPath));

	const char * values[] = {
		"orchestrator", "-a0", "--adaptive=99", "--policy=rr", configPath
	};
	int argc = sizeof(values) / sizeof(values[0]);
	char * argv[sizeof(values) / sizeof(values[0]) + 1];
	for (int i=0; i<argc; i++)
		argv[i] = strdup(values[i]);
	argv[argc] = NULL;

	prgset_t * set = calloc(1, sizeof(*set));
	ck_assert_ptr_nonnull(set);
	optind = 1;
	opterr = 0;
	process_options(set, argc, argv, 4);

	ck_assert_int_eq(SM_PADAPTIVE, set->sched_mode);
	ck_assert_int_eq(SCHED_RR, set->policy);
	ck_assert_int_eq(10, set->priority);

	set->affinity = NULL;
	freePrgSet(set);
	freeContParm(orchestrator_test_contparm);
	orchestrator_test_contparm = NULL;
	orchestratorTestCloseDebug();
	orchestratorTestFreeArgs(argv, argc);
	ck_assert_int_eq(0, unlink(configPath));
}
END_TEST

void orchestrator_orchestrator (Suite * s) {
	TCase *tc1 = tcase_create("orchestrator_options");
	tcase_add_test(tc1, orchestratorSignalTest);
	tcase_add_test(tc1, orchestratorOptionsTest);
	tcase_add_test(tc1, orchestratorPolicyDefaultsTest);

	suite_add_tcase(s, tc1);
}
