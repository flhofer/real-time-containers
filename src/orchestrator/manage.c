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

// parameter tree linked list head, resource linked list head
static struct resTracer * rhead;
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


////// TEMP ---------------------------------------------

#define MAX_UL 0.90

/// checkUvalue(): verify if task fits into Utilization limits of a resource
///
/// Arguments: resource entry for this cpu, the attr structure of the task
///
/// Return value: 0 = ok, -1 = no space, 1 = ok but recalc base
static int checkUvalue(struct resTracer * res, struct sched_attr * par) {
	uint64_t	base = res->basePeriod,
				used = res->usedPeriod;
	int rv = 0;
	
	if (base % par->sched_deadline != 0) {
		// realign periods
		uint64_t max_Value = MAX (base, par->sched_period);

		if (base % 1000 != 0 || par->sched_period % 1000 != 0)
			fatal("Nanosecond resolution periods not supported!");
			// temporary solution to avoid very long loops

		while(1) //Alway True
		{
			if(max_Value % base == 0 && max_Value % par->sched_period == 0) 
			{
				break;
			}
			max_Value+= 1000; // add a microsecond..
		}

		used *= max_Value/base;
		base = max_Value;	
		rv=1;
	}

	if (MAX_UL < (double)(used + par->sched_runtime * base/par->sched_period)/(double)(base) )
		rv = -1;

	return rv;
}

static void addUvalue(struct resTracer * res, struct sched_attr * par) {
	if (res->basePeriod % par->sched_deadline != 0) {
		// realign periods
		uint64_t max_Value = MAX (res->basePeriod, par->sched_period);

		if (res->basePeriod % 1000 != 0 || par->sched_period % 1000 != 0)
			fatal("Nanosecond resolution periods not supported!");
			// temporary solution to avoid very long loops

		while(1) //Always True
		{
			if(max_Value % res->basePeriod == 0 && max_Value % par->sched_period == 0) 
			{
				break;
			}
			max_Value+= 1000; // add a microsecond..
		}

		res->usedPeriod *= max_Value/res->basePeriod;
		res->basePeriod = max_Value;	

	}

	res->usedPeriod += par->sched_runtime * res->basePeriod/par->sched_period;
}

//////  END TEMP ---------------------------------------------

/// createResTracer(): create resource tracing memory elements
//
/// Arguments: 
///
/// Return value: N/D - int
///
static int createResTracer(){
	// mask affinity and invert for system map / readout of smi of online CPUs
	for (int i=0;i<(prgset->affinity_mask->size);i++) 

		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by selected only
			push((void**)&rhead, sizeof(struct resTracer));
			rhead->affinity = i;
			rhead->basePeriod = 1;
	}		
	return 0;
}

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

    node_t * current = head;

	while (current != NULL) {

        current = current->next;
    }


	(void)pthread_mutex_unlock(&dataMutex); 

	return 0;
}

// #################################### THREAD configuration specific ############################################

struct ftrace_elist {
	struct ftrace_elist * next;
	char* event;
	int eventid;
};

struct ftrace_elist * elist_head;

// signal to keep status of triggers ext SIG
volatile sig_atomic_t ftrace_stop;

pthread_t thread_traceRead;
int  iret_traceRead; // Timeout is set to 4 seconds by default
int  ino_traceRead; // run parameters

struct tr_runtime {
	uint16_t common_type; // 2
	uint8_t common_flags; // 3
	uint8_t common_preempt_count; //4
	int32_t common_pid; // 8

	char comm[16]; //24 - 16
	pid_t pid; // 28 - 20
	uint32_t dummy  ; // 32 - 24 - alignment filler
	uint64_t runtime; // 20 - 32
	uint64_t vruntime; // 48 - 40
	uint32_t dummy2  ; // 52 - 44 - alignment filler
	// filled with 0's to 52
};

static void *thread_ftrace(void *arg);

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

	printDbg(PFX "Trace status %d\n", notrace);
	// tracing_cpumask - hex string of tracing cpus!

	if (2 == notrace) {
		// TODO: add return value check
		(void)event_disable_all();
		(void)event_enable("sched/sched_stat_runtime");
		// TODO: use event_getid to filter events
		{
			push((void**)&elist_head, sizeof(struct ftrace_elist));
			elist_head->eventid = event_getid("sched/sched_stat_runtime");
			elist_head->event = "sched_stat_runtime";
		}
	}
	return notrace-2; // return number found versus needed
}

/// startTraceRead(): start CPU tracing threads
///
/// Arguments:
///
/// Return value:
///
static int startTraceRead() {

	int ino_traceRead = 0; // TODO: STATIC?
	iret_traceRead = pthread_create( &thread_traceRead, NULL, thread_ftrace, &ino_traceRead);

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
	if (!iret_traceRead) { // thread started successfully
		pthread_kill (thread_traceRead, SIGINT); // tell linking threads to stop
		iret_traceRead = pthread_join( thread_traceRead, NULL); // wait until end
	}

	// TODO: add return value
	return 0;
}

/// stphand(): interrupt handler for infinite while loop, help
/// this function is called from outside, interrupt handling routine
/// Arguments: - signal number of interrupt calling
///
/// Return value: -
//TODO: check function with static
static void stphandTrace (int sig, siginfo_t *siginfo, void *context){
	ftrace_stop = 1;
}


// #################################### THREAD specific ############################################

/// update_sched_info(): update data with kernel tracer debug out
///
/// Arguments:
///
/// Return value: error code, 0 = success
///
static int update_sched_info(struct tr_runtime * pFrame)
{

	// TODO: change UpdateStats form item update to update item :) ??

	// TODO: change to RW locks
	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	// for now does only a simple update
	for (node_t * item = head; ((item)); item=item->next )
		// skip deactivated tracking items

		if (abs(item->pid)==pFrame->pid){
				// item deactivated -> TODO actually an error!
			if (item->pid<0)
				break;

			item->mon.dl_diff = item->mon.dl_deadline - item->mon.dl_rt;
			item->mon.dl_diffmin = MIN (item->mon.dl_diffmin, item->mon.dl_diff);
			item->mon.dl_diffmax = MAX (item->mon.dl_diffmax, item->mon.dl_diff);

			if (pFrame->common_preempt_count) { // TODO: find out if before or after end
				item->mon.rt_min = MIN (item->mon.rt_min, item->mon.dl_rt);
				item->mon.rt_max = MAX (item->mon.rt_max, item->mon.dl_rt);
				item->mon.rt_avg = (item->mon.rt_avg * 9 + item->mon.dl_rt /* *1 */)/10;

				item->mon.dl_rt = pFrame->runtime;

			}
			else
				item->mon.dl_rt += pFrame->runtime;

			item->mon.dl_count++;
		}

	(void)pthread_mutex_unlock(&dataMutex);

	return 0;

}

/// thread_ftrace(): parse kernel tracer output
///
/// Arguments: status trace
///
/// Return value: error code, 0 = success
///
static void *thread_ftrace(void *arg){

	int pstate = 0;
	int got = 0;
	FILE *fp = NULL;
	int* cpuno = (int *)arg;

	// TODO: block not used signals
	{ // setup interrupt handler block
		struct sigaction act;
		memset (&act, '\0', sizeof(act));

		/* Use the sa_sigaction field because the handles has two additional parameters */
		act.sa_sigaction = &stphandTrace;

		/* The SA_SIGINFO flag tells sigaction() to use the sa_sigaction field, not sa_handler. */
		act.sa_flags = SA_SIGINFO;

		if (sigaction(SIGINT, &act, NULL) < 0) { // INT signal, stop from main prg
			perror ("Setup of sigaction failed");
			exit(EXIT_FAILURE); // exit the software, not working
		}
	} // END interrupt handler block

	unsigned char buffer[PIPE_BUFFER];
	struct tr_runtime *pFrame;

	while(1) {

		switch( pstate )
		{
		case 0:
			;
			char fn[100];
			(void)sprintf(fn, "%sper_cpu/cpu%d/trace_pipe_raw", get_debugfileprefix(), *cpuno);

			if (-1 == access (fn, R_OK)) {
				pstate = -1;
				err_msg (PFX "Could not open trace pipe for CPU%d", *cpuno);
				err_msg (PIN "Tracing for CPU%d disabled", *cpuno);
				break;
			} /** if file doesn't exist **/

			if ((fp = fopen (fn, "r")) == NULL) {
				pstate = -1;
				err_msg ("File open failed");
				break;
			} /** IF_NULL **/

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

			pFrame = (struct tr_runtime *)(buffer+20);
			got -=20;

			while ((NULL != (void*)pFrame) && (0 != pFrame->common_type)) {
//				printDbg( "type=%u flags=%u preempt=%u pid=%d : ",
//						pFrame->common_type, pFrame->common_flags, pFrame->common_preempt_count, pFrame->common_pid);

//				printDbg( "comm=%s pid=%d runtime=%lu [ns] vruntime=%lu [ns]\n",
//						pFrame->comm, pFrame->pid, pFrame->runtime, pFrame->vruntime);

				update_sched_info(pFrame);

				pFrame = (struct tr_runtime *)(((void *)pFrame) + 52); // TODO: adapt to frame size
				got -=52; // TODO: adapt to frame size

				// end of pipe, end of buffer?
				if (52 > got || 0x14 == pFrame->common_type){
					break;
				}
			}

			break;

		case 2:
			fclose (fp);
			pstate = -1;
			//no break
		case -1:
			break;
		}
		if (ftrace_stop) {
			if (fp) // if open, close first
				pstate = 2;
			else // just exit
				break;
		}
	}

	printf(PFX "\nExit thread\n");
	fflush(stderr);

	return NULL;
}

// #################################### THREAD specific END ############################################

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
	for (node_t * item = head; ((item)); item=item->next ) {
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

	node_t * item = head;
	(void)printf( "\nStatistics for real-time SCHED_DEADLINE PIDs, %ld scans:"
					" (others are omitted)\n"
					"Average exponential with alpha=0.9\n\n"
					"PID - Cycle Overruns(total/found/fail) - avg rt (min/max) - sum diff (min/max/avg)\n"
			        "----------------------------------------------------------------------------------\n",
					scount );

	// no PIDs in list
	if (!item) {
		(void)printf("(no PIDs)\n");
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
		if (prgset->ftrace)
			if (configureTracers())
				warn("Kernel function tracers not available");

		// TODO use return function
		(void)startTraceRead();
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
		// tidy or whatever is necessary
		dumpStats();
		pthread_exit(0); // exit the thread signaling normal return
		break;
	  }
	  // TODO: change to timer and settings based loop
	  usleep(10000);
	}
	// set stop signal
	stopTraceRead();

	// TODO: Start using return value
	return EXIT_SUCCESS;
}

