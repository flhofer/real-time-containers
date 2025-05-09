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
#ifdef USELIBTRACE
	#include <>kbuffer.h>	// ring-buffer management, use libtrace-event
#endif

// Custom includes
#include "orchestrator.h"

#include "orchdata.h"	// memory structure to store information
#include "kernutil.h"	// generic kernel utilities
#include "error.h"		// error and stderr print functions
#include "cmnutil.h"		// common definitions and functions
#ifndef USELIBTRACE
	#include "kbuffer.h"// ring-buffer management extracted from source libtrace-event
#endif
#include "resmgnt.h"		// PID and resource management

#include <numa.h>		// NUMA node identification

#undef PFX
#define PFX "[manage] "

#define PIPE_BUFFER			4096

#define GET_VARIABLE_NAME(Variable) (#Variable)

#define ALPHAAVG_SECONDS	300	// how many seconds we want to "go back"

// total scan counter for update-stats
static uint64_t scount = 0; // total scan count
// alpha for averaging
static float alphaAVG = 0.99998;

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
#define TR_EVENT_COMMON "common"
struct tr_common {
	uint16_t* common_type;
	uint8_t * common_flags;
	uint8_t * common_preempt_count;
	int32_t * common_pid;
} tr_common;
// related variable name dictionary
const char * const tr_common_dict[] = { TR_EVENT_COMMON,
										GET_VARIABLE_NAME(tr_common.common_type),
										GET_VARIABLE_NAME(tr_common.common_flags),
										GET_VARIABLE_NAME(tr_common.common_preempt_count),
										GET_VARIABLE_NAME(tr_common.common_pid),
										NULL};

// Parser offset structures - pointers to values for sched_switch
#define TR_EVENT_SWITCH "sched/sched_switch"
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
const char * const tr_switch_dict[] = { TR_EVENT_SWITCH,
										GET_VARIABLE_NAME(tr_switch.prev_comm),
										GET_VARIABLE_NAME(tr_switch.prev_pid),
										GET_VARIABLE_NAME(tr_switch.prev_prio),
										GET_VARIABLE_NAME(tr_switch.prev_state),
										GET_VARIABLE_NAME(tr_switch.next_comm),
										GET_VARIABLE_NAME(tr_switch.next_pid),
										GET_VARIABLE_NAME(tr_switch.next_prio),
										NULL};

// Parser offset structures - pointers to values for sched_wakeup
#define TR_EVENT_WAKEUP "sched/sched_wakeup"
struct tr_wakeup {
	char  * comm;
	pid_t * pid;
	int32_t * prio;
	int32_t * target_cpu;
} tr_wakeup;
// related variable name dictionary
const char * const tr_wakeup_dict[] = { TR_EVENT_WAKEUP,
										GET_VARIABLE_NAME(tr_wakeup.comm),
										GET_VARIABLE_NAME(tr_wakeup.pid),
										GET_VARIABLE_NAME(tr_wakeup.prio),
										GET_VARIABLE_NAME(tr_wakeup.target_cpu),
										NULL};

// these are data and name dictionaries used for parsing
const char * const * const tr_event_dict [] = { tr_common_dict, tr_switch_dict, tr_wakeup_dict, NULL };
const void * const tr_event_structs [] = { &tr_common, &tr_switch, &tr_wakeup, NULL };

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

	int cmn = 1;	/// run once - always - for common structure, first in array

	const char * const * const * tr_dicts = tr_event_dict;	// working pointer to structure dictionaries, init to first

	// Loop through structures to init, first is 'tr_common" (cmn =1)
	// note type cast below -> structs contain pointers we want to modify
	// (void *) points to pointer to value, (* const) = points to struct, (*) points to array of struct
	for(void ** const * tr_structs = (void ** const *)tr_event_structs;(*tr_structs) && (*tr_dicts); tr_structs++, tr_dicts++ ){

		// Loop through loaded ftrace events to find match
		for (struct ftrace_elist * event = elist_head; (event); event=event->next){

			// Event configuration name matches structure name in dictionary (or common is set for cmn set), find field configs
			if ((cmn) || !strcmp(**tr_dicts,event->event)){
				// match of dict
				void ** tr_struct = *tr_structs;	// init working pointer to memory begin position of struct (first field)
				// loop through dict entries for fields -> find cfgs
				for (const char * const * tr_dict = ((*tr_dicts)+1); (*tr_dict); tr_dict++, tr_struct++){
					char * t_tok;
					char * entry = strdup (*tr_dict);

					(void)strtok_r (entry, ".", &t_tok);
					if (t_tok)
						// loop through field cfg to find match for field in struct
						for (struct ftrace_ecfg * cfg = event->fields; (cfg); cfg = cfg->next)
							if (!strcmp(t_tok, cfg->name)){
								// set offset
								*tr_struct = (unsigned char*)0x0 + cfg->offset;
								break;
							}
					free(entry);
				}
				cmn = 0;
				break;
			}
		}
	}

	return -cmn;	// at least common has been parsed?
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
	int ret = 0;

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
							else if (!strcmp(t,"long")){
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
							printDbg(PFX "ftrace event parse - Field '%s' size mismatch, type %d bytes, value %d bytes!\n", (*ecfg)->name, (*ecfg)->size, size);
						(*ecfg)->size=size;
						fp++;
					}
					break;
				case 5:	// signed:
					if(!strcmp(s, "signed")){
						s = strtok_r (NULL, delim, &s_tok);
						int sign = atoi(s);
						if (sign != (*ecfg)->sign)
							printDbg(PFX "ftrace event parse - Field '%s' sign mismatch, type %s, value %s!\n", (*ecfg)->name, ((*ecfg)->sign)?"signed":"unsigned", (sign)?"signed":"unsigned");
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
					else{
						printDbg(PFX "ftrace event parse - Structure mismatch; expecting 'field', got '%s'!\n", s);
						ret = -1;
					}
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
			printDbg(PFX "ftrace event parse - Structure mismatch; unexpected 'print'\n");
			ret = -1;
		}
		else
			printDbg(PFX "ftrace event parse - Unknown token %s\n", s);

		// Next Token
		s = strtok_r (NULL, delim, &s_tok);

	}
	return ret;
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
	if (0 < setkernvar(path, "enable", "1", prgset->dryrun & MSK_DRYNOTRCNG)) {
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
			if ( 0 < getkernvar(path, "format", buf, sizeof(buf))){
				if (parseEventFields(&elist_head->fields, buf))
					warn("Unable to parse event format for '%s'", event);
			}
			else{
				warn("Unable to get event format for '%s'", event);
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

	if (prgset->dryrun & MSK_DRYNOTRCNG)
		warn("Changing of tracer settings disabled. Expect malfunction!");

	if ( 0 > setkernvar(dbgpfx, "tracing_on", "0", prgset->dryrun & MSK_DRYNOTRCNG))
		warn("Can not disable kernel function tracing");

	if ( 0 > setkernvar(dbgpfx, "events/enable", "0", prgset->dryrun & MSK_DRYNOTRCNG))
		warn("Unable to clear kernel fTrace event list");

	{ // get CPU-set in hex for tracing
		char trcpuset[129]; // enough for 512 CPUs
		if (!parse_bitmask_hex(prgset->affinity_mask, trcpuset, sizeof(trcpuset))){
			if (0 > setkernvar(dbgpfx, "tracing_cpumask", trcpuset, prgset->dryrun & MSK_DRYNOTRCNG) )
				warn("Unable to set tracing CPU-set");
		}
		else
			warn("can not obtain HEX CPU mask");
	}

	if ((appendEvent(dbgpfx, TR_EVENT_SWITCH, pickPidInfoS)))
		return -1;

	if ((appendEvent(dbgpfx, TR_EVENT_WAKEUP, pickPidInfoW)))
		return -1;

	if ((parseEventOffsets()))
		return -1;

	if ( 0 > setkernvar(dbgpfx, "tracing_on", "1", prgset->dryrun & MSK_DRYNOTRCNG)){
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

	if ( 0 > setkernvar(dbgpfx, "tracing_on", "0", prgset->dryrun & MSK_DRYNOTRCNG))
		warn("Can not disable kernel function tracing");

	if (0 > setkernvar(dbgpfx, "events/enable", "0", prgset->dryrun & MSK_DRYNOTRCNG))
		warn("Unable to clear kernel fTrace event list");

	// sched_stat_runtime tracer seems to need sched_stats
	if (0 > setkernvar(prgset->procfileprefix, "sched_schedstats", "0", prgset->dryrun & MSK_DRYNOTRCNG))
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

					item->mon.assigned = getTracerMainCPU(ntrc);
					if (!setPidAffinityAssinged (item)){
						item->mon.resched++;
						continue;
					}
					if (trc){ // Reallocate did not work, undo if possible
						item->mon.assigned = getTracerMainCPU(trc);
						for (node_t * bitem = nhead; ((bitem)) && bitem != item; bitem=bitem->next)
							if (0 < bitem->pid && bitem->param && bitem->param->cont
									&& bitem->param->cont == item->param->cont){
								bitem->mon.assigned = getTracerMainCPU(trc);
								(void)setPidAffinityAssinged (bitem);
							}
					}
					return -1;
				}

		// all done, recompute CPU-times
		(void)recomputeTimes(ntrc);
		if ((trc) && 0 > recomputeTimes(trc))
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

		if (citem->mon.deadline
				&& (citem->attr.sched_period || citem->mon.cdf_period)
				&& citem->mon.deadline <= item->mon.deadline){
			// dl present and smaller than next dl of item

			uint64_t stdl = citem->mon.deadline;

			// check how often period fits, add time
			while (stdl < item->mon.deadline){
				stdl += citem->attr.sched_period + citem->mon.cdf_period; 		// one of them is empty
				usedtime += (citem->mon.cdf_runtime) ? 							// if estimation OK, use that value (ptresh!) instead of WCET for DL
						citem->mon.cdf_runtime : citem->attr.sched_runtime;
			}
		}
	}

	// if remaining time is enough, return 0
	return (item->mon.deadline < ts + usedtime + extra_rt);
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
			b = (double)item->mon.rt;

		if ((runstats_histInit(&(item->mon.pdf_hist), b/(double)NSEC_PER_SEC)))
			warn("Histogram init failure for PID %d '%s' runtime", item->pid, (item->psig) ? item->psig : "");
	}

	double b = (double)item->mon.rt/(double)NSEC_PER_SEC; // transform to sec
	int ret;
	printDbg(PFX "Runtime for PID %d '%s' %f\n", item->pid, (item->psig) ? item->psig : "", b);
	if ((ret = runstats_histAdd(item->mon.pdf_hist, b)))
		if (ret != 1) // GSL_EDOM
			warn("Histogram increment error for PID %d '%s' runtime", item->pid, (item->psig) ? item->psig : "");

	// ---------- Compute diffs and averages  ----------

	// exponentially weighted moving average, alpha = 0.9
	item->mon.dl_diffavg = (item->mon.dl_diffavg * 9 + item->mon.dl_diff /* *1 */)/10;
	item->mon.dl_diffmin = MIN (item->mon.dl_diffmin, item->mon.dl_diff);
	item->mon.dl_diffmax = MAX (item->mon.dl_diffmax, item->mon.dl_diff);

	if (!item->mon.rt_avg)
		item->mon.rt_avg = item->mon.rt;
	else
		item->mon.rt_avg = (item->mon.rt_avg * 9 + item->mon.rt /* *1 */)/10;
	item->mon.rt_min = MIN (item->mon.rt_min, item->mon.rt);
	item->mon.rt_max = MAX (item->mon.rt_max, item->mon.rt);

	// reset counter, done with statistics, task in sleep (suspend)
	item->mon.rt = 0;
}

/*
 *  pickPidConsolidatePeriod(): update runtime data, init stats when needed
 *
 *  This function is called at end of NON-DL tasks (e.g. timer-wait) period
 *  or every time a DL tasks is preempted or yields (-> check if new period )
 *
 *  Arguments: - item to update
 * 			   - last time stamp
 *
 *  Return value: -
 */
static void
pickPidConsolidatePeriod(node_t *item, uint64_t ts){

	// failed period increment counter
	int64_t fail_count = 0;

	if (SCHED_DEADLINE == item->attr.sched_policy){

		// ----------  period ended ----------
		item->mon.dl_count++;				// total period counter

		if (!item->attr.sched_period)
			updatePidAttr(item);

		if (!item->mon.deadline){
			if (get_sched_info(item))			 // update deadline from debug buffer
				warn("Unable to read schedule debug buffer!");
			printDbg(PFX "Deadline PID %d %lu read for %lu with buffer %ld", item->pid, item->mon.deadline, ts, (int64_t)item->mon.deadline - (int64_t)ts);
			// sched-debug buffer not always up-to date, 10ms refresh rate
			while ((item->attr.sched_period) && (item->mon.deadline < ts))
				item->mon.deadline += item->attr.sched_period;
		}

		if (item->attr.sched_period) {

			// returned from task after last deadline?
			if ((item->mon.deadline < ts)
				 &&	(item->mon.rt > item->attr.sched_period)) {
				item->mon.dl_overrun++;

				uint64_t rt = item->mon.rt;

				while (rt > item->attr.sched_period){
					rt -= item->attr.sched_period;
					item->mon.deadline += item->attr.sched_period;
					fail_count++;
				}
			}

			// we still didn't reach new value? others may be scanfail
			while (item->mon.deadline < ts){
				item->mon.dl_scanfail++;
				item->mon.deadline += item->attr.sched_period;
				fail_count++;
			}

			// update deadline time-stamp from scheduler debug output if we missed something
			if (fail_count){
				if (get_sched_info(item))			 // update deadline from debug buffer
					warn("Unable to read schedule debug buffer!");
				printDbg(PFX "Deadline PID %d %lu read for %lu with buffer %ld", item->pid, item->mon.deadline, ts, (int64_t)item->mon.deadline - (int64_t)ts);
				// sched-debug buffer not always up-to date, 10ms refresh rate
				while (item->mon.deadline < ts)
					item->mon.deadline += item->attr.sched_period;
			}

			// just add a period, we rely on periodicity - sometimes readout gives the next period
			if ((int64_t)item->mon.deadline - (int64_t)ts < (int64_t)item->attr.sched_period)
				item->mon.deadline += item->attr.sched_period;

		}

		/*
		 * Check if we had a budget overrun and verify buffers
		 */
		if ((SM_DYNSIMPLE <= prgset->sched_mode)
				&& (item->mon.cdf_runtime && (item->mon.rt > item->mon.cdf_runtime))){
			// check reschedule?
			if ((pickPidCheckBuffer(item, ts, (int64_t)item->mon.rt - item->mon.cdf_runtime))){
				// reschedule
				item->mon.dl_overrun++;	// exceeded buffer
				if (pickPidReallocCPU(item->mon.assigned, item->mon.deadline))
					warn("Task overrun - Could not find CPU to reschedule for PID %d", item->pid);
			}
		}
		item->status |=	MSK_STATNPRD; // store for tsP evaluation

	}

	if (item->mon.rt && item->mon.last_ts)
		// statistics about variability
		pickPidAddRuntimeHist(item);

	// remove preemptively after statistics as we will have 1 period off
	while (fail_count){
		item->mon.dl_diff -= item->attr.sched_period;
		fail_count--;
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
	//#define FT_unknown 0x20 set on wakeup
	//#define FT_softIRQ 0x10
	//#define FT_hardIRQ 0x8
	//#define FT_needResched 0x4
	//#define FT_irqoff 0x1

	// use local copy and add addr's address with its offset
	struct tr_common frame = tr_common;
	// NOTE:  we inherit const void from addr
	for (const void ** ptr = (void*)&frame; ptr < (const void **)(&frame + 1); ptr++)
		// add addr
		*ptr = addr + *(int32_t*)ptr;

	if (*frame.common_type & 0xF000)	// Malformed! type ~hundreds
		return -1;

	(void)pthread_mutex_lock(&dataMutex);

	// find PID = actual running PID
	for (node_t * item = nhead; ((item)); item=item->next )

		if (item->pid == *frame.common_pid){
			if (!(*frame.common_flags & 0x4)){ // = NEED_RESCHED requested by event on running task  = Task has to go online
				item->status |=  MSK_STATNRSCH;
			}
		}
	(void)pthread_mutex_unlock(&dataMutex);

	// print here to have both line together
	printStat( "[%lu.%09lu] type=%u flags=%x preempt=%u pid=%d\n", ts/NSEC_PER_SEC, ts%NSEC_PER_SEC,
			*frame.common_type, *frame.common_flags, *frame.common_preempt_count, *frame.common_pid);

	return 0;
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

	if(pickPidCommon(addr, fthread, ts)) // malformed common?
		return -1;

	// use local copy and add addr's address with its offset
	struct tr_switch frame = tr_switch;
	// NOTE:  we inherit const void from addr
	for (const void ** ptr = (void*)&frame; ptr < (const void **)(&frame + 1); ptr++)
		// add addr
		*ptr = addr + *(int32_t*)ptr;

#ifdef DEBUG
	char flags[10];

	(void)get_status_flags(*frame.prev_state, flags, sizeof(flags));

	printStat ("    prev_comm=%s prev_pid=%d prev_prio=%d prev_state=%s ==> next_comm=%s next_pid=%d next_prio=%d\n",
				frame.prev_comm, *frame.prev_pid, *frame.prev_prio, flags,
				frame.next_comm, *frame.next_pid, *frame.next_prio);
#endif

	if ((*frame.prev_comm & 0x80) || (*frame.next_comm & 0x80)) // malformed buffer? valid char?
		return -1;

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

				if (0 <= CPU){
					item->mon.resched++;

					// Removed from old CPU, should give no issues
					if (-1 == recomputeCPUTimes(CPU))	// if -2 = CPU not found, i.e. affinity preference, no real affinity set yet, do nothing
						if (SM_DYNSIMPLE <= prgset->sched_mode)
							(void)pickPidReallocCPU(CPU, 0);
				}
				else
					// not assigned by orchestrator -> it sets assigned in setPidResources_u
					item->status |= MSK_STATNAFF;
			}

		}

		// find next PID and put timeStamp last started running
		if (item->pid == *frame.next_pid){
			item->mon.last_ts = ts;

			if (item->status & MSK_STATNPRD){
				// time between DL switches should tell jitter (technically perfect..)
				item->status &= ~MSK_STATNPRD;

				// floating skew=jitter
				if (item->mon.last_tsP)
					item->mon.dl_diff += (int64_t)ts - (int64_t)item->mon.last_tsP - (int64_t)item->attr.sched_period;
				item->mon.last_tsP = ts;			// this period start
			}
		}
	}

	// recompute actual CPU, new tasks might be there now
	if (0 > recomputeCPUTimes(fthread->cpuno))
		if (SM_DYNSIMPLE <= prgset->sched_mode)
			(void)pickPidReallocCPU(fthread->cpuno, 0);

	// find PID switching from
	for (node_t * item = nhead; ((item)); item=item->next )
		// previous PID in list, exiting, update runtime data
		if (item->pid == *frame.prev_pid){

			// unassigned CPU was not part of adaptive table

			if (item->status & MSK_STATNAFF){
				if (SCHED_NODATA == item->attr.sched_policy)
					updatePidAttr(item);
				if (SM_PADAPTIVE <= prgset->sched_mode){
					// never assigned to a resource and we have data (SCHED_DL), check for fit
					if (SCHED_DEADLINE == item->attr.sched_policy) {
						if (0 > pidReallocAndTest(checkPeriod_R(item, 0),
								getTracer(fthread->cpuno), item))
							warn("Unsuccessful first allocation of DL task PID %d '%s'", item->pid, (item->psig) ? item->psig : "");
					}
					else {
						// Set affinity, starting from PAdaptive as it might "correct" the setting, before it doesn't
						if ((1 < getPidAffinityAssingedNr(item)) && (!setPidAffinityAssinged(item)))
							warn("Setting run-time affinity for unassigned PID %d '%s'", item->pid, item->psig ? item->psig : "");
						else
							item->status &= ~MSK_STATNAFF;	// reset - no further need for it as weight is <=1 or set not possible (process exited)
					}
				}
			}

			// update real-time statistics and consolidate other values on period end
			if (item->mon.last_ts)
				item->mon.rt += ts - item->mon.last_ts;

			if (((SCHED_DEADLINE != item->attr.sched_policy)	// not deadline
					|| (*frame.prev_state & 0x0100)				// set preemption
					|| (0 == *frame.next_prio))					// or next is 'migration/x'; always preempts
				&& !(*frame.prev_state & 0x00FD)) 		// Not 'D' = uninterruptible sleep -> system call, nor 'R' = running and preempted
				break;							  		// break here, not final process switch

			pickPidConsolidatePeriod(item, ts);

			break;
		}

	(void)pthread_mutex_unlock(&dataMutex);

	return 0;
}

/*
 *  pickPidInfoW(): process PID fTrace wake-up / waking
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

	if(pickPidCommon(addr, fthread, ts)) // malformed common?
		return -1;

	// use local copy and add addr's address with its offset
	struct tr_wakeup frame = tr_wakeup;
	// NOTE:  we inherit const void from addr
	for (const void ** ptr = (void*)&frame; ptr < (const void **)(&frame + 1); ptr++)
		// add addr
		*ptr = addr + *(int32_t*)ptr;

	if (*frame.comm & 0x80) // malformed buffer? valid char?
		return -1;

	printStat("    comm=%s pid=%d prio=%d target_cpu=%03d\n",
				frame.comm, *frame.pid, *frame.prio, *frame.target_cpu);

	// lock data to avoid inconsistency
	(void)pthread_mutex_lock(&dataMutex);

	for (node_t * item = nhead; ((item)); item=item->next )
		// find PID that triggered wake-up
		if (item->pid == *frame.pid){

			if (item->mon.last_tsP){

				double period = (double)(ts - item->mon.last_tsP)/(double)NSEC_PER_SEC;

				if (!(item->mon.pdf_phist)){
					if ((runstats_histInit(&(item->mon.pdf_phist), period)))
						warn("Histogram init failure for PID %d '%s' period", item->pid, (item->psig) ? item->psig : "");
				}

				printDbg(PFX "Period for PID %d '%s' %f\n", item->pid, (item->psig) ? item->psig : "", period);
				if ((runstats_histAdd(item->mon.pdf_phist, period)))
					warn("Histogram increment error for PID %d '%s' period", item->pid, (item->psig) ? item->psig : "");

				if (item->mon.cdf_period){
					item->mon.dl_diff += (int64_t)ts - (int64_t)item->mon.last_tsP - (int64_t)findPeriodMatch((uint64_t)item->mon.cdf_period);
					if (TSCHS < item->mon.dl_diff)						// count only positive overruns based on period match
						item->mon.dl_overrun++;							// count number of times period deviates from ideal CDF
					item->mon.deadline = ts + item->mon.cdf_period;		// estimate deadline based on average period
				}
				else
					item->mon.deadline = 0;								// Reset to avoid for deadline boundary check

			}

			item->mon.last_tsP = ts;		// this period start
			item->mon.dl_count++;			// count number of periods

			break;
		}

	(void)pthread_mutex_unlock(&dataMutex);

	return 0;
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
			kbuf = kbuffer_alloc(KBUFFER_LSIZE_SAME_AS_HOST, KBUFFER_ENDIAN_SAME_AS_HOST);

			char* fn;
			if (NULL != fthread->dbgfile)
				fn = fthread->dbgfile;
			else{
				fn = malloc(CMD_LEN);
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
			if (0 >= (ret = fread (buffer, sizeof(char), PIPE_BUFFER, fp))) {
				if (ret < -1) {
					pstate = 2;
					*retVal = errno;
					err_msg ("File read failed: %s", strerror(errno));
				} // else stay here

				break;
			}

			if ((ret = kbuffer_load_subbuffer(kbuf, buffer)))
				warn ("Unable to parse ring-buffer page!");

#ifdef DEBUG
			if ((ret = kbuffer_missed_events(kbuf)))
				printDbg (PFX "Missed %d events on CPU%d!\n", ret, fthread->cpuno );
#endif

			// read first element
			pEvent = kbuffer_read_event(kbuf, &timestamp);

			while ((pEvent) && (!ftrace_stop)) {
				int (*eventcall)(const void *, const struct ftrace_thread *, uint64_t) = pickPidCommon; // default to common for unknown formats

				for (struct ftrace_elist * event = elist_head; ((event)); event=event->next)
					// check for ID, first value is 16 bit ID
					if (event->eventid == *(uint16_t*)pEvent){
						eventcall = event->eventcall;
						break;
					}

				ret = eventcall(pEvent, fthread, timestamp);
				if (0 > ret){
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
 *  Return value: -1 error, -2 = more to move, 0 = success, 1 = no change
 */
static int
updateSiblings(node_t * node){

	node_t * mainp = node;

	if ((node->status & MSK_STATSIBL)
			&& (node->param)
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

	if (mainp != node)  // we are not the main task
		return 1;		// assumed period of sibling changed, let's ignore it

	// ELSE update all TIDs
	return pidReallocAndTest(checkPeriod_R(node, 0),
			getTracer(node->mon.assigned), node);
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
	int rd = fread (szStatBuff, sizeof(char), PIPE_BUFFER-1, fp);
	if (0 >= rd) {
		fclose (fp);
		return -1;
	}
	szStatBuff[rd] = '\0'; // safety first

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
				diff = (int64_t)(num - item->mon.rt);
				item->mon.rt = (uint64_t)num; 	// store last seen runtime
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
				if (num != item->mon.rt)
					item->mon.dl_count++;
			}
			if (strncasecmp(ltag, "dl.deadline", 4) == 0)	{
				if (!item->mon.deadline)
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
							printDbg(PIN "PID %d '%s' Deadline overrun by %ldns, sum %ld\n",
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
						diff = (int64_t)item->attr.sched_runtime - item->mon.rt;
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
					item->mon.rt = ltrt;
				break; // we're done reading
			}
		}

		// Advance with token
		s = strtok_r (NULL, "\n", &s_ptr);	
	}

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

					if (runstats_histFit(&item->mon.pdf_phist))
						info("Happened for period in PID %d '%s'", item->pid, (item->psig) ? item->psig: "");

					// period changed enough for a different time-slot?
					if ( (findPeriodMatch(item->mon.cdf_period) != findPeriodMatch(newPeriod))
							&& (newPeriod > item->mon.cdf_runtime)
							&& (item->mon.cdf_period * MINCHNGL > newPeriod * 100
								|| item->mon.cdf_period * MINCHNGH < newPeriod * 100)){

						// meaningful change?

						info("Update PID %d '%s' period: %luus", item->pid, (item->psig) ? item->psig : "", newPeriod/1000);
						item->mon.resample++;

						item->mon.cdf_period = newPeriod;
						// check if there is a better fit for the period, and if it is main
						if (-1 == updateSiblings(item))
							warn("PID %d '%s' Sibling update not possible!", item->pid, (item->psig) ? item->psig : "");
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
								warn("CDF initialization/range error for PID %d '%s'", item->pid, (item->psig) ? item->psig : "");
							item->status |= MSK_STATHERR;
						}

					break;
				}

				if (0 < newWCET){
					if (SCHED_DEADLINE == item->attr.sched_policy){
						updatePidWCET(item, newWCET);
					}
					if ( item->mon.cdf_runtime * MINCHNGL > newWCET * 100
							|| item->mon.cdf_runtime * MINCHNGH < newWCET * 100){
						// meaningful change?
						info("Update PID %d '%s' runtime: %luus", item->pid, (item->psig) ? item->psig : "", newWCET/1000);
						item->mon.resample++;
					}
					item->mon.cdf_runtime = newWCET;
					item->status &= ~MSK_STATHERR;
				}
				else
					warn ("Estimation error, can not update WCET");

				if (runstats_histFit(&item->mon.pdf_hist))
					info("Happened for runtime in PID %d '%s'", item->pid, (item->psig) ? item->psig: "");
			}
		}
    }

	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		if (0.0 != trc->U) // ignore 0 min CPU
			trc->Umin = MIN (trc->Umin, trc->U);
		trc->Umax = MAX (trc->Umax, trc->U);

		if (0.0 == trc->Uavg)
			trc->Uavg = trc->U;
		else
			trc->Uavg = trc->Uavg * alphaAVG + trc->U * (1.0 - alphaAVG);
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
					"PID   - Rsh - Smpl - Cycle Overruns(total/found/fail) - avg rt (min/max) - sum diff (min/max/avg)\n"
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
			(void)printf("%7d%c: %3ld-%5ld-%3ld(%ld/%ld/%ld) - %ld(%ld/%ld) - %ld(%ld/%ld/%ld) - %s - %s\n",
				abs(item->pid), item->pid<0 ? '*' : ' ',
				item->mon.resched, item->mon.resample,  item->mon.dl_overrun, item->mon.dl_count+item->mon.dl_scanfail,
				item->mon.dl_count, item->mon.dl_scanfail,
				item->mon.rt_avg, item->mon.rt_min, item->mon.rt_max,
				item->mon.dl_diff, item->mon.dl_diffmin, item->mon.dl_diffmax, item->mon.dl_diffavg,
				policy_to_string(item->attr.sched_policy),
				(item->psig) ? item->psig : "");
			break;

		case SCHED_DEADLINE:
			(void)printf("%7d%c: %3ld-%5ld-%3ld(%ld/%ld/%ld) - %ld(%ld/%ld) - %ld(%ld/%ld/%ld) - %s\n",
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

	if (SM_PADAPTIVE <= prgset->sched_mode) {
		(void)printf( "\nStatistics on resource usage:\n"
						"CPU : AVG - 5min (MIN/MAX)\n"
						"----------------------------------------------------------------------------------\n");

		for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
			(void)recomputeTimes(trc);
			(void)printf( "CPU %d: %3.2f%% (%3.2f%%/%3.2f%%)\n", getTracerMainCPU(trc),
					trc->Uavg * 100, MIN(trc->Umin, trc->Umax) * 100, trc->Umax * 100 );
		}
	}

#ifdef DEBUG
	(void)checkContParam(contparm);
#endif
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

	// calculate alpha for 5 min (default) based on interval
	alphaAVG = 1.0 - (float)prgset->interval / (float)USEC_PER_SEC / ALPHAAVG_SECONDS;

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

