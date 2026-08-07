/* 
###############################
# test script by Florian Hofer
# last change: 25/07/2019
# ©2019 all rights reserved ☺
###############################
*/

#include "updateTest.h"
#include "../test.h"

// Includes from orchestrator library
#include "../../src/include/parse_config.h"
#include "../../src/include/kernutil.h"
#include "../../src/include/rt-sched.h"

// tested
#include "../../src/orchestrator/update.c"

#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <limits.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <linux/sched.h>	// linux specific scheduling

// for MUSL based systems
#ifndef RLIMIT_RTTIME
	#define RLIMIT_RTTIME 15
#endif

static pid_t updateTestPids[8];
static size_t updateTestPidCount;

static void
updateTestGetPids(node_t ** pidlst){
	for (size_t i=0; i<updateTestPidCount; i++){
		node_push(pidlst);
		(*pidlst)->pid = updateTestPids[i];
		(*pidlst)->attr.sched_policy = SCHED_OTHER;
		(*pidlst)->psig = strdup("update-test");
	}
}

static void
updateTestWriteFile(const char * path, const char * value){
	FILE * file = fopen(path, "w");
	ck_assert_msg(file, "Could not create test file %s", path);
	ck_assert_int_eq((int)strlen(value), (int)fwrite(value, 1, strlen(value), file));
	ck_assert_int_eq(0, fclose(file));
}

static void
updateTestWriteExecutable(const char * path, const char * value){
	updateTestWriteFile(path, value);
	ck_assert_int_eq(0, chmod(path, S_IRWXU));
}

static void
updateTestSetEvent(enum cont_events event, const char * id){
	containerEvent = calloc(1, sizeof(*containerEvent));
	ck_assert_ptr_nonnull(containerEvent);
	containerEvent->event = event;
	containerEvent->id = strdup(id);
	containerEvent->name = strdup("test-container");
	containerEvent->image = strdup("test-image");
}

static void orchestrator_update_setup() {
	prgset = calloc (1, sizeof(prgset_t));
	parse_config_set_default(prgset);
	pidSignature = NULL;
	pidUpdate = getCmdLinePids;
	lstevent = NULL;
	updateTestPidCount = 0;

	prgset->affinity = strdup("0");
	prgset->affinity_mask = parse_cpumask(prgset->affinity);

	prgset->ftrace = 1;

	// signatures and folders
	prgset->cont_ppidc = strdup(CONT_PPID);
	prgset->cont_pidc = strdup(CONT_PID);
	prgset->cont_cgrp = strdup(CGRP_DCKR);

	// filepaths virtual file system
	prgset->procfileprefix = strdup("/proc/sys/kernel/");
	prgset->cgroupfileprefix = strdup("/sys/fs/cgroup/");
	prgset->cpusystemfileprefix = strdup("/sys/devices/system/cpu/");

	parse_dockerfileprefix(prgset);

	contparm = calloc (1, sizeof(containers_t));
}

static void orchestrator_update_teardown() {
	// free memory
	while (nhead)
		node_pop(&nhead);

	freeContainerEvent(containerEvent);
	containerEvent = NULL;
	freeContainerEvent(lstevent);
	lstevent = NULL;
	free(pidSignature);
	pidSignature = NULL;

	freePrgSet(prgset);
	freeContParm(contparm);
}

/// TEST CASE -> compare PID items and select each supported discovery mode
/// EXPECTED -> PID difference and update callback/signature match the selected mode
START_TEST(orchestrator_update_select)
{
	node_t low = { .pid = 10 };
	node_t high = { .pid = 35 };
	ck_assert_int_eq(25, cmpPidItem(&low, &high));
	ck_assert_int_eq(-25, cmpPidItem(&high, &low));
	ck_assert_int_eq(0, cmpPidItem(&low, &low));

	prgset->use_cgroup = DM_CGRP;
	selectUpdate();
	ck_assert_ptr_eq(pidUpdate, getContPids);

	prgset->use_cgroup = DM_CNTPID;
	selectUpdate();
	ck_assert_ptr_eq(pidUpdate, getParentPids);

	prgset->use_cgroup = DM_CMDLINE;
	free(prgset->cont_pidc);
	prgset->cont_pidc = strdup("sleep");
	selectUpdate();
	ck_assert_ptr_eq(pidUpdate, getCmdLinePids);
	// check generated signature
#ifdef BUSYBOX
	ck_assert_str_eq(pidSignature, "| grep -E 'sleep'");
#else
	ck_assert_str_eq(pidSignature, "-C sleep");
#endif

	free(pidSignature);
	pidSignature = NULL;
	prgset->psigscan = 1;
	selectUpdate();
	// check generated signature with threads
#ifdef BUSYBOX
	ck_assert_str_eq(pidSignature, "-T | grep -E 'sleep'");
#else
	ck_assert_str_eq(pidSignature, "-TC sleep");
#endif
}
END_TEST

/// TEST CASE -> read process IDs from a synthetic container CGroup
/// EXPECTED -> all PIDs receive the container ID and sibling marker
START_TEST(orchestrator_update_getcontpids)
{
	char directory[] = "/tmp/update-cgroup-XXXXXX";
	ck_assert_ptr_nonnull(mkdtemp(directory));
	const char * id = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
	char container[PATH_MAX];
	char tasks[PATH_MAX];
	char prefix[PATH_MAX];
	char contents[64];
	ck_assert_int_lt(snprintf(container, sizeof(container), "%s/docker-%s.scope", directory, id), (int)sizeof(container));
	ck_assert_int_lt(snprintf(tasks, sizeof(tasks), "%s/%s", container, CGRP_PIDS), (int)sizeof(tasks));
	ck_assert_int_lt(snprintf(prefix, sizeof(prefix), "%s/", directory), (int)sizeof(prefix));
	ck_assert_int_lt(snprintf(contents, sizeof(contents), "%d\n%d\n", getpid(), getppid()), (int)sizeof(contents));
	ck_assert_int_eq(0, mkdir(container, S_IRWXU));
	updateTestWriteFile(tasks, contents);

	free(prgset->cpusetdfileprefix);
	prgset->cpusetdfileprefix = strdup(prefix);
	node_t * found = NULL;
	getContPids(&found);

	int count = 0;
	for (node_t * item=found; item; item=item->next){
		ck_assert(item->pid == getpid() || item->pid == getppid());
		ck_assert_str_eq(id, item->contid);
		ck_assert_int_ne(0, item->status & MSK_STATSIBL);
		count++;
	}
	ck_assert_int_eq(2, count);

	while (found)
		node_pop(&found);
	ck_assert_int_lt(snprintf(contents, sizeof(contents), "%d\n", getpid()), (int)sizeof(contents));
	updateTestWriteFile(tasks, contents);
	getContPids(&found);
	ck_assert_ptr_nonnull(found);
	ck_assert_ptr_null(found->next);
	ck_assert_int_eq(getpid(), found->pid);
	ck_assert_int_eq(0, found->status & MSK_STATSIBL);
	node_pop(&found);

	ck_assert_int_eq(0, unlink(tasks));
	ck_assert_int_eq(0, rmdir(container));
	ck_assert_int_eq(0, rmdir(directory));

	getContPids(&found);
	ck_assert_ptr_null(found);
	ck_assert_int_eq(DM_CNTPID, prgset->use_cgroup);
}
END_TEST

/// TEST CASE -> parse parent-based discovery using controlled pidof and ps output
/// EXPECTED -> PPIDs are retained as container IDs for every discovered task
START_TEST(orchestrator_update_getparentpids)
{
	char directory[] = "/tmp/update-parent-XXXXXX";
	ck_assert_ptr_nonnull(mkdtemp(directory));
	char pidof[PATH_MAX];
	char ps[PATH_MAX];
	char path[PATH_MAX * 2];
	ck_assert_int_lt(snprintf(pidof, sizeof(pidof), "%s/pidof", directory), (int)sizeof(pidof));
	ck_assert_int_lt(snprintf(ps, sizeof(ps), "%s/ps", directory), (int)sizeof(ps));
	updateTestWriteExecutable(pidof, "#!/bin/sh\nprintf '1234 5678\\n'\n");
	updateTestWriteExecutable(ps, "#!/bin/sh\nprintf '111 1234 helper-one\\n222 5678 helper-two\\n'\n");

	const char * oldPath = getenv("PATH");
	char * savedPath = strdup(oldPath ? oldPath : "");
	ck_assert_ptr_nonnull(savedPath);
	ck_assert_int_lt(snprintf(path, sizeof(path), "%s:%s", directory, savedPath), (int)sizeof(path));
	ck_assert_int_eq(0, setenv("PATH", path, 1));
	free(prgset->cont_ppidc);
	prgset->cont_ppidc = strdup("fake-shim");

	node_t * found = NULL;
	getParentPids(&found);
	ck_assert_ptr_nonnull(found);
	ck_assert_ptr_nonnull(found->next);
	ck_assert_ptr_null(found->next->next);
	ck_assert_int_eq(222, found->pid);
	ck_assert_str_eq("5678", found->contid);
	ck_assert_int_eq(111, found->next->pid);
	ck_assert_str_eq("1234", found->next->contid);
	ck_assert_int_ne(0, found->status & MSK_STATSIBL);
	ck_assert_int_ne(0, found->next->status & MSK_STATSIBL);

	while (found)
		node_pop(&found);
	ck_assert_int_eq(0, setenv("PATH", savedPath, 1));
	free(savedPath);
	ck_assert_int_eq(0, unlink(pidof));
	ck_assert_int_eq(0, unlink(ps));
	ck_assert_int_eq(0, rmdir(directory));
}
END_TEST

/// TEST CASE -> test detected pid list using pid signture and ps
/// EXPECTED -> 3 elements detectes (and no leaks!)
START_TEST(orchestrator_update_getpids)
{
	// TODO: extend with shim and subprocess examples
	pid_t pid1, pid2, pid3;
	FILE * fd1, * fd2,  * fd3;

	char pid[CMD_LEN];

	// create pids
	fd1 = popen2("sleep 4", "r", &pid1);
	fd2 = popen2("sleep 3", "r", &pid2);
	fd3 = popen2("sleep 5", "r", &pid3);
	// set detect mode to pid
	free (prgset->cont_pidc);
	prgset->cont_pidc = strdup("sleep");
#ifdef BUSYBOX
	(void)sprintf(pid, "| grep -E '%s'", prgset->cont_pidc);
#else
	(void)sprintf(pid, "-C %s", prgset->cont_pidc);
#endif
	usleep(100000); // wait for process creation // yield

	selectUpdate();

	getPids(&nhead, pid, NULL);

	// verify 2 nodes exist
	ck_assert(nhead);
	ck_assert(nhead->next);
	ck_assert(nhead->next->next);
	ck_assert(!nhead->next->next->next);

	// verify pids, no container assigned
	ck_assert_int_eq(nhead->next->next->pid, pid1);
	ck_assert_int_eq(nhead->next->pid, pid2);
	ck_assert_int_eq(nhead->pid, pid3);
	ck_assert_ptr_null(nhead->contid);
	ck_assert_ptr_null(nhead->next->contid);
	ck_assert_ptr_null(nhead->next->next->contid);

	while (nhead)
		node_pop(&nhead);

	// retry with the actual parent PID, should be stored as container ID
	char ppid[10];
	(void)sprintf(ppid, "%d", getpid());
	getPids(&nhead, pid, ppid);
	int count = 0;
	for (node_t * item = nhead; item; item=item->next){
		ck_assert_str_eq(item->contid, ppid);
		count++;
	}
	ck_assert_int_eq(count, 3);

	pclose2(fd1, pid1, SIGINT); // close pipe
	pclose2(fd2, pid2, SIGINT); // close pipe
	pclose2(fd3, pid3, SIGINT); // close pipe
}
END_TEST

/// TEST CASE -> test insert/remove from list
/// EXPECTED -> 3 elements detected, than 1 removes, than 1 inserted
START_TEST(orchestrator_update_scannew)
{
	// TODO: extend with shim and subprocess examples
	pid_t pid1, pid2, pid3;
	FILE * fd1, * fd2,  * fd3;

	// create pids
	fd1 = popen2("sleep 4", "r", &pid1);
	fd2 = popen2("sleep 2", "r", &pid2);
	fd3 = popen2("sleep 5", "r", &pid3);
	// set detect mode to pid
	free (prgset->cont_pidc);
	prgset->cont_pidc = strdup("sleep");
	prgset->use_cgroup = DM_CMDLINE;

	selectUpdate();

	usleep(100000); // wait for process creation // yield
	scanNew();

	// verify 3 nodes exist
	ck_assert(nhead);
	ck_assert(nhead->next);
	ck_assert(nhead->next->next);
	ck_assert(!nhead->next->next->next);

	// verify pids
	ck_assert_int_eq(nhead->next->next->pid, pid1);
	ck_assert_int_eq(nhead->next->pid, pid2);
	ck_assert_int_eq(nhead->pid, pid3);


	pclose2(fd2, pid2, SIGINT); // send SIGINT = CTRL+C to sleep instances

	scanNew();

	// verify PIDs
	ck_assert_int_eq(nhead->next->pid, pid1);
	ck_assert_int_eq(nhead->pid, pid3);

	fd2 = popen2("sleep 3", "r", &pid2);
	usleep(100000); // wait for process creation // yield

	scanNew();

	// verify 3 nodes exist
	ck_assert(nhead);
	ck_assert(nhead->next);
	ck_assert(nhead->next->next);
	ck_assert(!nhead->next->next->next);

	// verify pids
	ck_assert_int_eq(nhead->next->next->pid, pid1);
	ck_assert_int_eq(nhead->next->pid, pid3);
	ck_assert_int_eq(nhead->pid, pid2);


	pclose2(fd1, pid1, SIGINT); // close pipe
	pclose2(fd2, pid2, SIGINT); // close pipe
	pclose2(fd3, pid3, SIGINT); // close pipe
}
END_TEST

/// TEST CASE -> retain missing PIDs and reactivate them when tracking is enabled
/// EXPECTED -> missing entries become negative and return as fresh positive entries
START_TEST(orchestrator_update_scantracked)
{
	prgset->trackpids = 1;
	pidUpdate = updateTestGetPids;
	pid_t initial[] = { 10, 20, 30 };
	for (size_t i=0; i<sizeof(initial)/sizeof(initial[0]); i++){
		node_push(&nhead);
		nhead->pid = initial[i];
		nhead->attr.sched_policy = SCHED_OTHER;
		nhead->psig = strdup("update-test");
	}

	updateTestPids[0] = 30;
	updateTestPids[1] = 10;
	updateTestPidCount = 2;
	scanNew();
	ck_assert_int_eq(30, nhead->pid);
	ck_assert_int_eq(-20, nhead->next->pid);
	ck_assert_int_eq(10, nhead->next->next->pid);

	updateTestPids[0] = 30;
	updateTestPids[1] = 20;
	updateTestPids[2] = 10;
	updateTestPidCount = 3;
	scanNew();
	ck_assert_int_eq(30, nhead->pid);
	ck_assert_int_eq(20, nhead->next->pid);
	ck_assert_int_eq(10, nhead->next->next->pid);

	updateTestPidCount = 0;
	scanNew();
	ck_assert_int_eq(-30, nhead->pid);
	ck_assert_int_eq(-20, nhead->next->pid);
	ck_assert_int_eq(-10, nhead->next->next->pid);
}
END_TEST

/// TEST CASE -> fill link event structure and test passing/parameters
/// EXPECTED ->  resources set and all freed
START_TEST(orchestrator_update_dlinkread)
{
	updateTestSetEvent(cnt_add, "1232144314");
	pidUpdate = updateTestGetPids;
	updateDocker();

	ck_assert_ptr_null(containerEvent);
	ck_assert_ptr_null(lstevent);
	ck_assert_ptr_null(contparm->cont);
}
END_TEST

/// TEST CASE -> process Docker removal events with and without PID tracking
/// EXPECTED -> matching PIDs are deleted or retained as inactive respectively
START_TEST(orchestrator_update_dlinkremove)
{
	pidUpdate = updateTestGetPids;
	updateTestPids[0] = 10;
	updateTestPidCount = 1;

	node_push(&nhead);
	nhead->pid = 10;
	nhead->contid = strdup("keep");
	node_push(&nhead);
	nhead->pid = 20;
	nhead->contid = strdup("remove");
	updateTestSetEvent(cnt_remove, "remove");
	updateDocker();
	ck_assert_int_eq(10, nhead->pid);
	ck_assert_ptr_null(nhead->next);

	while (nhead)
		node_pop(&nhead);
	prgset->trackpids = 1;
	node_push(&nhead);
	nhead->pid = 10;
	nhead->contid = strdup("keep");
	node_push(&nhead);
	nhead->pid = 20;
	nhead->contid = strdup("remove");
	updateTestSetEvent(cnt_remove, "remove");
	updateDocker();
	ck_assert_int_eq(-20, nhead->pid);
	ck_assert_int_eq(10, nhead->next->pid);
	ck_assert_ptr_null(nhead->next->next);
}
END_TEST

/// TEST CASE -> discard a pending Docker event
/// EXPECTED -> event ownership is released without creating PID entries
START_TEST(orchestrator_update_dlinkpending)
{
	pidUpdate = updateTestGetPids;
	updateTestSetEvent(cnt_pending, "pending");
	updateDocker();
	ck_assert_ptr_null(containerEvent);
	ck_assert_ptr_null(lstevent);
	ck_assert_ptr_null(nhead);
}
END_TEST

/// TEST CASE -> reject invalid real-time update-thread scheduler parameters
/// EXPECTED -> failed FIFO and Deadline requests fall back to SCHED_OTHER
START_TEST(orchestrator_update_threadparams)
{
	prgset->policy = SCHED_FIFO;
	prgset->priority = 0;
	setThreadParameters();
	ck_assert_int_eq(SCHED_OTHER, prgset->policy);

	prgset->policy = SCHED_DEADLINE;
	prgset->update_wcet = 0;
	setThreadParameters();
	ck_assert_int_eq(SCHED_OTHER, prgset->policy);

	prgset->policy = SCHED_BATCH;
	prgset->priority = 10;
	setThreadParameters();
	ck_assert_int_eq(SCHED_BATCH, prgset->policy);
}
END_TEST


/// TEST CASE -> Stop update thread when setting status to -1
/// EXPECTED -> exit after 2 seconds, no error
START_TEST(orchestrator_update_stop)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 1;

	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(2);
//	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end

	node_pop(&nhead);
}
END_TEST

/// TEST CASE -> test detected pid list
/// EXPECTED -> 3 elements at first, then two with one deleted, desc order
START_TEST(orchestrator_update_findprocs)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 1;

	pid_t pid1, pid2, pid3;
	FILE * fd1, * fd2,  * fd3;

	// create pids
	fd1 = popen2("sleep 4", "r", &pid1);
	fd2 = popen2("sleep 2", "r", &pid2);
	fd3 = popen2("sleep 5", "r", &pid3);
	// set detect mode to pid 
	free (prgset->cont_pidc);
	prgset->cont_pidc = strdup("sleep");
	prgset->use_cgroup = DM_CMDLINE;
	prgset->loops = 5; // shorten scan time
	
	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(1);

	// verify 2 nodes exist
	ck_assert(nhead);
	ck_assert(nhead->next);
	ck_assert(nhead->next->next);
	ck_assert(!nhead->next->next->next);

	// verify pids
	ck_assert_int_eq(nhead->next->next->pid, pid1);
	ck_assert_int_eq(nhead->next->pid, pid2);
	ck_assert_int_eq(nhead->pid, pid3);

	pclose2(fd2, pid2, SIGINT); // send SIGINT = CTRL+C to sleep instances
	sleep(1);

	// verify PIDs
	ck_assert_int_eq(nhead->next->pid, pid1);
	ck_assert_int_eq(nhead->pid, pid3);

	pclose2(fd1, pid1, SIGINT); // close pipe
	pclose2(fd3, pid3, SIGINT); // close pipe

	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

/// TEST CASE -> test will all pids on machine
/// EXPECTED -> adding and removing of pidof sequences
START_TEST(orchestrator_update_findprocsall)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 1;

	// set detect mode to pid 
	free (prgset->cont_pidc);
	prgset->cont_pidc = strdup(""); // all!
	prgset->use_cgroup = DM_CMDLINE;
	prgset->loops = 5; // shorten scan time

	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(3); // 4 seconds timeout

	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end
}
END_TEST

/// TEST CASE -> test assign resources
/// EXPECTED -> resources should match settings
START_TEST(orchestrator_update_rscs)
{	
	pthread_t thread1;
	int  iret1;
	int stat1 = 1;
	pid_t pid1, pid2;
	FILE * fd1, * fd2;

	// create pids
	fd1 = popen2("sleep 4", "r", &pid1);
	fd2 = popen2("sleep 5", "r", &pid2);
	// set detect mode to pid 
	free (prgset->cont_pidc);
	prgset->cont_pidc = strdup("sleep");
	prgset->use_cgroup = DM_CMDLINE;
	prgset->loops = 5; // shorten scan time

	// push sig to config	
	contparm->rscs = malloc (sizeof(struct sched_rscs));
	contparm->rscs->affinity=0;
	contparm->rscs->affinity_mask = parse_cpumask("0");
	contparm->rscs->rt_timew=95000;
	contparm->rscs->rt_time=100000;
	contparm->rscs->mem_dataw=100;
	contparm->rscs->mem_data=-1;

	contparm->attr = calloc (1, sizeof(struct sched_attr));
	contparm->attr->size =SCHED_ATTR_SIZE;
	contparm->attr->sched_policy=SCHED_BATCH;
	contparm->attr->sched_flags=SCHED_FLAG_RESET_ON_FORK;
	contparm->attr->sched_nice=5;
	contparm->attr->sched_priority=0;
	contparm->attr->sched_runtime=0;
	contparm->attr->sched_deadline=0;
	contparm->attr->sched_period=0;

	const char *pids[] = {	"sleep",
							NULL };

	const char ** pidsig = pids;
	while (*pidsig) {
		// new pid
		push((void**)&contparm->pids, sizeof(pidc_t));
		contparm->pids->psig = strdup(*pidsig);
		contparm->pids->attr = contparm->attr;
		contparm->pids->rscs = contparm->rscs;
		contparm->pids->status |= MSK_STATSHAT | MSK_STATSHRC;
		pidsig++;
	}


	iret1 = pthread_create( &thread1, NULL, thread_update, (void*) &stat1);
	ck_assert_int_eq(iret1, 0);

	sleep(1);

	// verify 2 nodes exist
	ck_assert(nhead);
	ck_assert(nhead->next);
	ck_assert(!nhead->next->next);

	// verify pids
	ck_assert_int_eq(nhead->next->pid, pid1);
	ck_assert_int_eq(nhead->pid, pid2);

	// ipdate-> the pid cfg is cloned for pids unrelated to container configs
	ck_assert(contparm->pids->next);
	ck_assert(contparm->pids->next->next);
	ck_assert_ptr_eq(nhead->param, contparm->pids->next);
	ck_assert_ptr_eq(nhead->next->param, contparm->pids);

	{
		struct rlimit rlim;		
		// RT-Time limit
		if (prlimit(pid1, RLIMIT_RTTIME, NULL, &rlim))
			err_msg_n(errno, "getting RT-Limit for PID %d", pid1);

		ck_assert_int_eq(contparm->pids->next->rscs->rt_timew, rlim.rlim_cur);
		ck_assert_int_eq(contparm->pids->next->rscs->rt_time,  rlim.rlim_max);

		if (prlimit(pid1, RLIMIT_DATA, NULL, &rlim))
			err_msg_n(errno, "getting data-Limit for PID %d", pid1);

		ck_assert_int_eq(contparm->pids->next->rscs->mem_dataw, rlim.rlim_cur);
		ck_assert_int_eq(contparm->pids->next->rscs->mem_data,  rlim.rlim_max);


		if (prlimit(pid2, RLIMIT_RTTIME, NULL, &rlim))
			err_msg_n(errno, "getting RT-Limit for PID %d", pid2);

		ck_assert_int_eq(contparm->pids->rscs->rt_timew, rlim.rlim_cur);
		ck_assert_int_eq(contparm->pids->rscs->rt_time,  rlim.rlim_max);

		if (prlimit(pid2, RLIMIT_DATA, NULL, &rlim))
			err_msg_n(errno, "getting data-Limit for PID %d", pid2);

		ck_assert_int_eq(contparm->pids->rscs->mem_dataw, rlim.rlim_cur);
		ck_assert_int_eq(contparm->pids->rscs->mem_data,  rlim.rlim_max);
	}

	{
		struct sched_attr attr;
		if (sched_getattr (pid1, &(attr), sizeof(struct sched_attr), 0U) != 0) 
			warn("Unable to read params for PID %d: %s", pid1, strerror(errno));

		ck_assert(!memcmp(&attr, contparm->pids->next->attr, 22 )); // starting 6.12, EEVDF scheduler fills unused

		if (sched_getattr (pid2, &(attr), sizeof(struct sched_attr), 0U) != 0) 
			warn("Unable to read params for PID %d: %s", pid2, strerror(errno));		

		ck_assert(!memcmp(&attr, contparm->pids->attr, 22)); // starting 6.12, EEVDF scheduler fills unused

	}
	pclose2(fd1, pid1, SIGINT); // close pipe
	pclose2(fd2, pid2, SIGINT); // close pipe

	// set stop sig
	stat1 = -1;

	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL); // wait until end

}
END_TEST

void orchestrator_update (Suite * s) {
	TCase *tc1 = tcase_create("update_newread");
	tcase_add_checked_fixture(tc1, orchestrator_update_setup, orchestrator_update_teardown);
	tcase_add_test(tc1, orchestrator_update_select);
	tcase_add_test(tc1, orchestrator_update_getcontpids);
	tcase_add_test(tc1, orchestrator_update_getparentpids);
	tcase_add_test(tc1, orchestrator_update_getpids);
	tcase_add_test(tc1, orchestrator_update_scannew);
	tcase_add_test(tc1, orchestrator_update_scantracked);
	tcase_add_test(tc1, orchestrator_update_dlinkread);
	tcase_add_test(tc1, orchestrator_update_dlinkremove);
	tcase_add_test(tc1, orchestrator_update_dlinkpending);
	tcase_add_test(tc1, orchestrator_update_threadparams);

	suite_add_tcase(s, tc1);

    TCase *tc2 = tcase_create("update_thread");
	tcase_add_checked_fixture(tc2, orchestrator_update_setup, orchestrator_update_teardown);
	tcase_add_exit_test(tc2, orchestrator_update_stop, EXIT_SUCCESS);
	tcase_add_test(tc2, orchestrator_update_findprocs);
	tcase_add_test(tc2, orchestrator_update_findprocsall);

    suite_add_tcase(s, tc2);

	TCase *tc3 = tcase_create("update_thread_resources");
	tcase_add_checked_fixture(tc3, orchestrator_update_setup, orchestrator_update_teardown);
	tcase_add_test(tc3, orchestrator_update_rscs);

    suite_add_tcase(s, tc3);

	return;
}
