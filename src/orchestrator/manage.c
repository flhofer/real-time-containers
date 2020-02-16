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
#include "rt-utils.h"	// trace and other utilities
#include "kernutil.h"	// generic kernel utilities
#include "error.h"		// error and stderr print functions

#include <sys/wait.h>
#include <sys/types.h>
#include <numa.h>			// NUMA node identification
#include <sys/sysinfo.h>	// system general information

// total scan counter for updatestats
static uint64_t scount = 0; // total scan count

// for musl systems
#ifndef _POSIX_PATH_MAX
	#define _POSIX_PATH_MAX 1024
#endif

// Included in kernel 4.13
#ifndef SCHED_FLAG_RECLAIM
	#define SCHED_FLAG_RECLAIM		0x02
#endif
// Included in kernel 4.16
#ifndef SCHED_FLAG_DL_OVERRUN
	#define SCHED_FLAG_DL_OVERRUN		0x04
#endif

// TODO: standardize printing
#define PFX "[manage] "
#define PFL "         "PFX
#define PIN PFX"    "
#define PIN2 PIN"    "
#define PIN3 PIN2"    "

#define PIPE_BUFFER			4096

// #################################### THREAD configuration specific ############################################

// Linked list of event configurations and handlers
struct ftrace_elist {
	struct ftrace_elist * next;
	char* event;	// string identifier
	int eventid;	// event kernel ID
	int (*eventcall)(node_t **, void *); // event elaboration function
};
struct ftrace_elist * elist_head;

// Linked list of CPU threads
struct ftrace_thread {
	struct ftrace_thread * next;
	pthread_t thread;	// thread information
	int iret;			// return value of thread launch
	int cpuno;			// CPU number monitored
};
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

#ifndef DEBUG
static // define as static in non-debug
#endif
void *thread_ftrace(void *arg);

// functions to elaborate data for tracer frames
static int pickPidInfoW(node_t ** item, void * addr);
static int pickPidInfoS(node_t ** item, void * addr);
static int pickPidInfoR(node_t ** item, void * addr);

/// ftrace_inthand(): interrupt handler for infinite while loop, help
/// this function is called from outside, interrupt handling routine
/// Arguments: - signal number of interrupt calling
///
/// Return value: -
static void ftrace_inthand (int sig, siginfo_t *siginfo, void *context){
	ftrace_stop = 1;
}

/// configureTracers(): setup kernel function trace system
///
/// Arguments: - none
///
/// Return value: (-) no of missing traces, 0 = success
///
static int configureTracers(){
	// TODO: fix tracers poll
	int notrace = valid_tracer("wakeup_rt") +
			valid_tracer("wakeup_dl");

	printDbg(PFX "Present tracers, status %d\n", notrace);

	if (2 == notrace) {
		char * dbgpfx = get_debugfileprefix();

		// TODO: add to util?
		if ( 0 > setkernvar(dbgpfx, "tracing_on", "0", prgset->dryrun))
			warn("Can not disable kernel function tracing");

		// TODO: add return value check
		if (0 > event_disable_all())
			warn("Unable to clear kernel fTrace event list");

		if (0 < event_enable("sched/sched_stat_runtime")) {
			push((void**)&elist_head, sizeof(struct ftrace_elist));
			elist_head->eventid = event_getid("sched/sched_stat_runtime");
			elist_head->event = "sched_stat_runtime";
			elist_head->eventcall = pickPidInfoR;
		}
		else
			warn("Unable to set event for 'sched_stat_runtime'");

		if (0 < event_enable("sched/sched_wakeup")) {
			push((void**)&elist_head, sizeof(struct ftrace_elist));
			elist_head->eventid = event_getid("sched/sched_wakeup");
			elist_head->event = "sched_wakeup";
			elist_head->eventcall = pickPidInfoW;
		}
		else
			warn("Unable to set event for 'sched_wakeup'");


		if (0 < event_enable("sched/sched_switch")) {
			push((void**)&elist_head, sizeof(struct ftrace_elist));
			elist_head->eventid = event_getid("sched/sched_switch");
			elist_head->event = "sched_switch";
			elist_head->eventcall = pickPidInfoS;
		}
		else
			warn("Unable to set event for 'sched_switch'");

		if ( 0 > setkernvar(dbgpfx, "tracing_on", "1", prgset->dryrun))
			warn("Can not enable kernel function tracing");

		printDbg("\n");
	}
	// TODO: review this return value
	return notrace-2; // return number found versus needed
}

/// startTraceRead(): start CPU tracing threads
///
/// Arguments:
///
/// Return value:
///
static int startTraceRead() {

	int maxcpu = get_nprocs();
	// loop through, bit set = start a thread and store in ll
	for (int i=0;i<maxcpu  ;i++)
		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by active
			push((void**)&elist_thead, sizeof(struct ftrace_elist));
			elist_thead->cpuno = i;
			//TODO: use return value
			elist_thead->iret = pthread_create( &elist_thead->thread, NULL, thread_ftrace, &elist_thead->cpuno);
#ifdef DEBUG
			char tname [17]; // 16 char length restriction
			(void)sprintf(tname, "manage_ftCPU%d", elist_thead->cpuno); // space for 4 digit CPU number
			(void)pthread_setname_np(elist_thead->thread, tname);
#endif
		}

	// TODO: add return value
	return 0;
}

/// stopTraceRead(): stop CPU tracing threads
///
/// Arguments:
///
/// Return value:
///
static int stopTraceRead() {

	// loop through, existing list elements, and join
	while ((elist_thead))
		if (!elist_thead->iret) { // thread started successfully
			//TODO: return value
			(void)pthread_kill (elist_thead->thread, SIGQUIT); // tell threads to stop
			elist_thead->iret = pthread_join( elist_thead->thread, NULL); // wait until end
			pop((void**)&elist_thead);
		}

	// TODO: add return value
	return 0;
}

// #################################### THREAD specific ############################################

static int pickPidCons(node_t *item){
	item->mon.dl_diff = item->mon.dl_deadline - item->mon.dl_rt;
	item->mon.dl_diffmin = MIN (item->mon.dl_diffmin, item->mon.dl_diff);
	item->mon.dl_diffmax = MAX (item->mon.dl_diffmax, item->mon.dl_diff);


	item->mon.rt_min = MIN (item->mon.rt_min, item->mon.dl_rt);
	item->mon.rt_max = MAX (item->mon.rt_max, item->mon.dl_rt);
	item->mon.rt_avg = (item->mon.rt_avg * 9 + item->mon.dl_rt /* *1 */)/10;

	item->mon.dl_rt = 0;

	return 0; // TODO:
}

/// pickPidInfoS(): process PID fTrace common
///
/// Arguments: - item to update with statistics
///			   - frame containing the runtime info
///
/// Return value: error code, 0 = success
///
static int pickPidCommon(node_t ** item, void * addr) {
	struct tr_common *pFrame = (struct tr_common*)addr;

	printDbg( "type=%u flags=%u preempt=%u pid=%d\n",
			pFrame->common_type, pFrame->common_flags, pFrame->common_preempt_count, pFrame->common_pid);

	return sizeof(*pFrame);
}

/// pickPidInfoW(): process PID fTrace sched_wakeup
///					update data with kernel tracer debug out
///
/// Arguments: - item to update with statistics
///			   - frame containing the runtime info
///
/// Return value: error code, 0 = success
///
static int pickPidInfoW(node_t ** item, void * addr) {

	int ret1 = pickPidCommon(item, addr);
	addr+= sizeof(struct tr_common);

	struct tr_wakeup *pFrame = (struct tr_wakeup*)addr;


	printDbg("    comm=%s pid=%d prio=%d target_cpu=%03d\n",
			pFrame->comm, pFrame->pid, pFrame->prio, pFrame->target_cpu);

	// TODO: change to RW locks
	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	if ((*item))
		(void)pickPidCons(*item);

	// reset item, remains null if not found
	*item=NULL;
	// for now does only a simple update
	for (node_t * citem = nhead; ((citem)); citem=citem->next )
		// skip deactivated tracking items
		if (abs(citem->pid)==pFrame->pid){
			*item = citem;
			break;
		}

	(void)pthread_mutex_unlock(&dataMutex);

	return ret1 + sizeof(struct tr_wakeup);
}

/// pickPidInfoS(): process PID fTrace sched_switch
///					update data with kernel tracer debug out
///
/// Arguments: - item to update with statistics
///			   - frame containing the runtime info
///
/// Return value: error code, 0 = success
///
static int pickPidInfoS(node_t ** item, void * addr) {

	int ret1 = pickPidCommon(item, addr);
	addr+= sizeof(struct tr_common);

	struct tr_switch *pFrame = (struct tr_switch*)addr;


	printDbg("    prev_comm=%s prev_pid=%d prev_prio=%d prev_state=%ld ==> next_comm=%s next_pid=%d next_prio=%d\n",
				pFrame->prev_comm, pFrame->prev_pid, pFrame->prev_prio, pFrame->prev_state,
//				(pFrame->prev_state & ((((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) - 1))
//				? __print_flags(pFrame->prev_state & ((((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) - 1),"|", { 0x0001, "S" }, { 0x0002, "D" }, { 0x0004, "T" }, { 0x0008, "t" }, { 0x0010, "X" }, { 0x0020, "Z" }, { 0x0040, "P" }, { 0x0080, "I" }) : "R",
//						pFrame->prev_state & (((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) ? "+" : "",
						pFrame->next_comm, pFrame->next_pid, pFrame->next_prio);

	// TODO: change to RW locks
	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	if ((*item))
		(void)pickPidCons(*item);

	// reset item, remains null if not found
	*item=NULL;
	// for now does only a simple update
	for (node_t * citem = nhead; ((citem)); citem=citem->next )
		// skip deactivated tracking items
		if (abs(citem->pid)==pFrame->next_pid){
			*item = citem;
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
///
/// Return value: error code, 0 = success
///
static int pickPidInfoR(node_t ** item, void * addr)
{
	struct tr_common *pcFrame = (struct tr_common*)addr;

	int ret1 = pickPidCommon(item, addr);
	addr+= sizeof(struct tr_common);

	struct tr_runtime *pFrame = (struct tr_runtime*)addr;


	printDbg( "    comm=%s pid=%d runtime=%lu [ns] vruntime=%lu [ns]\n",
			pFrame->comm, pFrame->pid, pFrame->runtime, pFrame->vruntime);

	// TODO: change to RW locks
	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	// reset item, remains null if not found
	if (!(*item) || ((*item) && (*item)->pid != pFrame->pid)) {
		if ((*item))
			(void)pickPidCons(*item);

		*item=NULL;
		// for now does only a simple update
		for (node_t * citem = nhead; ((citem)); citem=citem->next )
			// skip deactivated tracking items
			if (abs(citem->pid)==pFrame->pid){
				*item = citem;
				break;
			}
	}

	// item deactivated -> TODO actually an error!
	if ((*item) && (*item)->pid > 0) {
		(*item)->mon.dl_rt += pFrame->runtime;
		(*item)->mon.dl_scanfail += pcFrame->common_preempt_count;
		(*item)->mon.dl_count++;
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
	int got = 0;
	FILE *fp = NULL;
	int* cpuno = (int *)arg;

	unsigned char buffer[PIPE_BUFFER];
	uint16_t *pType;
	node_t * item;

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

		(void)sigfillset(&set);
		(void)sigdelset(&set, SIGQUIT);
		if (0 != pthread_sigmask(SIG_BLOCK, &set, NULL))
		{
			perror ("Setup of sigmask failed");
			exit(EXIT_FAILURE); // exit the software, not working
		}
	}

	while(1) {

		switch( pstate )
		{
		case 0:
			;
			char* fn;
			if (NULL != arg)
				fn = (char *)arg;
			else{
				fn = malloc(100);
				(void)sprintf(fn, "%sper_cpu/cpu%d/trace_pipe_raw", get_debugfileprefix(), *cpuno);
			}
			if (-1 == access (fn, R_OK)) {
				pstate = -1;
				err_msg (PFX "Could not open trace pipe for CPU%d", *cpuno);
				err_msg (PIN "Tracing for CPU%d disabled", *cpuno);
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
			//no break

		case 1:
			pstate = 1;

			// read output into buffer!
			if (0 >= (got = fread (buffer, sizeof(unsigned char), PIPE_BUFFER, fp))) {
				if (got < -1) {
					pstate = 2;
					err_msg ("File read failed");
				} // else stay here

				break;
			}
			else if (buffer[0]==0 ) // empty buffer, it always starts with an ID
				break;

			// TODO: what are these first 20 bytes?

			pType = (uint16_t *)(buffer+20);
			got -=20;

			while ((NULL != (void*)pType) && (0 != *pType) && (!ftrace_stop)) {
				int (*eventcall)(node_t **, void *) = pickPidCommon; // default to common

				for (struct ftrace_elist * event = elist_head; ((event)); event=event->next)
					if (event->eventid == *pType){
						eventcall = event->eventcall;
						break;
					}

				// call event
				int count = eventcall(&item, (void*)pType);
				if (0 > count){
					// something went wrong, dump and exit
					got = 0;
					printDbg(PFX "CPU%d - Buffer probably unaligned, flushing", *cpuno);
					break;
				}
				// TODO: no event? something went wrong, not configured event

				// update to the end of Frame
				pType += (count/2); // 16 bit values, double in bytes
				got -= count;

				{ // read end of Frame signature and update pointers
					int32_t * pFrameSig = (int32_t*) pType;
					printDbg(PFX "Frame signature %d\n",  *pFrameSig);

					pType += sizeof(*pFrameSig)/2;
					got -= sizeof(*pFrameSig);
				}

				// end of pipe, end of buffer?
				if (44 > got || 0x14 == *pType){ // TODO: test if got size necessary
					break;
				}
			}
			if (!ftrace_stop)
				break;
			// no break

		case 2:
			fclose (fp);
			pstate = -1;
			//no break

		case -1:
			break;
		}
		if (ftrace_stop) {
			break;
		}
	}

	printf(PFX "Exit fTrace CPU%d thread\n", *cpuno);
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

	// TODO: this is for the dynamic and adaptive scheduler only

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
		if (item->pid<0){
			item=item->next; 
			continue;
		}

		//TODO : move this to the container discovery
		// update only when defaulting -> new entry, or every 100th scan
		if (!(scount%prgset->loops) || (SCHED_NODATA == item->attr.sched_policy)) {
			struct sched_attr attr_act;
			if (sched_getattr (item->pid, &attr_act, sizeof(struct sched_attr), 0U) != 0) {

				warn("Unable to read parameters for PID %d: %s", item->pid, strerror(errno));
			}

			if (memcmp(&(item->attr), &attr_act, sizeof(struct sched_attr))) {
				
				if (SCHED_NODATA != item->attr.sched_policy)
					info("scheduling attributes changed for pid %d", item->pid);
				item->attr = attr_act;
			}

			// set the flag for GRUB reclaim operation if not enabled yet
			if ((prgset->setdflag) 
				&& (SCHED_DEADLINE == item->attr.sched_policy) 
				&& (KV_416 <= prgset->kernelversion) 
				&& !(SCHED_FLAG_RECLAIM == (item->attr.sched_flags & SCHED_FLAG_RECLAIM))){

				cont("Set dl_overrun flag for PID %d", item->pid);		

				// TODO: DL_overrun flag is set to inform running process of it's overrun
				// could actually be a problem for the process itself -> depends on task.
				// May need to set a parameter
				item->attr.sched_flags |= SCHED_FLAG_RECLAIM | SCHED_FLAG_DL_OVERRUN;
				if (sched_setattr (item->pid, &(item->attr), 0U))
					err_msg_n(errno, "Can not set overrun flag");
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

	for (;((item)); item=item->next)
//		TODO: commented out, for tests only
//		if (policy_is_realtime(item->attr.sched_policy))
			switch(item->attr.sched_policy){
			default:
			case SCHED_FIFO:
			case SCHED_RR:
				// TODO: cleanup print-out
				(void)printf("%5d%c: %ld(%ld/%ld/%ld) - %ld(%ld/%ld) - %s\n",
					abs(item->pid), item->pid<0 ? '*' : ' ',
					item->mon.dl_overrun, item->mon.dl_count+item->mon.dl_scanfail,
					item->mon.dl_count, item->mon.dl_scanfail,
					item->mon.rt_avg, item->mon.rt_min, item->mon.rt_max,
					policy_to_string(item->attr.sched_policy));
				break;

			case SCHED_DEADLINE:
				(void)printf("%5d%c: %ld(%ld/%ld/%ld) - %ld(%ld/%ld) - %ld(%ld/%ld/%ld)\n",
					abs(item->pid), item->pid<0 ? '*' : ' ',
					item->mon.dl_overrun, item->mon.dl_count+item->mon.dl_scanfail,
					item->mon.dl_count, item->mon.dl_scanfail,
					item->mon.rt_avg, item->mon.rt_min, item->mon.rt_max,
					item->mon.dl_diff, item->mon.dl_diffmin, item->mon.dl_diffmax, item->mon.dl_diffavg);
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
			// TODO use return function
			(void)startTraceRead();
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
			(void)stopTraceRead();
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
	// TODO: Start using return value
	return NULL;
}

