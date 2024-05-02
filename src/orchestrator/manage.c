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
#include "cmnutil.h"		// common definitions and functions
#include "kbuffer.h"		// ring-buffer management from trace-event
#include "resmgnt.h"		// PID and resource management

#include <numa.h>		// NUMA node identification

#undef PFX
#define PFX "[manage] "

#define PIPE_BUFFER			4096
#if __x86_64__ || __ppc64__
	#define WORDSIZE		KBUFFER_LSIZE_8
#else
	#define WORDSIZE		KBUFFER_LSIZE_4
#endif
#define GET_VARIABLE_NAME(Variable) (#Variable)

// total scan counter for update-stats
static uint64_t scount = 0; // total scan count

// #################################### THREAD configuration specific ############################################

// linked list of event configuration fields
struct ftrace_ecfg {
	struct ftrace_ecfg * next;
	char* name;
	int type;
	int offset;
	int size;
	int sign;
};

// Linked list of event configurations and handlers
struct ftrace_elist {
	struct ftrace_elist * next;
	char* event;	// string identifier
	int eventid;	// event kernel ID
	int (*eventcall)(const void *, const struct ftrace_thread *, uint64_t); // event elaboration function
	struct ftrace_ecfg* fields;
};
struct ftrace_elist * elist_head;

struct ftrace_thread * elist_thead = NULL;

// variable types
enum tr_vtypes { trv_short, trv_int, trv_long, trv_longlong, trv_char = 10, trv_pid_t = 20 };

// Parser offset structures - pointers to values for common
struct tr_common {
	uint16_t* common_type;
	uint8_t * common_flags;
	uint8_t * common_preempt_count;
	int32_t * common_pid;
} tr_common;
// related variable name dictionary
const char * tr_common_dict[] = { "common",
								GET_VARIABLE_NAME(tr_common.common_type),
								GET_VARIABLE_NAME(tr_common.common_flags),
								GET_VARIABLE_NAME(tr_common.common_preempt_count),
								GET_VARIABLE_NAME(tr_common.common_pid),
								NULL};

// Parser offset structures - pointers to values for sched_switch
struct tr_switch {
	char    * prev_comm;
	pid_t   * prev_pid;
	int32_t * prev_prio;

	uint64_t * prev_state;

	char   * next_comm;
	pid_t  * next_pid;
	int32_t* next_prio;
} tr_switch;
// related variable name dictionary
const char * tr_switch_dict[] = { "sched_switch",
								GET_VARIABLE_NAME(tr_switch.prev_comm),
								GET_VARIABLE_NAME(tr_switch.prev_pid),
								GET_VARIABLE_NAME(tr_switch.prev_prio),
								GET_VARIABLE_NAME(tr_switch.prev_state),
								GET_VARIABLE_NAME(tr_switch.next_comm),
								GET_VARIABLE_NAME(tr_switch.next_pid),
								GET_VARIABLE_NAME(tr_switch.next_prio),
								NULL};

// Parser offset structures - pointers to values for sched_wakeup
struct tr_wakeup {
	char  * comm;
	pid_t * pid;
	int32_t * prio;
	int32_t * success;
	int32_t * target_cpu;
} tr_wakeup;
// related variable name dictionary
const char * tr_wakeup_dict[] = { "sched_wakeup",
								GET_VARIABLE_NAME(tr_wakeup.comm),
								GET_VARIABLE_NAME(tr_wakeup.pid),
								GET_VARIABLE_NAME(tr_wakeup.prio),
								GET_VARIABLE_NAME(tr_wakeup.success),
								GET_VARIABLE_NAME(tr_wakeup.target_cpu),
								NULL};

// these are data and name dictionaries used for parsing
const char ** tr_event_dict [] = { tr_common_dict, tr_switch_dict, tr_wakeup_dict, NULL };
const void * tr_event_structs [] = { &tr_common, &tr_switch, &tr_wakeup, NULL };

// signal to keep status of triggers ext SIG
static volatile sig_atomic_t ftrace_stop;

void *thread_ftrace(void *arg);

// functions to elaborate data for tracer frames
static int pickPidCommon(const void * addr, const struct ftrace_thread * fthread, uint64_t ts);
static int pickPidInfoS(const void * addr, const struct ftrace_thread * fthread, uint64_t ts);
static int pickPidInfoW(const void * addr, const struct ftrace_thread * fthread, uint64_t ts);

static int get_sched_info(node_t * item);

/*
 *  ftrace_inthand(): interrupt handler for infinite while loop, help
 *  this function is called from outside, interrupt handling routine
 *
 *  Arguments: - signal number of interrupt calling
 *
 *  Return value: -
 */
static void
ftrace_inthand (int sig, siginfo_t *siginfo, void *context){
	ftrace_stop = 1;
}

/*
 *  parseEventOffsets(): store field offsets into structs (ftrace)
 *				this function puts field offsets to known trace
 *				structures based on read field configuration
 *
 *  Arguments: - ( global dictionaries/arrays are used to init structures )
 *
 *  Return value: 0 on success, -1 on error
 */
static int
parseEventOffsets(){
	//

	int cmn = 1;	/// run once - always - for common structure, first in array

	const char *** tr_dicts = tr_event_dict;	// working pointer to structure dictionaries, init to first

	// Loop through structures to init, first is 'tr_common" (cmn =1)
	// note type cast below -> structs contain pointers we want to modify
	for(const void *** tr_structs = (void const***)tr_event_structs;(*tr_structs) && (*tr_dicts); tr_structs++, tr_dicts++ ){

		// Loop through loaded ftrace events to find match
		for (struct ftrace_elist * event = elist_head; (event); event=event->next){

			// Event configuration name matches structure name in dictionary (or common is set for cmn set), find field configs
			if ((cmn) || !strcmp(**tr_dicts,event->event)){
				// match of dict
				void const ** tr_struct = *tr_structs;	// init working pointer to memory begin position of struct (first field)
				// loop through dict entries for fields -> find cfgs
				for (char const ** tr_dict = ++(*(tr_dicts)); (*tr_dict); tr_dict++, tr_struct++){
					char * t_tok;
					char * entry = strdup (*tr_dict);

					(void)strtok_r (entry, ".", &t_tok);
					if (t_tok)
						// loop through field cfg to find match for field in struct
						for (struct ftrace_ecfg * cfg = event->fields; (cfg); cfg = cfg->next)
							if (!strcmp(t_tok, cfg->name)){
								// set offset
								*tr_struct = (const unsigned char*)0x0 + cfg->offset;
								break;
							}
					free(entry);
				}
				cmn = 0;
				break;
			}
		}
	}

	return -1;
}


/*
 *  parseEventFields(): parse field format for event (fTrace)
 *
 *  Arguments: - event entry field list head
 *  		   - format buffer, null terminated
 *
 *  Return value: 0 on success, -1 on error
 */
static int
parseEventFields(struct ftrace_ecfg ** ecfg, char * buffer){

	char * s, * s_tok;
	char * delim = ": \n";
	int fp = 0;	// field position counter, 0 = off, 1 beign, 2 name, 3...

	// Scan though buffer and parse contents
	s = strtok_r (buffer,delim, &s_tok);
	while(s) {
		if (fp)
			// field parsing state machine
			switch (fp) {
				case 2:	// field:
					{
						char * name = s; // variable type and name - it's ok to edit buffer
						char * t, * t_tok;
						t = strtok_r (name, " ", &t_tok);
						(*ecfg)->sign = 1;
						while (t){

							// preset size and sign, check later
							if (!strcmp(t,"short")){
								(*ecfg)->type = trv_short;
								(*ecfg)->size = sizeof(short);
							}
							if (!strcmp(t,"int")){
								(*ecfg)->type = trv_int;
								(*ecfg)->size = sizeof(int);
							}
							if (!strcmp(t,"long") && trv_long == (*ecfg)->type){
								// long keyword for second time
								(*ecfg)->type = trv_longlong;
								(*ecfg)->size = 2*sizeof(long);
							}
							if (!strcmp(t,"long")){
								(*ecfg)->type = trv_long;
								(*ecfg)->size = sizeof(long);
							}
							if (!strcmp(t,"char")){
								(*ecfg)->type = trv_char;
								(*ecfg)->sign = 0;
								(*ecfg)->size = sizeof(char);
							}
							if (!strcmp(t,"pid_t")){
								(*ecfg)->type = trv_pid_t;
								(*ecfg)->size = sizeof(int);
							}
							if (!strcmp(t,"unsigned"))
								(*ecfg)->sign = 0;

							name = t;		// last ok value
							t = strtok_r (NULL, " ", &t_tok);
						}
						// extract name and sub-parse for array size if present
						t = strtok_r (name, "[]", &t_tok);

						(*ecfg)->name=strdup(name);
						// if an array, read size in type description
						t = strtok_r (NULL, "[]", &t_tok);
						if (t) {
							(*ecfg)->size=atoi(t) * (*ecfg)->size;
						}
					}
					delim = "\t;: \n";
					fp++;
					break;
				case 3:	// offset:
					if (!strcmp(s, "offset")){
						s = strtok_r (NULL, delim, &s_tok);
						(*ecfg)->offset=atoi(s);
						fp++;
					}
					break;
				case 4:	// size:
					if (!strcmp(s, "size")){
						s = strtok_r (NULL, delim, &s_tok);
						int size = atoi(s);
						if (size != (*ecfg)->size)
							printDbg(PFX "ftrace event parse - Field size mismatch, type %d bytes, value %d bytes\n", (*ecfg)->size, size);
						(*ecfg)->size=size;
						fp++;
					}
					break;
				case 5:	// signed:
					if(!strcmp(s, "signed")){
						s = strtok_r (NULL, delim, &s_tok);
						int sign = atoi(s);
						if (sign != (*ecfg)->sign)
							printDbg(PFX "ftrace event parse - Field sign mismatch, type %s, value %s\n", ((*ecfg)->sign)?"signed":"unsigned", (sign)?"signed":"unsigned");
						(*ecfg)->sign=sign;
						fp=1;	// ok, return
					}
					break;
				default:
					if (!strcmp(s, "field")){
						fp = 2;
						if (*ecfg)
							ecfg=&(*ecfg)->next;
						push((void**)ecfg, sizeof(struct ftrace_ecfg));
						delim = ";\n";
					}
					else if (!strcmp(s, "print")) {
						fp = 0;
						s = strtok_r (NULL, "\n", &s_tok); // read until end of line
					}
					else
						printDbg(PFX "ftrace event parse - Structure mismatch; expecting 'field', got '%s'\n", s);
			}
		else if (!strcmp(s, "name")){
			// read name
			s = strtok_r (NULL, delim, &s_tok);
		}
		else if (!strcmp(s, "ID")){
			// read ID
			s = strtok_r (NULL, delim, &s_tok);
		}
		else if (!strcmp(s, "format")) {
			// switch to format parsing
			delim = "\t;: \n";
			fp = 1;
		}
		else if (!strcmp(s, "print")) {
			// dup!
			fp = 0;
			s = strtok_r (NULL, "\n", &s_tok); // read until end of line
			printDbg(PFX "ftrace event parse - Structure mismatch; unexpected 'print'");
		}
		else
			printDbg(PFX "ftrace event parse - Unknown token %s\n", s);

		// Next Token
		s = strtok_r (NULL, delim, &s_tok);

	}
	return -1; // TODO: return value
}

/*
 *  appendEvent(): add an event to event list to watch (fTrace)
 *
 *  Arguments: - debug path prefix
 *  		   - event name (path)
 *  		   - function (pointer) to call for the event
 *
 *  Return value: 0 on success, -1 on error
 */
static int
appendEvent(char * dbgpfx, char * event, void* fun ){

	char path[_POSIX_PATH_MAX];
	(void)sprintf(path, "%sevents/%s/", dbgpfx, event);

	// maybe put it in a function??
	if (0 < setkernvar(path, "enable", "1", prgset->dryrun)) {
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

		{
			char buf[PIPE_BUFFER];
			if ( 0 < getkernvar(path, "format", buf, sizeof(buf)))
				parseEventFields(&elist_head->fields, buf);
			else{
				warn("Unable to get event format '%s'", event);
				return -1;
			}
		}

		return 0;
	}

	warn("Unable to set event for '%s'", event);

	return -1;
}

/*
 *  configureTracers(): setup kernel function trace system
 *
 *  Arguments: - none
 *
 *  Return value: 0 = success, else error
 */
static int
configureTracers(){

	char * dbgpfx = get_debugfileprefix();

	if (!dbgpfx)
		return -1;

	if ( 0 > setkernvar(dbgpfx, "tracing_on", "0", prgset->dryrun))
		warn("Can not disable kernel function tracing");

	if ( 0 > setkernvar(dbgpfx, "events/enable", "0", prgset->dryrun))
		warn("Unable to clear kernel fTrace event list");

	{ // get CPU-set in hex for tracing
		char trcpuset[129]; // enough for 512 CPUs
		if (!parse_bitmask_hex(prgset->affinity_mask, trcpuset, sizeof(trcpuset))){
			if (0 > setkernvar(dbgpfx, "tracing_cpumask", trcpuset, prgset->dryrun) )
				warn("Unable to set tracing CPU-set");
		}
		else
			warn("can not obtain HEX CPU mask");
	}

	if ((appendEvent(dbgpfx, "sched/sched_switch", pickPidInfoS)))
		return -1;

	if ((appendEvent(dbgpfx, "sched/sched_wakeup", pickPidInfoW)))
		return -1;

	if ((parseEventOffsets()))
		return -1;

	if ( 0 > setkernvar(dbgpfx, "tracing_on", "1", prgset->dryrun)){
		warn("Can not enable kernel function tracing");
		return -1;
	}

	return 0; // setup successful?
}

/*
 * resetTracers(): reset kernel function trace system
 *
 *  Arguments: - none
 *
 *  Return value: 0 = success, else error
 */
static void
resetTracers(){
	char * dbgpfx = get_debugfileprefix();

	if ( 0 > setkernvar(dbgpfx, "tracing_on", "0", prgset->dryrun))
		warn("Can not disable kernel function tracing");

	if (0 > setkernvar(dbgpfx, "events/enable", "0", prgset->dryrun))
		warn("Unable to clear kernel fTrace event list");

	// sched_stat_runtime tracer seems to need sched_stats
	if (0 > setkernvar(prgset->procfileprefix, "sched_schedstats", "0", prgset->dryrun) )
		warn("Unable to deactivate schedstat probe");

	while (elist_head){
		free(elist_head->event);
		while (elist_head->fields){
			free(elist_head->fields->name);
			pop((void**)&elist_head->fields);
		}
		pop((void**)&elist_head);
	}
}

/*
 *  startTraceRead(): start CPU tracing threads
 *
 *  Arguments:
 *
 *  Return value: OR-result of pthread_create, negative if one failed
 */
static int
startTraceRead() {

	int maxcpu = prgset->affinity_mask->size;
	int ret = 0;
	// loop through, bit set = start a thread and store in ll
	for (int i=0;i<maxcpu;i++)
		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by active
			push((void**)&elist_thead, sizeof(struct ftrace_thread));
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

/*
 *  stopTraceRead(): stop CPU tracing threads
 *
 *  Arguments:
 *
 *  Return value: OR-result of pthread_*, negative if one failed
 */
static int
stopTraceRead() {

	int ret = 0;
	void * retVal = NULL;
	// loop through, existing list elements, and join
	while ((elist_thead))
		if (!elist_thead->iret) { // thread started successfully

			int ret1 = 0;
			if ((ret1 = pthread_kill (elist_thead->thread, SIGQUIT))) // tell threads to stop
				err_msg_n(ret1, "Failed to send signal to fTrace thread");
			ret |= ret1; // combine results in OR to detect one failing

			if ((ret1 = pthread_join( elist_thead->thread, &retVal))) // wait until end
				err_msg_n(ret1, "Could not join with fTrace thread");
			ret |= ret1 | *(int*)retVal; // combine results in OR to detect one failing

			if (retVal){ // return value assigned
				if (*(int*)retVal)
					err_msg_n(*(int*)retVal, "fTrace thread exited");
				free (retVal); // free heap space of return value
			}

			free(elist_thead->dbgfile); // free it if defined
			pop((void**)&elist_thead);
		}
	return ret; // >= 0 if OK, else negative
}

// #################################### THREAD specific ############################################

/*
 *  pidReallocAndTest(): try to reallocate a PID to a new fit
 *
 *  Arguments: - resource tracer
 *  		   - candidate tracer
 *  		   - item of PID to move
 *
 *  Return value: -1 failed, -2 origin still full, 0 = success, 1 = no change
 */
static int
pidReallocAndTest(resTracer_t * ntrc, resTracer_t * trc, node_t * node){

	if (ntrc && ntrc != trc){
		// better fit found

		// move all threads of same container
		if (node->param)
			for (node_t * item = nhead; ((item)); item=item->next )
				if (0 < item->pid && item->param && item->param->cont
						&& item->param->cont == node->param->cont){

					item->mon.assigned = ntrc->affinity;
					if (!setPidAffinityAssinged (item)){
						item->mon.resched++;
						continue;
					}
					if (trc){ // Reallocate did not work, undo if possible
						item->mon.assigned = trc->affinity;
						for (node_t * bitem = nhead; ((bitem)) && bitem != item; bitem=bitem->next)
							if (0 < bitem->pid && bitem->param && bitem->param->cont
									&& bitem->param->cont == item->param->cont){
								bitem->mon.assigned = trc->affinity;
								(void)setPidAffinityAssinged (bitem);
							}
					}
					return -1;
				}

		// all done, recompute CPU-times
		(void)recomputeCPUTimes(ntrc->affinity);
		if ((trc) && 0 > recomputeCPUTimes(trc->affinity))
			return -2; // more than one task to move
		return 0;
	}
	return 1;
}

/*
 *  pickPidReallocCPU(): process PID runtime overrun,
 *
 *  Arguments: - CPU number to check
 *
 *  Return value: -1 failed, 0 = success (ok)
 */
static int
pickPidReallocCPU(int32_t CPUno, uint64_t deadline){
	resTracer_t * trc = getTracer(CPUno);
	resTracer_t * ntrc = NULL;

	for (int include = 0; include < 2; include++ )
		// run twice, include=0 and include=1 to force move second time
		for (node_t * item = nhead; ((item)); item=item->next){
			if (item->mon.assigned != CPUno || 0 > item->pid
					// consider only within next period
				|| ((deadline) && item->mon.deadline >= deadline))
				continue;

			ntrc = checkPeriod_R(item, include);
			if (!pidReallocAndTest(ntrc, trc, item))
				return 0; // realloc worked and CPU ok
		}
	
	return -1;
}

/*
 *  pickPidCheckBuffer(): process PID runtime overrun,
 *
 *  Arguments: - item to check
 * 			   - required extra buffer time
 *
 *  Return value: error code, 0 = success (ok), 1 = re-scheduling needed
 */
static int
pickPidCheckBuffer(node_t * item, uint64_t ts, uint64_t extra_rt){

	uint64_t usedtime = 0;

	// find all matching, test if space is enough
	for (node_t *citem = nhead; ((citem)); citem=citem->next){
		if (citem->mon.assigned != item->mon.assigned || 0 > citem->pid)
			continue;

		// !! WARN, works only for deadline scheduled tasks
		if (citem->mon.deadline && citem->attr.sched_period
				&& citem->mon.deadline <= item->mon.deadline){
			// dl present and smaller than next dl of item

			uint64_t stdl = citem->mon.deadline;

			// check how often period fits, add time
			while (stdl < item->mon.deadline){
				stdl += citem->attr.sched_period;
				usedtime += citem->attr.sched_runtime;
			}
		}
	}

	// if remaining time is enough, return 0
	return 0 <= (item->mon.deadline - ts - usedtime - extra_rt);
}

/*
 *  pickPidAddRuntimeHist(): Add runtime to histogram, init if needed
 *
 *  Arguments: - item with data for runtime
 *
 *  Return value: -
 */
static void
pickPidAddRuntimeHist(node_t *item){
	// ---------- Add to histogram  ----------
	if (!(item->mon.pdf_hist)){
		// base for histogram, runtime parameter
		double b = (double)item->attr.sched_runtime;
		// --, try prefix if none loaded
		if (0.0 == b && item->param && item->param->attr)
			b = (double)item->param->attr->sched_runtime;
		// -- fall-back to last runtime
		if (0.0 == b && item->mon.cdf_runtime)
			b = (double)item->mon.cdf_runtime;
		if (0.0 == b)
			b = (double)item->mon.dl_rt;

		if ((runstats_histInit(&(item->mon.pdf_hist), b/(double)NSEC_PER_SEC)))
			warn("Histogram init failure for PID %d %s runtime", item->pid, (item->psig) ? item->psig : "");
	}

	double b = (double)item->mon.dl_rt/(double)NSEC_PER_SEC; // transform to sec
	int ret;
	printDbg(PFX "Runtime for PID %d %s %f\n", item->pid, (item->psig) ? item->psig : "", b);
	if ((ret = runstats_histAdd(item->mon.pdf_hist, b)))
		if (ret != 1) // GSL_EDOM
			warn("Histogram increment error for PID %d %s runtime", item->pid, (item->psig) ? item->psig : "");

	// ---------- Compute diffs and averages  ----------

	// exponentially weighted moving average, alpha = 0.9
	item->mon.dl_diffavg = (item->mon.dl_diffavg * 9 + item->mon.dl_diff /* *1 */)/10;
	item->mon.dl_diffmin = MIN (item->mon.dl_diffmin, item->mon.dl_diff);
	item->mon.dl_diffmax = MAX (item->mon.dl_diffmax, item->mon.dl_diff);

	item->mon.rt_min = MIN (item->mon.rt_min, item->mon.dl_rt);
	item->mon.rt_max = MAX (item->mon.rt_max, item->mon.dl_rt);
	item->mon.rt_avg = (item->mon.rt_avg * 9 + item->mon.dl_rt /* *1 */)/10;

	// reset counter, done with statistics, task in sleep (suspend)
	item->mon.dl_rt = 0;

}

/*
 *  pickPidConsolidateRuntime(): update runtime data, init stats when needed
 *
 *  Arguments: - item to update
 * 			   - last time stamp
 *
 *  Return value: -
 */
static void
pickPidConsolidateRuntime(node_t *item, uint64_t ts){

	if (SCHED_DEADLINE == item->attr.sched_policy){

		// ---------- Check first if it is a valid period ----------

		if (!item->attr.sched_period)
			updatePidAttr(item);
		// period should never be zero from here on

		// compute deadline -> what if we read the debug output here??.. maybe we lost track of deadline?
		if (!item->mon.deadline)
			if (get_sched_info(item))			 // update deadline from debug buffer
				warn("Unable to read schedule debug buffer!");

		if (item->mon.deadline > ts){
			item->mon.dl_rt += (ts - item->mon.last_ts);
			return;
		}

		// ----------  period ended ----------

		// just add a period, we rely on periodicity
		item->mon.deadline += MAX( item->attr.sched_period, 1000); // safety..

		uint64_t count = 1;
		while (item->mon.deadline < ts){	 // after update still not in line? (buffer updates 10ms)
			item->mon.deadline += MAX( item->attr.sched_period, 1000); // safety..
			item->mon.dl_scanfail++;
			count++;
		}

		/*
		 * Check if we had a overrun and verify buffers
		 */
		if ((SM_DYNSIMPLE <= prgset->sched_mode)
				&& (item->mon.cdf_runtime && (item->mon.dl_rt > item->mon.cdf_runtime))){
			// check reschedule?
			if (0 < pickPidCheckBuffer(item, ts, item->mon.dl_rt - item->mon.cdf_runtime)){
				// reschedule
				item->mon.dl_overrun++;	// exceeded buffer
				pickPidReallocCPU(item->mon.assigned, item->mon.deadline);
			}
		}

		item->mon.dl_rt /= count; // if we had multiple periods we missed, divive
		if (item->mon.dl_rt){
			// statistics about variability
			item->mon.dl_diff = item->attr.sched_runtime - item->mon.dl_rt;
			pickPidAddRuntimeHist(item);
		}
		// reset runtime to new value
		if (item->mon.last_ts > 0) // compute runtime
			item->mon.dl_rt = (ts - item->mon.last_ts);
	}
	else{
		// if not DL use estimated value
		item->mon.dl_rt += (ts - item->mon.last_ts);
		item->mon.dl_diff = item->mon.cdf_runtime - item->mon.dl_rt;
		pickPidAddRuntimeHist(item);
	}
}

/*
 *  pickPidCommon(): process PID fTrace common header
 *
 *  Arguments: - frame address containing the runtime info
 *             - fTrace thread info
 * 			   - last time stamp
 *
 *  Return value: error code, 0 = success
 */
static int
pickPidCommon(const void * addr, const struct ftrace_thread * fthread, uint64_t ts) {

	//thread information flags, probable meaning
	//#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
	//#define TIF_NOTIFY_RESUME	1	/* callback before returning to user */
	//#define TIF_SIGPENDING	2	/* signal pending */
	//#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
	//#define TIF_SINGLESTEP	4	/* reenable singlestep on user return*/
	//#define TIF_SSBD			5	/* Speculative store bypass disable */
	//#define TIF_SYSCALL_EMU	6	/* syscall emulation active */
	//#define TIF_SYSCALL_AUDIT	7	/* syscall auditing active */

	// use local copy and add addr's address with its offset
	struct tr_common frame = tr_common;
	// NOTE:  we inherit const void from addr
	for (const void ** ptr = (void*)&frame; ptr < (const void **)(&frame + 1); ptr++)
		// add addr
		*ptr = addr + *(int32_t*)ptr;

	(void)pthread_mutex_lock(&dataMutex);

	// find PID = actual running PID
	for (node_t * item = nhead; ((item)); item=item->next )
		// find next PID and put timeStamp last seen, compute period if last time ended
		if (item->pid == *frame.common_pid){
			if (!(*frame.common_flags & 0x8)){ // = NEED_RESCHED requested by running task
				item->status |=  MSK_STATNRSCH;
			}
		}
	(void)pthread_mutex_unlock(&dataMutex);

	// print here to have both line together
	printDbg( "[%lu.%09lu] type=%u flags=%x preempt=%u pid=%d\n", ts/NSEC_PER_SEC, ts%NSEC_PER_SEC,
			*frame.common_type, *frame.common_flags, *frame.common_preempt_count, *frame.common_pid);

	return 0;  // TODO: Fix return values
}

/*
 *  pickPidInfoS(): process PID fTrace sched_switch
 * 					update data with kernel tracer debug out
 *
 *  Arguments: - frame containing the runtime info
 *             - fTrace thread info
 * 			   - last time stamp
 *
 *  Return value: error code, 0 = success
 */
static int
pickPidInfoS(const void * addr, const struct ftrace_thread * fthread, uint64_t ts) {

	int ret1 = pickPidCommon(addr, fthread, ts);

	// use local copy and add addr's address with its offset
	struct tr_switch frame = tr_switch;
	// NOTE:  we inherit const void from addr
	for (const void ** ptr = (void*)&frame; ptr < (const void **)(&frame + 1); ptr++)
		// add addr
		*ptr = addr + *(int32_t*)ptr;

	printDbg("    prev_comm=%s prev_pid=%d prev_prio=%d prev_state=%ld ==> next_comm=%s next_pid=%d next_prio=%d\n",
				frame.prev_comm, *frame.prev_pid, *frame.prev_prio, *frame.prev_state,

				//				(*frame.prev_state & ((((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) - 1))
				//				? __print_flags(*frame.prev_state & ((((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) - 1),"|", { 0x0001, "S" }, { 0x0002, "D" }, { 0x0004, "T" }, { 0x0008, "t" }, { 0x0010, "X" }, { 0x0020, "Z" }, { 0x0040, "P" }, { 0x0080, "I" }) : "R",
				//						*frame.prev_state & (((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) ? "+" : "",

//				(*frame.prev_state & 0xFF ? __print_flags(*frame.prev_state & 0xFF,"|",
//						*frame.prev_state & 0x100 ? "+" : "",

				frame.next_comm, *frame.next_pid, *frame.next_prio);

	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	// find PID switching from
	for (node_t * item = nhead; ((item)); item=item->next ){

		// previous or next pid in list, update data
		if ((item->pid == *frame.prev_pid)
				|| (item->pid == *frame.next_pid)){

			// check if CPU changed, exiting
			if (item->mon.assigned != fthread->cpuno){
				// change on exit???, reassign CPU?
				int32_t CPU = item->mon.assigned;
				item->mon.assigned = fthread->cpuno;

				// Removed from, should give no issues
				if (0 > recomputeCPUTimes(CPU))
					if (SM_DYNSIMPLE <= prgset->sched_mode)
						pickPidReallocCPU(CPU, 0);

				if (0 <= CPU)
					item->mon.resched++;
				else
					// not assigned by orchestrator -> it sets assigned in setPidResources_u
					item->status |= MSK_STATNAFF;
			}

		}

		// find next PID and put timeStamp last seen, compute period if last time ended
		if (item->pid == *frame.next_pid)
			item->mon.last_ts = ts;
	}

	// recompute actual CPU, new tasks might be there now
	if (0 > recomputeCPUTimes(fthread->cpuno))
		if (SM_DYNSIMPLE <= prgset->sched_mode)
			pickPidReallocCPU(fthread->cpuno, 0);

	// find PID switching from
	for (node_t * item = nhead; ((item)); item=item->next )
		// previous PID in list, exiting, update runtime data
		if (item->pid == *frame.prev_pid){

			if (item->status & MSK_STATNAFF){
				// unassigned CPU was not part of adaptive table
				if (SCHED_NODATA == item->attr.sched_policy)
						updatePidAttr(item);
				// never assigned to a resource and we have data (SCHED_DL), check for fit
				if (SCHED_DEADLINE == item->attr.sched_policy) {
					if (0 > pidReallocAndTest(checkPeriod_R(item, 0),
							getTracer(fthread->cpuno), item))
						warn("Unsuccessful first allocation of DL task PID %d %s", item->pid, (item->psig) ? item->psig : "");
				}
			}

			// TODO: check _l vs. 64 bit
			if ((*frame.prev_state & 0x00FD) // ~'D' uninterruptible sleep -> system call
				|| (SCHED_DEADLINE == item->attr.sched_policy)) {
				// update real-time statistics and consolidate other values
				pickPidConsolidateRuntime(item, ts);

				// period histogram and CDF, update on actual switch
				if ((SM_PADAPTIVE <= prgset->sched_mode)
						&& (SCHED_DEADLINE != item->attr.sched_policy)
	//					&& (item->status & MSK_STATNRSCH)	// task asked for, NEED_RESCHED
						){

					if (item->mon.last_tsP){

						double period = (double)(ts - item->mon.last_tsP)/(double)NSEC_PER_SEC;

						if (!(item->mon.pdf_phist)){
							if ((runstats_histInit(&(item->mon.pdf_phist), period)))
								warn("Histogram init failure for PID %d %s period", item->pid, (item->psig) ? item->psig : "");
						}

						printDbg(PFX "Period for PID %d %s %f\n", item->pid, (item->psig) ? item->psig : "", period);
						if ((runstats_histAdd(item->mon.pdf_phist, period)))
							warn("Histogram increment error for PID %d %s period", item->pid, (item->psig) ? item->psig : "");
					}
					item->status &= ~MSK_STATNRSCH;
					item->mon.last_tsP = ts;
				}
			}
			else
				if (item->mon.last_ts > 0) // compute runtime
					item->mon.dl_rt += (ts - item->mon.last_ts);

			break;
		}

	(void)pthread_mutex_unlock(&dataMutex);

	return ret1 + sizeof(struct tr_switch); // TODO: Fix return values
}

/*
 *  pickPidInfoW(): process PID fTrace wakeup
 * 					update data with kernel tracer debug out
 *
 *  Arguments: - frame containing the runtime info
 *             - fTrace thread info
 * 			   - last time stamp
 *
 *  Return value: error code, 0 = success
 */
static int
pickPidInfoW(const void * addr, const struct ftrace_thread * fthread, uint64_t ts) {

	int ret1 = pickPidCommon(addr, fthread, ts);

	// use local copy and add addr's address with its offset
	struct tr_wakeup frame = tr_wakeup;
	// NOTE:  we inherit const void from addr
	for (const void ** ptr = (void*)&frame; ptr < (const void **)(&frame + 1); ptr++)
		// add addr
		*ptr = addr + *(int32_t*)ptr;


	printDbg("    comm=%s pid=%d prio=%d success=%03d target_cpu=%03d\n",
				frame.comm, *frame.pid, *frame.prio, *frame.success, *frame.target_cpu);

	return ret1 + sizeof(struct tr_wakeup); // TODO: Fix return values
}

/*
 *  thread_ftrace(): parse kernel tracer output
 *
 *  Arguments: - pointer to fTrace thread info
 *
 *  Return value: pointer to error code, 0 = success
 */
void *
thread_ftrace(void *arg){

	int pstate = 0;
	int ret = 0;
	FILE *fp = NULL;
	const struct ftrace_thread * fthread = (struct ftrace_thread *)arg;
	int * retVal = malloc (sizeof(int)); // Thread return value in heap
	*retVal = 0;

	unsigned char buffer[PIPE_BUFFER];
	void *pEvent = NULL;
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
			*retVal = errno;
			return retVal;
		}
	} // END interrupt handler block

	{
		 sigset_t set;
		/* Block all signals except SIGQUIT */
		if ( ((sigfillset(&set)))
			|| ((sigdelset(&set, SIGQUIT)))
			|| (0 != pthread_sigmask(SIG_BLOCK, &set, NULL))){
			perror ("Setup of sigmask failed");
			*retVal = errno;
			return retVal;
		}
	}

	while(1) {

		switch( pstate )
		{
		case 0:
			// init buffer structure for page management
			kbuf = kbuffer_alloc(WORDSIZE, KBUFFER_ENDIAN_LITTLE);

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
				*retVal = errno;
				free(fn);
				break;
			} /** if file doesn't exist **/

			if ((fp = fopen (fn, "r")) == NULL) {
				pstate = -1;
				err_msg ("File open failed");
				*retVal = errno;
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
					*retVal = errno;
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
				int (*eventcall)(const void *, const struct ftrace_thread *, uint64_t) = pickPidCommon; // default to common for unknown formats

				for (struct ftrace_elist * event = elist_head; ((event)); event=event->next)
					// check for ID, first value is 16 bit ID
					if (event->eventid == *(uint16_t*)pEvent){
						eventcall = event->eventcall;
						break;
					}

				// call event TODO: fix return values
				int count = eventcall(pEvent, fthread, timestamp);
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
			printf(PFX "Exit fTrace CPU%d thread\n", fthread->cpuno);
			fflush(stderr);
			return retVal;
		}
	}

}

// #################################### THREAD specific END ############################################


/*
 *  updateSiblings(): check if the item is - has - the primary sibling
 *  				  update all and siblings if there is a better fit
 *
 *  Arguments: - item that triggered update request
 *
 *  Return value: -1 don't touch, 0 = main or no siblings
 */
static int
updateSiblings(node_t * node){

	node_t * mainp = node;

	if (// (node->status & MSK_STATSIBL) && // removed temporarily as flag is not reliable
			(node->param)
			&& (node->param->cont)){

		uint64_t smp = NSEC_PER_SEC;
		for (node_t * item = nhead; ((item)); item=item->next ){
			if (0 < item->pid && item->param && item->param->cont
					&& item->param->cont == node->param->cont){
				if (SCHED_DEADLINE == item->attr.sched_policy){
					if (item->attr.sched_period < smp){
						mainp = item;
						smp = item->attr.sched_period;
					}
				}
				else
					if (policy_is_realtime(item->attr.sched_policy)
							&& (item->mon.cdf_period)	// the "periodic" task is fastest, and 0 siblings are to ignore
							&& item->mon.cdf_period < smp){
						mainp = item;
						smp = item->mon.cdf_period;
					}
			}
		}
	}

	if (mainp != node) // we are not the main task
		return -1;

	// ELSE update all TIDs
	return pidReallocAndTest(checkPeriod_R(mainp, 0),
			getTracer(mainp->mon.assigned), node);
}

/*
 *  get_sched_info(): get scheduler debug output info
 *
 *  Arguments: the node to get info for
 *
 *  Return value: error code, 0 = success
 */
static int
get_sched_info(node_t * item)
{
#ifdef DEBUG
	int ret;
	struct timespec start, end;

	// get clock, use it as a future reference for update time TIMER_ABS*
	ret = clock_gettime(clocksources[prgset->clocksel], &start);
	if (0 != ret) {
		if (EINTR != ret)
			warn("clock_gettime() failed: %s", strerror(errno));
	}
#endif

	char szFileName [_POSIX_PATH_MAX];
	char szStatBuff [PIPE_BUFFER];
	char ltag [80]; // just tag of beginning, max length expected ~30
    char *s, *s_ptr;

	FILE *fp;

	(void)sprintf (szFileName, "/proc/%u/sched", (unsigned) item->pid);

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
		if ((SCHED_RR == item->attr.sched_policy)
			|| (SCHED_FIFO == item->attr.sched_policy)){
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
		}

		// ---------- SCHED_DEADLINE --------------
		if (SCHED_DEADLINE == item->attr.sched_policy) {

			if (!prgset->ftrace // do not parse runtime if on ftrace
					&& (strncasecmp(ltag, "dl.runtime", 4) == 0)) {
				// store last seen runtime
				ltrt = num;
				if (num != item->mon.dl_rt)
					item->mon.dl_count++;
			}
			if (strncasecmp(ltag, "dl.deadline", 4) == 0)	{
				if (0 == item->mon.deadline)
					item->mon.deadline = num;
				else if (num != item->mon.deadline) {
					// it's not, updated deadline found
					if (!prgset->ftrace){ // only if not from ftrace call

						// calculate difference to last reading, should be 1 period
						diff = (int64_t)(num-item->mon.deadline)-(int64_t)item->attr.sched_period;

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
							printDbg("\nPID %d %s Deadline overrun by %ldns, sum %ld\n",
								item->pid, (item->psig) ? item->psig : "", diff, item->mon.dl_diff);
						}

						item->mon.dl_diff += diff;
						item->mon.dl_diffmin = MIN (item->mon.dl_diffmin, diff);
						item->mon.dl_diffmax = MAX (item->mon.dl_diffmax, diff);

						// exponentially weighted moving average, alpha = 0.9
						item->mon.dl_diffavg = (item->mon.dl_diffavg * 9 + diff /* *1 */)/10;

						// runtime replenished - deadline changed: old value may be real RT ->
						// Works only if scan time < slack time
						// and if not, this here filters the hole (maybe)
						diff = (int64_t)item->attr.sched_runtime - item->mon.dl_rt;
						if (!((int64_t)item->attr.sched_runtime - ltrt) && diff){
							item->mon.rt_min = MIN (item->mon.rt_min, diff);
							item->mon.rt_max = MAX (item->mon.rt_max, diff);
							item->mon.rt_avg = (item->mon.rt_avg * 9 + diff /* *1 */)/10;
						}
					}

					item->mon.deadline = num;
				}

				// update last seen runtime
				if (!prgset->ftrace)
					item->mon.dl_rt = ltrt;
				break; // we're done reading
			}
		}

		// Advance with token
		s = strtok_r (NULL, "\n", &s_ptr);	
	}

#ifdef DEBUG
	// get clock, use it as a future reference for update time TIMER_ABS*
	ret = clock_gettime(clocksources[prgset->clocksel], &end);
	if (0 != ret) {
		if (EINTR != ret)
			warn("clock_gettime() failed: %s", strerror(errno));
	}

	// compute difference -> time needed
	end.tv_sec -= start.tv_sec;
	end.tv_nsec -= start.tv_nsec;
	tsnorm(&end);

	printDbg(PFX "%s parse time: %ld.%09ld\n", __func__, end.tv_sec, end.tv_nsec);
#endif

  return 0;
}

/*
 * updateStats(): update the real time statistics for all scheduled threads
 *  -- used for monitoring purposes ---
 *
 *  Arguments: -
 *
 *  Return value: returns 1 if a statistics update is needed
 */
static int
updateStats()
{
	fflush(stdout);

	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	scount++; // increase scan-count

	// for now does only a simple update
	for (node_t * item = nhead; ((item)); item=item->next ) {
		// skip deactivated tracking items
		// skip PID 0, = undefined or ROOT PID (swapper/sched)
		if (0 >= item->pid)
			continue;

		// update only when defaulting -> new entry, or every 100th scan
		if (!(scount%prgset->loops)
			|| (SCHED_NODATA == item->attr.sched_policy))
			updatePidAttr(item);

		/*  Curve Fitting from here, for now every second (default) */

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

	return !(( scount % (prgset->loops*10) ));	// return 1 if we passed 10th time loops
}

/*
 * manageSched(): main function called to update resources
 * 					called once out of 10* loops (less often..)
 *
 * Arguments: -
 *
 * Return value: N/D - int
 */
static int
manageSched(){

	// this is for the dynamic and adaptive scheduler only

	// test disabling throttle again if it didn't work.
	if (!(prgset->status & MSK_STATTRTL)){
		// set even if not successful. stop trying
		(void)resetRTthrottle(prgset, -1);
		prgset->status |= MSK_STATTRTL;
	}

	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	// for now does only a simple update
	for (node_t * item = nhead; ((item)); item=item->next ) {
		if (0 > item->pid)
			continue;

		// update CMD-line
		updatePidCmdline(item);

		if (SM_PADAPTIVE <= prgset->sched_mode){
			if (!(runstats_histCheck(item->mon.pdf_phist))){
				if ((SCHED_DEADLINE != item->attr.sched_policy)){

					uint64_t newPeriod = (uint64_t)(NSEC_PER_SEC *
							runstats_histMean(item->mon.pdf_phist)); // use simple mean as periodicity depends on other tasks

					(void)runstats_histFit(&item->mon.pdf_phist);

					// period changed enough for a different time-slot?
					if (findPeriodMatch(item->mon.cdf_period) != findPeriodMatch(newPeriod)
							&& newPeriod > item->mon.cdf_runtime){
						if (item->mon.cdf_period * 95 > newPeriod * 100
								|| item->mon.cdf_period * 105 < newPeriod * 100){
							// meaningful change?
							info("Update PID %d %s period: %luus", item->pid, (item->psig) ? item->psig : "", newPeriod/1000);
							item->mon.resample++;
						}
						item->mon.cdf_period = newPeriod;
						// check if there is a better fit for the period, and if it is main
						if (0 > updateSiblings(item))
							warn("PID %d %s Sibling update not possible!", item->pid, (item->psig) ? item->psig : "");
					}
					else
						item->mon.cdf_period = newPeriod;
					}
				}


			if (!(runstats_histCheck(item->mon.pdf_hist))){
				// if histogram is set and count is ok, update and fit curve

				uint64_t newWCET = 0;
				int ret;

				switch (prgset->sched_mode) {

				default:
				case SM_PADAPTIVE:
					// ADAPTIVE KEEP SIX-SIGMA for Deadline tasks
					if (SCHED_DEADLINE == item->attr.sched_policy){
						newWCET = (uint64_t)(NSEC_PER_SEC *
										runstats_histSixSigma(item->mon.pdf_hist));
						if (item->param && item->param->attr &&
								(item->param->attr->sched_runtime)) // max double initial WCET
							newWCET = MIN (item->param->attr->sched_runtime * 2, newWCET);
					}
					else
						// Otherwise, fifo ecc
						newWCET = (uint64_t)(NSEC_PER_SEC *
									runstats_histMean(item->mon.pdf_hist));
					break;

				case SM_DYNSIMPLE:
				case SM_DYNMCBIN:
					// DYNAMIC, USE PROBABILISTIC WCET VALUE
					if (!(ret = runstats_cdfCreate(&item->mon.pdf_hist, &item->mon.pdf_cdf))){

						if (SCHED_DEADLINE == item->attr.sched_policy)
							newWCET = (uint64_t)(NSEC_PER_SEC *
										runstats_cdfSample(item->mon.pdf_cdf, prgset->ptresh));
						else
							// Otherwise, fifo ecc
							newWCET = (uint64_t)(NSEC_PER_SEC *
										runstats_histMean(item->mon.pdf_hist));
					}
					else
						if (ret != 0)
						{
							// something went wrong
							if (!(item->status & MSK_STATHERR))
								warn("CDF initialization/range error for PID %d %s", item->pid, (item->psig) ? item->psig : "");
							item->status |= MSK_STATHERR;
						}

					break;
				}

				if (0 < newWCET){
					if (SCHED_DEADLINE == item->attr.sched_policy){
						updatePidWCET(item, newWCET);
					}
					if ( item->mon.cdf_runtime * 95 > newWCET * 100
							|| item->mon.cdf_runtime * 105 < newWCET * 100){
						// meaningful change?
						info("Update PID %d %s runtime: %luus", item->pid, (item->psig) ? item->psig : "", newWCET/1000);
						item->mon.resample++;
					}
					item->mon.cdf_runtime = newWCET;
					item->status &= ~MSK_STATHERR;
				}
				else
					warn ("Estimation error, can not update WCET");

				(void)runstats_histFit(&item->mon.pdf_hist);
			}
		}
    }

	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		if (0.0 != trc->U) // ignore 0 min CPU
			trc->Umin = MIN (trc->Umin, trc->U);
		trc->Umax = MAX (trc->Umax, trc->U);
		if (0.0 == trc->Uavg && 0.0 != trc->U)
			trc->Uavg = trc->U;
		else
			trc->Uavg = trc->Uavg * 0.9 + trc->U * 0.1;
	}

	(void)pthread_mutex_unlock(&dataMutex);

	return 0;
}

/*
 * dumpStats(): prints thread statistics to out
 *
 * Arguments: -
 *
 * Return value: -
 */
static void
dumpStats (){

	node_t * item = nhead;
	(void)printf( "\nStatistics for real-time SCHED_DEADLINE, FIFO and RR PIDs, %ld scans:"
					" (others are omitted)\n"
					"Average exponential with alpha=0.9\n\n"
					"PID - Rsh - Smpl - Cycle Overruns(total/found/fail) - avg rt (min/max) - sum diff (min/max/avg)\n"
			        "----------------------------------------------------------------------------------\n",
					scount );

	// find first matching
	while ((item) && !policy_is_realtime(item->attr.sched_policy))
		item=item->next;

	// no PIDs in list
	if (!item) {
		(void)printf("(no PIDs)\n");
		return;
	}

	for (;((item)); item=item->next)
		switch(item->attr.sched_policy){
		case SCHED_FIFO:
		case SCHED_RR:
			(void)printf("%5d%c: %3ld-%5ld-%3ld(%ld/%ld/%ld) - %ld(%ld/%ld) - %s - %s\n",
				abs(item->pid), item->pid<0 ? '*' : ' ',
				item->mon.resched, item->mon.resample,  item->mon.dl_overrun, item->mon.dl_count+item->mon.dl_scanfail,
				item->mon.dl_count, item->mon.dl_scanfail,
				item->mon.rt_avg, item->mon.rt_min, item->mon.rt_max,
				policy_to_string(item->attr.sched_policy),
				(item->psig) ? item->psig : "");
			break;

		case SCHED_DEADLINE:
			(void)printf("%5d%c: %3ld-%5ld-%3ld(%ld/%ld/%ld) - %ld(%ld/%ld) - %ld(%ld/%ld/%ld) - %s\n",
				abs(item->pid), item->pid<0 ? '*' : ' ',
				item->mon.resched, item->mon.resample, item->mon.dl_overrun, item->mon.dl_count+item->mon.dl_scanfail,
				item->mon.dl_count, item->mon.dl_scanfail,
				item->mon.rt_avg, item->mon.rt_min, item->mon.rt_max,
				item->mon.dl_diff, item->mon.dl_diffmin, item->mon.dl_diffmax, item->mon.dl_diffavg,
				(item->psig) ? item->psig : "");
			break;
		default:
			;
		}

	(void)printf( "\nStatistics on resource usage:\n"
				    "CPU : AVG (MIN/MAX)\n"
			        "----------------------------------------------------------------------------------\n");

	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		recomputeCPUTimes(trc->affinity);
		(void)printf( "CPU %d: %3.2f%% (%3.2f%%/%3.2f%%)\n", trc->affinity,
				trc->Uavg * 100, trc->Umin * 100, trc->Umax * 100 );
	}

	fflush(stdout);
}

/*
 *  thread_manage(): thread function call to manage schedule list
 *
 *  Arguments: - thread state/state machine, passed on to allow main thread stop
 *
 *  Return value: Exit Code - o for no error
 */
void *thread_manage (void *arg)
{
	// be explicit!
	int32_t* pthread_state = (int32_t *)arg;

	int ret;
	struct timespec intervaltv;

	// get clock, use it as a future reference for update time TIMER_ABS*
	ret = clock_gettime(clocksources[prgset->clocksel], &intervaltv);
	if (0 != ret) {
		if (EINTR != ret)
			warn("clock_gettime() failed: %s", strerror(errno));
		*pthread_state=-1;
	}

	// initialize the thread locals
	while(1)
	{
		switch( *pthread_state )
		{
			case 0: // setup thread
			*pthread_state=1; // first thing
			if (prgset->ftrace) {
				(void)printf(PFX "Starting CPU tracing threads\n");
				if (configureTracers()){
					warn("Kernel function tracers not available, RESET!");
					prgset->ftrace = 0; // reset (ok, not read elsewhere)
					resetTracers();
				}
				else
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

		{
			// absolute time relative interval shift

			// calculate next execution interval
			intervaltv.tv_sec += prgset->interval / USEC_PER_SEC;
			intervaltv.tv_nsec+= (prgset->interval % USEC_PER_SEC) * 1000;
			tsnorm(&intervaltv);

			// sleep for interval nanoseconds
			ret = clock_nanosleep(clocksources[prgset->clocksel], TIMER_ABSTIME, &intervaltv, NULL);
			if (0 != ret) {
				// Set warning only.. shouldn't stop working
				// probably overrun, restarts immediately in attempt to catch up
				if (EINTR != ret) {
					warn("clock_nanosleep() failed. errno: %s",strerror (ret));
				}
			}
		}
	}

	(void)printf(PFX "Stopped\n");
	// Start using return value
	return NULL;
}

