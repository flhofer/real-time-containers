/*
###############################
# test script by Florian Hofer
# last change: 06/08/2026
# ©2026 all rights reserved ☺
###############################
*/

#include "prepareTest.h"
#include "../test.h"

#include "parse_config.h"
#include "kernutil.h"

// tested
#include "../../src/orchestrator/prepare.c"

#include <ftw.h>
#include <limits.h>

static char prepareTestDirectory[PATH_MAX];

static void
prepareTestPath(char * path, size_t size, const char * prefix, const char * name){
	ck_assert_int_lt(snprintf(path, size, "%s/%s", prefix, name), (int)size);
}

static void
prepareTestDirectoryCreate(const char * prefix, const char * name){
	char path[PATH_MAX];
	prepareTestPath(path, sizeof(path), prefix, name);
	ck_assert_int_eq(0, mkdir(path, 0700));
}

static void
prepareTestWrite(const char * prefix, const char * name, const char * value){
	char path[PATH_MAX];
	prepareTestPath(path, sizeof(path), prefix, name);
	FILE * file = fopen(path, "w");
	ck_assert_msg(file, "Could not create test file %s", path);
	ck_assert_int_eq((int)strlen(value), (int)fwrite(value, 1, strlen(value), file));
	ck_assert_int_eq(0, fclose(file));
}

static void
prepareTestRead(const char * prefix, const char * name, char * value, size_t size){
	char path[PATH_MAX];
	prepareTestPath(path, sizeof(path), prefix, name);
	FILE * file = fopen(path, "r");
	ck_assert_msg(file, "Could not read test file %s", path);
	size_t bytes = fread(value, 1, size - 1, file);
	value[bytes] = '\0';
	ck_assert_int_eq(0, fclose(file));
}

static int
prepareTestRemove(const char * path, const struct stat * status, int type, struct FTW * info){
	(void)status;
	(void)type;
	(void)info;
	return remove(path);
}

static void
orchestrator_prepare_setup() {
	prgset = calloc(1, sizeof(*prgset));
	ck_assert_ptr_nonnull(prgset);
	parse_config_set_default(prgset);

	(void)snprintf(prepareTestDirectory, sizeof(prepareTestDirectory), "/tmp/orchestrator-prepare-XXXXXX");
	ck_assert_ptr_nonnull(mkdtemp(prepareTestDirectory));
	char prefix[PATH_MAX];
	ck_assert_int_lt(snprintf(prefix, sizeof(prefix), "%s/", prepareTestDirectory), (int)sizeof(prefix));
	prgset->cpusystemfileprefix = strdup(prefix);
	prgset->cpusetdfileprefix = strdup(prefix);
	prgset->procfileprefix = strdup(prefix);

	contparm = calloc(1, sizeof(*contparm));
}

static void
orchestrator_prepare_teardown() {
	freeTracer(&rHead);
	adaptFree();
	freePrgSet(prgset);
	freeContParm(contparm);
	prgset = NULL;
	contparm = NULL;
	ck_assert_int_eq(0, nftw(prepareTestDirectory, prepareTestRemove, 16, FTW_DEPTH | FTW_PHYS));
}

/// TEST CASE -> initialize and reconstruct a user-selected CPU affinity
/// EXPECTED -> the parsed mask and textual representation remain consistent
START_TEST(prepareAffinityTest)
{
	prgset->setaffinity = AFFINITY_USERSPECIFIED;
	prgset->affinity = strdup("0,2");
	ck_assert_int_eq(0, setDefaultAffinity(prgset));
	ck_assert_int_eq(2, numa_bitmask_weight(prgset->affinity_mask));
	ck_assert(numa_bitmask_isbitset(prgset->affinity_mask, 0));
	ck_assert(numa_bitmask_isbitset(prgset->affinity_mask, 2));

	numa_bitmask_clearbit(prgset->affinity_mask, 2);
	ck_assert_int_eq(0, resetCPUstring(prgset));
	ck_assert_str_eq("0", prgset->affinity);
}
END_TEST

/// TEST CASE -> read and update the round-robin scheduler interval
/// EXPECTED -> read mode imports the value and write/dry-run modes respect policy
START_TEST(prepareRRsliceTest)
{
	prepareTestWrite(prepareTestDirectory, "sched_rr_timeslice_ms", "42\n");
	getRRslice(prgset);
	ck_assert_int_eq(42, prgset->rrtime);

	prgset->policy = SCHED_RR;
	prgset->rrtime = 77;
	prgset->dryrun = MSK_DRYNORTSLCE;
	getRRslice(prgset);
	char value[20];
	prepareTestRead(prepareTestDirectory, "sched_rr_timeslice_ms", value, sizeof(value));
	ck_assert_str_eq("42\n", value);

	prgset->dryrun = 0;
	getRRslice(prgset);
	prepareTestRead(prepareTestDirectory, "sched_rr_timeslice_ms", value, sizeof(value));
	ck_assert_int_eq(0, strncmp("77", value, 2));
}
END_TEST

/// TEST CASE -> read SMT state and remove offline CPUs from the configured mask
/// EXPECTED -> SMT states and the intersected affinity are reported correctly
START_TEST(prepareTopologyTest)
{
	prepareTestDirectoryCreate(prepareTestDirectory, "smt");
	prepareTestWrite(prepareTestDirectory, "smt/control", "on\n");
	ck_assert_int_eq(1, testSMT(prgset));
	prepareTestWrite(prepareTestDirectory, "smt/control", "off\n");
	ck_assert_int_eq(0, testSMT(prgset));
	prgset->blindrun = 1;
	ck_assert_int_eq(-1, testSMT(prgset));
	prgset->blindrun = 0;

	prepareTestWrite(prepareTestDirectory, "online", "0-2\n");
	prgset->affinity = strdup("0-3");
	prgset->affinity_mask = parse_cpumask(prgset->affinity);
	ck_assert_int_eq(0, resetCPUmask(prgset));
	ck_assert_str_eq("0-2", prgset->affinity);
}
END_TEST

/// TEST CASE -> selectively remove SMT siblings from an affinity mask
/// EXPECTED -> one hardware thread per sibling pair remains selected
START_TEST(prepareSiblingTest)
{
	prepareTestDirectoryCreate(prepareTestDirectory, "cpu0");
	prepareTestDirectoryCreate(prepareTestDirectory, "cpu0/topology");
	prepareTestDirectoryCreate(prepareTestDirectory, "cpu1");
	prepareTestDirectoryCreate(prepareTestDirectory, "cpu1/topology");
	prepareTestWrite(prepareTestDirectory, "cpu0/topology/thread_siblings_list", "0,2\n");
	prepareTestWrite(prepareTestDirectory, "cpu1/topology/thread_siblings_list", "1,3\n");

	prgset->affinity = strdup("0-3");
	prgset->affinity_mask = parse_cpumask(prgset->affinity);
	prgset->dryrun = MSK_DRYNOSMTOFF;
	ck_assert_int_eq(0, disableCPUsibling(prgset));
	ck_assert_str_eq("0-1", prgset->affinity);
	ck_assert_int_eq(2, numa_bitmask_weight(prgset->affinity_mask));
}
END_TEST

/// TEST CASE -> validate CPU governor, frequency and power-QoS preparation
/// EXPECTED -> dry-run preserves values and authorized writes apply requested values
START_TEST(prepareCPUSettingsTest)
{
	prepareTestDirectoryCreate(prepareTestDirectory, "cpu0");
	prepareTestDirectoryCreate(prepareTestDirectory, "cpu0/cpufreq");
	prepareTestDirectoryCreate(prepareTestDirectory, "cpu0/power");
	prepareTestDirectoryCreate(prepareTestDirectory, "intel_pstate");
	prepareTestWrite(prepareTestDirectory, "cpu0/cpufreq/scaling_governor", "powersave\n");
	prepareTestWrite(prepareTestDirectory, "cpu0/cpufreq/scaling_available_governors", "powersave performance\n");
	prepareTestWrite(prepareTestDirectory, "intel_pstate/no_turbo", "0\n");
	prepareTestWrite(prepareTestDirectory, "cpu0/cpufreq/base_frequency", "2000\n");
	prepareTestWrite(prepareTestDirectory, "cpu0/cpufreq/scaling_min_freq", "1000\n");
	prepareTestWrite(prepareTestDirectory, "cpu0/power/pm_qos_resume_latency_us", "10\n");

	prgset->dryrun = MSK_DRYNOCPUGOV;
	ck_assert_int_eq(0, setCPUgovernor(prgset, 0));
	ck_assert_int_eq(0, setCPUpowerQos(prgset, 0));
	char value[32];
	prepareTestRead(prepareTestDirectory, "cpu0/cpufreq/scaling_governor", value, sizeof(value));
	ck_assert_str_eq("powersave\n", value);
	prepareTestRead(prepareTestDirectory, "cpu0/power/pm_qos_resume_latency_us", value, sizeof(value));
	ck_assert_str_eq("10\n", value);

	prgset->dryrun = 0;
	prgset->force = 1;
	ck_assert_int_eq(0, setCPUgovernor(prgset, 0));
	ck_assert_int_eq(0, adjustCPUfreq(prgset, 0));
	ck_assert_int_eq(0, setCPUpowerQos(prgset, 0));
	prepareTestRead(prepareTestDirectory, "cpu0/cpufreq/scaling_governor", value, sizeof(value));
	ck_assert_int_eq(0, strncmp("performance", value, 11));
	prepareTestRead(prepareTestDirectory, "cpu0/cpufreq/scaling_min_freq", value, sizeof(value));
	ck_assert_int_eq(0, strncmp("2000", value, 4));
	prepareTestRead(prepareTestDirectory, "cpu0/power/pm_qos_resume_latency_us", value, sizeof(value));
	ck_assert_int_eq(0, strncmp("n/a", value, 3));
	ck_assert_int_eq(-1, setCPUgovernor(prgset, 1));
}
END_TEST

/// TEST CASE -> count tasks in the configured Docker cgroup hierarchy
/// EXPECTED -> the cgroup-version-specific task representation is counted
START_TEST(prepareCGroupCountTest)
{
#ifdef CGROUP2
	prepareTestWrite(prepareTestDirectory, "pids.current", "3\n");
	ck_assert_int_eq(3, countCGroupTasks(prgset));
#else
	const char * id = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
	prepareTestDirectoryCreate(prepareTestDirectory, id);
	char containerPath[PATH_MAX];
	prepareTestPath(containerPath, sizeof(containerPath), prepareTestDirectory, id);
	prepareTestWrite(containerPath, CGRP_PIDS, "11\n22\n33\n");
	ck_assert_int_eq(3, countCGroupTasks(prgset));
#endif

	free(prgset->cpusetdfileprefix);
	prgset->cpusetdfileprefix = strdup("/tmp/orchestrator-cgroup-missing/");
	ck_assert_int_eq(-1, countCGroupTasks(prgset));
}
END_TEST

/// TEST CASE -> cycle selected CPUs offline and online to move interrupts
/// EXPECTED -> every selected non-boot CPU is returned online
START_TEST(preparePushCPUirqsTest)
{
	prepareTestDirectoryCreate(prepareTestDirectory, "cpu1");
	prepareTestDirectoryCreate(prepareTestDirectory, "cpu2");
	prepareTestWrite(prepareTestDirectory, "cpu1/online", "1");
	prepareTestWrite(prepareTestDirectory, "cpu2/online", "1");
	prgset->affinity_mask = parse_cpumask("0-2");

	pushCPUirqs(prgset);

	char value[4];
	prepareTestRead(prepareTestDirectory, "cpu1/online", value, sizeof(value));
	ck_assert_str_eq("1", value);
	prepareTestRead(prepareTestDirectory, "cpu2/online", value, sizeof(value));
	ck_assert_str_eq("1", value);
}
END_TEST

/// TEST CASE -> cleanup releases resource tracers without optional SMI state
/// EXPECTED -> all tracer entries are removed
START_TEST(prepareCleanupTest)
{
	push((void**)&rHead, sizeof(*rHead));
	rHead->affinity = parse_cpumask("0");
	cleanupEnvironment(prgset);
	ck_assert_ptr_null(rHead);
}
END_TEST

void orchestrator_prepare (Suite * s) {
	TCase *tc1 = tcase_create("prepare_configuration");
	tcase_add_checked_fixture(tc1, orchestrator_prepare_setup, orchestrator_prepare_teardown);
	tcase_add_test(tc1, prepareAffinityTest);
	tcase_add_test(tc1, prepareRRsliceTest);
	tcase_add_test(tc1, prepareTopologyTest);
	tcase_add_test(tc1, prepareSiblingTest);
	tcase_add_test(tc1, prepareCPUSettingsTest);
	tcase_add_test(tc1, prepareCGroupCountTest);
	tcase_add_test(tc1, preparePushCPUirqsTest);
	tcase_add_test(tc1, prepareCleanupTest);

	suite_add_tcase(s, tc1);
}
