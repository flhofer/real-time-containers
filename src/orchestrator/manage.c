#include "manage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // used for string parsing
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants

#include <sched.h>			// scheduler functions
#include <linux/sched.h>	// Linux specific scheduling
#include <linux/types.h>	// data structure types, short names and linked list
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <fcntl.h>			// file control, new open/close functions
#include <dirent.h>			// directory entry structure and exploration
#include <errno.h>			// error numbers and strings

// Custom includes
#include "orchestrator.h"

#include "orchdata.h"	// memory structure to store information
#include "kernutil.h"	// generic kernel utilities
#include "error.h"		// error and stderr print functions
#include "cmnutil.h"	// common definitions and functions
#include "kbuffer.h"	// ring-buffer management from trace-event
#include "resmgnt.h"	// PID and resource management

#include <numa.h>		// NUMA node identification

#undef PFX
#define PFX "[manage] "

#define PIPE_BUFFER			4096

// total scan counter for update-stats
static uint64_t scount = 0; // total scan count

// #################################### THREAD configuration specific ############################################

// Linked list of event configurations and handlers
struct ftrace_elist {
	struct ftrace_elist * next;
	char* event;	// string identifier
	int eventid;	// event kernel ID
	int (*eventcall)(void *, uint64_t); // event elaboration function
};
struct ftrace_elist * elist_head;

struct ftrace_thread * elist_thead = NULL;

struct tr_common {
	uint16_t common_type;
	uint8_t common_flags;
	uint8_t common_preempt_count;
	int32_t common_pid;
};

struct tr_wakeup {
	char comm[16]; //24 - 16
	pid_t pid; // 28 - 20
	int32_t prio; // 32 - 24
	int32_t success; // 36 - 28
	int32_t target_cpu; // 40 - 32
};

struct tr_switch {
	char prev_comm[16]; //24 - 16
	pid_t prev_pid; // 28 - 20
	int32_t prev_prio; // 32 - 24
	int64_t prev_state; // 40 - 32

	char next_comm[16]; // 56 - 48
	pid_t next_pid; // 60 - 52
	int32_t next_prio; // 64 - 56
};

struct tr_runtime {
	char comm[16]; //24 - 16
	pid_t pid; // 28 - 20
	uint32_t dummy  ; // 32 - 24 - alignment filler
	uint64_t runtime; // 40 - 32
	uint64_t vruntime; // 48 - 40
	// filled with 0's to 52
};

// signal to keep status of triggers ext SIG
static volatile sig_atomic_t ftrace_stop;

void *thread_ftrace(void *arg);

// functions to elaborate data for tracer frames
static int pickPidInfoW(void * addr, uint64_t ts);
static int pickPidInfoS(void * addr, uint64_t ts);
static int pickPidInfoR(void * addr, uint64_t ts);

/// ftrace_inthand(): interrupt handler for infinite while loop, help
/// this function is called from outside, interrupt handling routine
/// Arguments: - signal number of interrupt calling
///
/// Return value: -
static void ftrace_inthand (int sig, siginfo_t *siginfo, void *context){
	ftrace_stop = 1;
}

void buildEventConf(){
	push((void**)&elist_head, sizeof(struct ftrace_elist));
	elist_head->eventid = 305;
	elist_head->event = "sched_stat_runtime";
	elist_head->eventcall = pickPidInfoR;

	push((void**)&elist_head, sizeof(struct ftrace_elist));
	elist_head->eventid = 319;
	elist_head->event = "sched_wakeup";
	elist_head->eventcall = pickPidInfoW;


	push((void**)&elist_head, sizeof(struct ftrace_elist));
	elist_head->eventid = 317;
	elist_head->event = "sched_switch";
	elist_head->eventcall = pickPidInfoS;
}

void clearEventConf(){
	while (elist_head)
		pop((void**)&elist_head);
}



static int
appendEvent(char * dbgpfx, char * event, void* fun ){

	char path[_POSIX_PATH_MAX];
	(void)sprintf(path, "%sevents/%s/", dbgpfx, event);

	// maybe put it in a function??
	if (0 < setkernvar(path, "enable", "0", prgset->dryrun)) {
		push((void**)&elist_head, sizeof(struct ftrace_elist));
		{
			char val[5];
			if ( 0 < getkernvar(path, "id", val, 5))
				elist_head->eventid = atoi(val);
			else{
				warn("Unable to get event id '%s'", event);
				return -1;
			}
		}
		elist_head->event = strdup(event);
		elist_head->eventcall = fun;
		return 0;
	}

	warn("Unable to set event for '%s'", event);

	return -1;
}

/// configureTracers(): setup kernel function trace system
///
/// Arguments: - none
///
/// Return value: 0 = success, else error
///
static int configureTracers(){

	char * dbgpfx = get_debugfileprefix();

	if (!dbgpfx)
		return -1;

	if ( 0 > setkernvar(dbgpfx, "tracing_on", "0", prgset->dryrun))
		warn("Can not disable kernel function tracing");

	if ( 0 > setkernvar(dbgpfx, "events/enable", "0", prgset->dryrun))
		warn("Unable to clear kernel fTrace event list");

	{ // get CPU-set in hex for tracing
		char trcpuset[129]; // enough for 512 CPUs
		if (parse_bitmask_hex(prgset->affinity_mask, trcpuset, sizeof(trcpuset)))
			if (0> setkernvar(dbgpfx, "tracing_cpumask", trcpuset, prgset->dryrun) )
				warn("Unable to set tracing CPU-set");
	}

	// Setup and enabling of events must work
/* FIXME: it seems that I only need sched_switch
	// sched_stat_runtime tracer seems to need sched_stats
	if (0> setkernvar(prgset->procfileprefix, "sched_schedstats", "1", prgset->dryrun) )
		warn("Unable to activate schedstat probe");

	if ((appendEvent(dbgpfx, "sched/sched_stat_runtime", pickPidInfoR))
		|| (appendEvent(dbgpfx, "sched/sched_wakeup", sched_wakeup))
*/
	if ((appendEvent(dbgpfx, "sched/sched_switch", pickPidInfoS)))
		return -1;

	if ( 0 > setkernvar(dbgpfx, "tracing_on", "1", prgset->dryrun)){
		warn("Can not enable kernel function tracing");
		return -1;
	}

	return 0; // setup successful?
}

/// resetTracers(): reset kernel function trace system
///
/// Arguments: - none
///
/// Return value: 0 = success, else error
///
static void resetTracers(){
	char * dbgpfx = get_debugfileprefix();

	if ( 0 > setkernvar(dbgpfx, "tracing_on", "0", prgset->dryrun))
		warn("Can not disable kernel function tracing");

	if (0 > setkernvar(dbgpfx, "events/enable", "0", prgset->dryrun))
		warn("Unable to clear kernel fTrace event list");

	// sched_stat_runtime tracer seems to need sched_stats
	if (0 > setkernvar(prgset->procfileprefix, "sched_schedstats", "0", prgset->dryrun) )
		warn("Unable to deactivate schedstat probe");

	while (elist_head)
		pop((void**)&elist_head);
}

/// startTraceRead(): start CPU tracing threads
///
/// Arguments:
///
/// Return value: OR-result of pthread_create, negative if one failed
///
static int startTraceRead() {

	int maxcpu = prgset->affinity_mask->size;
	int ret = 0;
	// loop through, bit set = start a thread and store in ll
	for (int i=0;i<maxcpu;i++)
		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by active
			push((void**)&elist_thead, sizeof(struct ftrace_elist));
			elist_thead->cpuno = i;
			elist_thead->dbgfile = NULL;
			elist_thead->iret = pthread_create( &elist_thead->thread, NULL, thread_ftrace, elist_thead);
#ifdef DEBUG
			char tname [17]; // 16 char length restriction
			(void)sprintf(tname, "manage_ftCPU%d", elist_thead->cpuno); // space for 4 digit CPU number
			(void)pthread_setname_np(elist_thead->thread, tname);
#endif
			ret |= elist_thead->iret; // combine results in OR to detect one failing
		}

	return ret; // = 0 if OK, else negative
}

/// stopTraceRead(): stop CPU tracing threads
///
/// Arguments:
///
/// Return value: OR-result of pthread_*, negative if one failed
///
static int stopTraceRead() {

	int ret = 0;
	// loop through, existing list elements, and join
	while ((elist_thead))
		if (!elist_thead->iret) { // thread started successfully

			int ret1 = pthread_kill (elist_thead->thread, SIGQUIT); // tell threads to stop
			if (ret1)
				perror("Failed to send signal to fTrace thread");
			ret |= ret1; // combine results in OR to detect one failing

			ret1 = pthread_join( elist_thead->thread, NULL); // wait until end
			if (ret1)
				perror("Could not join with fTrace thread");
			ret |= ret1; // combine results in OR to detect one failing

			free(elist_thead->dbgfile); // free it if defined
			pop((void**)&elist_thead);
		}
	return ret; // >= 0 if OK, else negative
}

// #################################### THREAD specific ############################################

static void pickPidCons(node_t *item, uint64_t ts){

	// -> what if we read the debug output here??

	if (SCHED_DEADLINE == item->attr.sched_policy)
		// did we skip a deadline update? TODO: check if we can sync it
		while (item->mon.dl_deadline < ts)
			// TODO: what if sched_deadline is not set?
			item->mon.dl_deadline += MAX( item->attr.sched_period, 1000); // safety..

	item->mon.dl_diff = item->mon.dl_deadline - item->mon.dl_rt;
	item->mon.dl_diffmin = MIN (item->mon.dl_diffmin, item->mon.dl_diff);
	item->mon.dl_diffmax = MAX (item->mon.dl_diffmax, item->mon.dl_diff);

	item->mon.rt_min = MIN (item->mon.rt_min, item->mon.dl_rt);
	item->mon.rt_max = MAX (item->mon.rt_max, item->mon.dl_rt);
	item->mon.rt_avg = (item->mon.rt_avg * 9 + item->mon.dl_rt /* *1 */)/10;

	item->mon.dl_rt = 0;

}

/// pickPidCommon(): process PID fTrace common
///
/// Arguments: - item to update with statistics
///			   - frame containing the runtime info
///			   - last time stamp
///
/// Return value: error code, 0 = success
///
static int pickPidCommon(void * addr, uint64_t ts) {
	struct tr_common *pFrame = (struct tr_common*)addr;

	printDbg( "[%lu.%09lu] type=%u flags=%u preempt=%u pid=%d\n", ts/1000000000, ts%1000000000,
			pFrame->common_type, pFrame->common_flags, pFrame->common_preempt_count, pFrame->common_pid);

	if (pFrame->common_flags != 1)
		{
		// print the flag found, if different from 1
		(void)printf("FLAG DEVIATION! %d, %x\n", pFrame->common_flags, pFrame->common_flags);
		}

	return sizeof(*pFrame); // TODO: not always the case!! +8; // TODO 8 zeros?
}

/// pickPidInfoW(): process PID fTrace sched_wakeup
///					update data with kernel tracer debug out
///
/// Arguments: - item to update with statistics
///			   - frame containing the runtime info
///			   - last time stamp
///
/// Return value: error code, 0 = success
///
static int pickPidInfoW(void * addr, uint64_t ts) {

	int ret1 = pickPidCommon(addr, ts);
	addr+= ret1;

	struct tr_wakeup *pFrame = (struct tr_wakeup*)addr;


	printDbg("    comm=%s pid=%d prio=%d target_cpu=%03d\n",
			pFrame->comm, pFrame->pid, pFrame->prio, pFrame->target_cpu);

	return ret1 + sizeof(struct tr_wakeup);
}

/// pickPidInfoS(): process PID fTrace sched_switch
///					update data with kernel tracer debug out
///
/// Arguments: - item to update with statistics
///			   - frame containing the runtime info
///			   - last time stamp
///
/// Return value: error code, 0 = success
///
static int pickPidInfoS(void * addr, uint64_t ts) {

	int ret1 = pickPidCommon(addr, ts);
	addr+= ret1;

	struct tr_switch *pFrame = (struct tr_switch*)addr;


	printDbg("    prev_comm=%s prev_pid=%d prev_prio=%d prev_state=%ld ==> next_comm=%s next_pid=%d next_prio=%d\n",
				pFrame->prev_comm, pFrame->prev_pid, pFrame->prev_prio, pFrame->prev_state,
//				(pFrame->prev_state & ((((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) - 1))
//				? __print_flags(pFrame->prev_state & ((((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) - 1),"|", { 0x0001, "S" }, { 0x0002, "D" }, { 0x0004, "T" }, { 0x0008, "t" }, { 0x0010, "X" }, { 0x0020, "Z" }, { 0x0040, "P" }, { 0x0080, "I" }) : "R",
//						pFrame->prev_state & (((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) ? "+" : "",
						pFrame->next_comm, pFrame->next_pid, pFrame->next_prio);

	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	// working item
	node_t * item = NULL;

	// find previous pid switching from
	for (node_t * citem = nhead; ((citem)); citem=citem->next )
		// skip deactivated tracking items
		if (abs(citem->pid)==pFrame->prev_pid){
			item = citem;
			break;
		}

	// PID in list, update data
	if (item){
		// compute runtime - limit between 1ns and 1 sec
		item->mon.dl_rt += MAX(MIN(ts - item->mon.last_ts, (double)NSEC_PER_SEC), 1);

		// TODO: call only if Need-Resched is set
		{
			// check statistics and add value
			if (!(item->mon.pdf_hist)){
				// base for histogram, runtime parameter
				double b = (double)item->attr.sched_runtime;
				// --, try prefix if none loaded
				if (!b && item->param && item->param->attr)
					b = (double)item->param->attr->sched_runtime;
				// -- fall-back to last runtime
				if (!b)
					b = (double)item->mon.dl_rt; // at least 1ns

				if ((runstats_inithist(&(item->mon.pdf_hist), b/(double)NSEC_PER_SEC)))
					warn("Curve fitting parameter init failure for PID %d", item->pid);
			}

			double b = (double)item->mon.dl_rt/NSEC_PER_SEC; // transform to sec
			int ret;
			if ((ret = runstats_addhist(item->mon.pdf_hist, b)))
				if (ret != 1) // GSL_EDOM
					warn("Curve fitting histogram increment error for PID %d", item->pid);

			// consolidate other values
			pickPidCons(item, ts);
		}
	}

	// find mext pid and put ts
	for (node_t * citem = nhead; ((citem)); citem=citem->next )
		// skip deactivated tracking items
		if (abs(citem->pid)==pFrame->next_pid){
			citem->mon.last_ts = ts;
			break;
		}

	(void)pthread_mutex_unlock(&dataMutex);

	return ret1 + sizeof(struct tr_switch);
}

/// pickPidInfoR(): process PID fTrace sched_stat_runtime
///					update data with kernel tracer debug out
///
/// Arguments: - item to update with statistics
///			   - frame containing the runtime info
///			   - last time stamp
///
/// Return value: error code, 0 = success
///
static int pickPidInfoR(void * addr, uint64_t ts) {

	struct tr_common *pcFrame = (struct tr_common*)addr;

	int ret1 = pickPidCommon( addr, ts);
	addr+= ret1;

	struct tr_runtime *pFrame = (struct tr_runtime*)addr;


	printDbg( "    comm=%s pid=%d runtime=%lu [ns] vruntime=%lu [ns]\n",
			pFrame->comm, pFrame->pid, pFrame->runtime, pFrame->vruntime);

	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	// working item
	node_t * item = NULL;
	// for now does only a simple update of runtime
	for (node_t * citem = nhead; ((citem)); citem=citem->next )
		// skip deactivated tracking items
		if (abs(citem->pid)==pFrame->pid){
			item = citem;
			break;
		}

	if (item && item->pid > 0) {
		item->mon.dl_rt += pFrame->runtime;
		item->mon.last_ts = ts;
		item->mon.dl_scanfail += pcFrame->common_preempt_count;
		item->mon.dl_count++;
	}

	(void)pthread_mutex_unlock(&dataMutex);

	return ret1 + sizeof(struct tr_runtime);
}

/// thread_ftrace(): parse kernel tracer output
///
/// Arguments: status trace
///
/// Return value: error code, 0 = success
///
void *thread_ftrace(void *arg){

	int pstate = 0;
	int ret = 0;
	FILE *fp = NULL;
	const struct ftrace_thread * fthread = (struct ftrace_thread *)arg;

	unsigned char buffer[PIPE_BUFFER];
	void *pEvent;
	struct kbuffer * kbuf; // kernel ring buffer structure
	unsigned long long timestamp; // event time stamp, based on up-time in ns (using long long for compatibility kbuffer library)

	{ // setup interrupt handler block
		struct sigaction act;

		/* Use the sa_sigaction field because the handles has two additional parameters */
		/* The SA_SIGINFO flag tells sigaction() to use the sa_sigaction field, not sa_handler. */
		act.sa_handler = NULL; // On some architectures ---
		act.sa_sigaction = &ftrace_inthand; // these are a union, do not assign both, -> first set null, then value
		act.sa_flags = SA_SIGINFO;

		/* blocking signal set */
		(void)sigemptyset(&act.sa_mask);

		act.sa_restorer = NULL;

		if (sigaction(SIGQUIT, &act, NULL) < 0)		 // quit from caller
		{
			perror ("Setup of sigaction failed");
			exit(EXIT_FAILURE); // exit the software, not working
		}
	} // END interrupt handler block

	{
		 sigset_t set;
		/* Block all signals except SIGQUIT */
		if ( ((sigfillset(&set)))
			|| ((sigdelset(&set, SIGQUIT)))
			|| (0 != pthread_sigmask(SIG_BLOCK, &set, NULL))){
			perror ("Setup of sigmask failed");
			exit(EXIT_FAILURE); // exit the software, not working
		}
	}

	while(1) {

		switch( pstate )
		{
		case 0:
			// init buffer structure for page management
			kbuf = kbuffer_alloc(KBUFFER_LSIZE_8, KBUFFER_ENDIAN_LITTLE); // FIXME: static setting

			char* fn;
			if (NULL != fthread->dbgfile)
				fn = fthread->dbgfile;
			else{
				fn = malloc(100);
				(void)sprintf(fn, "%sper_cpu/cpu%d/trace_pipe_raw", get_debugfileprefix(), fthread->cpuno);
			}
			if (-1 == access (fn, R_OK)) {
				pstate = -1;
				err_msg (PFX "Could not open trace pipe for CPU%d", fthread->cpuno);
				err_msg (PIN "Tracing for CPU%d disabled", fthread->cpuno);
				free(fn);
				break;
			} /** if file doesn't exist **/

			if ((fp = fopen (fn, "r")) == NULL) {
				pstate = -1;
				err_msg ("File open failed");
				free(fn);
				break;
			} /** IF_NULL **/
			free(fn);

			printDbg(PFX "Reading trace output from pipe...\n");
			pstate = 1;
			//no break

		case 1:
			if (ftrace_stop){
				pstate = 2;
				break;
			}
			// read output into buffer!
			if (0 >= (ret = fread (buffer, sizeof(unsigned char), PIPE_BUFFER, fp))) {
				if (ret < -1) {
					pstate = 2;
					err_msg ("File read failed");
				} // else stay here

				break;
			}
			// empty buffer? it always starts with a header
			else if (buffer[0]==0 )
				break;

			if ((ret = kbuffer_load_subbuffer(kbuf, buffer)))
				warn ("Unable to parse ring-buffer page!");

			// read first element
			pEvent = kbuffer_read_event(kbuf, &timestamp);

			while ((pEvent)  && (!ftrace_stop)) {
				int (*eventcall)(void *, uint64_t) = pickPidCommon; // default to common for unknown formats

				for (struct ftrace_elist * event = elist_head; ((event)); event=event->next)
					// check for ID, first value is 16 bit ID
					if (event->eventid == *(uint16_t*)pEvent){
						eventcall = event->eventcall;
						break;
					}

				// call event
				int count = eventcall(pEvent, timestamp);
				if (0 > count){
					// something went wrong, dump and exit
					printDbg(PFX "CPU%d - Buffer probably unaligned, flushing", fthread->cpuno);
					break;
				}

				// gather next element
				pEvent = kbuffer_next_event(kbuf, &timestamp);
			}
			break;

		case 2:
			fclose (fp);
			kbuffer_free(kbuf);
			pstate = -1;
			break;

		case -1:
			sleep(1); // TODO: safety, avoids to overload CPU in case of not shutting down correctly
			break;
		}
		if (-1 == pstate) { // if  at the end, was ftrace_stop
			break;
		}
	}

	printf(PFX "Exit fTrace CPU%d thread\n", fthread->cpuno);
	fflush(stderr);

	return NULL;
}

// #################################### THREAD specific END ############################################

/// manageSched(): main function called to reassign resources
///
/// Arguments:
///
/// Return value: N/D - int
///
static int manageSched(){

	// this is for the dynamic and adaptive scheduler only

	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

    node_t * current = nhead;

	while (current != NULL) {

        current = current->next;
    }


	(void)pthread_mutex_unlock(&dataMutex);

	return 0;
}

/// get_sched_info(): get scheduler debug output info
///
/// Arguments: the node to get info for
///
/// Return value: error code, 0 = success
///
static int get_sched_info(node_t * item)
{
	char szFileName [_POSIX_PATH_MAX];
	char szStatBuff [PIPE_BUFFER];
	char ltag [80]; // just tag of beginning, max length expected ~30
    char *s, *s_ptr;

	FILE *fp;

	sprintf (szFileName, "/proc/%u/sched", (unsigned) item->pid);

	if (-1 == access (szFileName, R_OK)) {
		return -1;
	} /** if file doesn't exist **/

	if ((fp = fopen (szFileName, "r")) == NULL) {
		return -1;
	} /** IF_NULL **/

	// read output into buffer!
	if (0 >= fread (szStatBuff, sizeof(char), PIPE_BUFFER-1, fp)) {
		fclose (fp);
		return -1;
	}
	szStatBuff[PIPE_BUFFER-1] = '\0'; // safety first

	fclose (fp);

	int64_t num;
	int64_t diff = 0;
	int64_t ltrt = 0; // last seen runtime

	s = strtok_r (szStatBuff, "\n", &s_ptr);
	while (NULL != s) {
		(void)sscanf(s,"%s %*c %ld", ltag, &num);

		// ---------- SCHED_FIFO/RR --------------
		if (strncasecmp(ltag, "se.exec_start", 4) == 0)	{
			// execution time since start, reread
			int64_t nanos = 0; // value after the dot
			(void)sscanf(s,"%s %*c %ld.%ld", ltag, &num, &nanos);
			num *= 1000000; // push left for values after comma
			num += nanos;

			// compute difference
			diff = (int64_t)(num - item->mon.dl_rt);
			item->mon.dl_rt = num; 	// store last seen runtime
		}
		if (strncasecmp(ltag, "nr_voluntary_switches", 4) == 0)	{
			// computation loop end

			if ((item->mon.dl_count != num)
					&& (0 != item->mon.dl_count)) {
				// new switches detected
				item->mon.rt_min = MIN (item->mon.rt_min, diff);
				item->mon.rt_max = MAX (item->mon.rt_max, diff);
				item->mon.rt_avg = (item->mon.rt_avg * 9 + diff /* *1 */)/10;
			}

			// store last seen switch number
			item->mon.dl_count = num;
		}

		// ---------- SCHED_DEADLINE --------------
		if (strncasecmp(ltag, "dl.runtime", 4) == 0)	{
			// store last seen runtime
			ltrt = num;
			if (num != item->mon.dl_rt)
				item->mon.dl_count++;
		}
		if (strncasecmp(ltag, "dl.deadline", 4) == 0)	{
			if (0 == item->mon.dl_deadline) 
				item->mon.dl_deadline = num;
			else if (num != item->mon.dl_deadline) {
				// it's not, updated deadline found

				// calculate difference to last reading, should be 1 period
				diff = (int64_t)(num-item->mon.dl_deadline)-(int64_t)item->attr.sched_period;

				// difference is very close to multiple of period we might have a scan fail
				// in addition to the overshoot
				while (diff >= ((int64_t)item->attr.sched_period - TSCHS) ) { 
					item->mon.dl_scanfail++;
					diff -= (int64_t)item->attr.sched_period;
				}

				// overrun-GRUB handling statistics -- ?
				if (diff)  {
					item->mon.dl_overrun++;

					// usually: we have jitter but execution stays constant -> more than a slot?
					printDbg("\nPID %d Deadline overrun by %ldns, sum %ld\n",
						item->pid, diff, item->mon.dl_diff); 
				}

				item->mon.dl_diff += diff;
				item->mon.dl_diffmin = MIN (item->mon.dl_diffmin, diff);
				item->mon.dl_diffmax = MAX (item->mon.dl_diffmax, diff);

				// exponentially weighted moving average, alpha = 0.9
				item->mon.dl_diffavg = (item->mon.dl_diffavg * 9 + diff /* *1 */)/10;

				// runtime replenished - deadline changed: old value may be real RT ->
				// Works only if scan time < slack time 
				diff = (int64_t)item->attr.sched_runtime - item->mon.dl_rt;
				item->mon.rt_min = MIN (item->mon.rt_min, diff);
				item->mon.rt_max = MAX (item->mon.rt_max, diff);
				item->mon.rt_avg = (item->mon.rt_avg * 9 + diff /* *1 */)/10;

				item->mon.dl_deadline = num;
			}	

			// update last seen runtime
			item->mon.dl_rt = ltrt;
			break; // we're done reading
		}
		s = strtok_r (NULL, "\n", &s_ptr);	
	}

  return 0;
}

/// updateStats(): update the real time statistics for all scheduled threads
/// -- used for monitoring purposes ---
///
/// Arguments: - 
///
/// Return value: number of PIDs found (total) that exceed peak PDF
///				  defaults to 0 for static scheduler
///
static int updateStats ()
{
	static int prot = 0; // pipe rotation animation
	static char const sp[4] = "/-\\|";

	prot = (prot+1) % 4;
	if (!prgset->quiet)	
		(void)printf("\b%c", sp[prot]);		
	fflush(stdout);

	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	scount++; // increase scan-count

	// for now does only a simple update
	for (node_t * item = nhead; ((item)); item=item->next ) {
		// skip deactivated tracking items
		// skip pid 0, = undefined or ROOT PID (swapper/sched)
		if (item->pid<=0){
			item=item->next; 
			continue;
		}

		// update only when defaulting -> new entry, or every 100th scan
		if (!(scount%prgset->loops)
			|| (SCHED_NODATA == item->attr.sched_policy))
			updatePidAttr(item);

		/*  Curve Fitting from here, for now every second (default) */

		if (!(( scount % (prgset->loops*10) ))){
			// update CMD-line once out of 10 (less often..)
			updatePidCmdline(item);

			if (!(runstats_checkhist(item->mon.pdf_hist))){
				// if histogram is set and count is ok, update and fit curve

				// check if old parameters are fine, else reset
				if (runstats_verifyparam(item->mon.pdf_hist,
						item->mon.pdf_parm)){

					if (item->mon.pdf_parm)	// if re-instantiate, free first
						runstats_freeparam(item->mon.pdf_parm);

					double mn = (double)item->mon.rt_avg;
					if (!mn)
						mn = (double)item->attr.sched_runtime;
					if (!mn && item->param && item->param->attr)
						mn = (double)item->attr.sched_runtime;
					mn/=(double)NSEC_PER_SEC; // re-format to seconds

					// if still 0, don't know what to do.. -> at least bind it to range
					mn = runstats_shapehist(item->mon.pdf_hist, mn);

					runstats_initparam(&item->mon.pdf_parm, mn);
				}

				if ((runstats_solvehist(item->mon.pdf_hist, item->mon.pdf_parm)))
					warn("Curve fitting solver error for PID %d", item->pid);

				if ((runstats_fithist(&item->mon.pdf_hist)))
					warn("Curve fitting histogram bin adaptation error for PID %d", item->pid);
			}
		}

		// get runtime value
		if (!prgset->ftrace) // use standard debug output for scheduler
			if (policy_is_realtime(item->attr.sched_policy)) {
				int ret;
				if ((ret = get_sched_info(item)) ) {
					err_msg ("reading thread debug details %d", ret);
				}
			}
	}

	(void)pthread_mutex_unlock(&dataMutex); 

	return 0;
}

/// dumpStats(): prints thread statistics to out
///
/// Arguments: -
///
/// Return value: -
static void dumpStats (){

	node_t * item = nhead;
	(void)printf( "\nStatistics for real-time SCHED_DEADLINE PIDs, %ld scans:"
					" (others are omitted)\n"
					"Average exponential with alpha=0.9\n\n"
					"PID - Cycle Overruns(total/found/fail) - avg rt (min/max) - sum diff (min/max/avg)\n"
			        "----------------------------------------------------------------------------------\n",
					scount );

	// no PIDs in list
	if (!item) {
		(void)printf("(no PIDs)\n");
		return;
	}


	char * curve = malloc(50);
	int  ret = -1; // default not present

	for (;((item)); item=item->next)
		switch(item->attr.sched_policy){
		default:
		case SCHED_FIFO:
		case SCHED_RR:
			// TODO: cleanup print-out
			if (item->mon.pdf_parm)
				ret = runstats_printparam(item->mon.pdf_parm, curve, 50);

			(void)printf("%5d%c: %ld(%ld/%ld/%ld) - %ld(%ld/%ld) - %s\n\t%s\n",
				abs(item->pid), item->pid<0 ? '*' : ' ',
				item->mon.dl_overrun, item->mon.dl_count+item->mon.dl_scanfail,
				item->mon.dl_count, item->mon.dl_scanfail,
				item->mon.rt_avg, item->mon.rt_min, item->mon.rt_max,
				policy_to_string(item->attr.sched_policy),
				ret ? "none" : curve );
			break;

		case SCHED_DEADLINE:
			if (item->mon.pdf_parm)
				ret = runstats_printparam(item->mon.pdf_parm, curve, 50);
			(void)printf("%5d%c: %ld(%ld/%ld/%ld) - %ld(%ld/%ld) - %ld(%ld/%ld/%ld)\n\t%s\n",
				abs(item->pid), item->pid<0 ? '*' : ' ',
				item->mon.dl_overrun, item->mon.dl_count+item->mon.dl_scanfail,
				item->mon.dl_count, item->mon.dl_scanfail,
				item->mon.rt_avg, item->mon.rt_min, item->mon.rt_max,
				item->mon.dl_diff, item->mon.dl_diffmin, item->mon.dl_diffmax, item->mon.dl_diffavg,
				ret ? "none" : curve );
			free (curve);
			break;
		}
}

/// thread_manage(): thread function call to manage schedule list
///
/// Arguments: - thread state/state machine, passed on to allow main thread stop
///
/// Return value: Exit Code - o for no error
void *thread_manage (void *arg)
{
	// be explicit!
	int32_t* pthread_state = (int32_t *)arg;
	// initialize the thread locals
	while(1)
	{
	  switch( *pthread_state )
	  {
	  case 0: // setup thread
		*pthread_state=1; // first thing
		if (prgset->ftrace) {
			if (configureTracers())
				warn("Kernel function tracers not available");

			if (startTraceRead()){
				err_msg("Unable to start tracing, have to stop here now..");
				// set stop signal
				raise (SIGTERM); // tell main to stop
			}
		}
		//no break

	  case 1: // normal thread loop, check and update data
		if (!updateStats())
			break;	// stop here if no updates are found
		//no break

	  case 2: //
		// update resources
		*pthread_state=1;
		(void)manageSched();
		break;

	  case -1:
		*pthread_state=-2;
		// tidy or whatever is necessary
		dumpStats();
		// no break

	  case -2:
		*pthread_state=-99;
		// set stop signal to dependent threads
		if (prgset->ftrace) {
			(void)printf(PFX "Stopping threads\n");
			if (stopTraceRead())
				warn("Unable to stop all fTrace threads");
			resetTracers();
			(void)printf(PFX "Threads stopped\n");
		}
		// no break
	  case -99:
		//		pthread_exit(0); // exit the thread signaling normal return
		break;
	  }

	  // STOP Loop?
	  if (-99 == *pthread_state)
		break;

	  // TODO: change to timer and settings based loop
	  usleep(10000);
	}

	(void)printf(PFX "Stopped\n");
	// Start using return value
	return NULL;
}

