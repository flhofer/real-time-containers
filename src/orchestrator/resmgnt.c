/*
 * resmgmt.c
 *
 *  Created on: May 3, 2020
 *      Author: Florian Hofer
 */
#include "resmgnt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h> 		// used for string parsing
#include <sched.h>			// scheduler functions
#include <linux/sched.h>	// Linux specific scheduling

// Custom includes
#include "orchestrator.h"

#include "orchdata.h"	// memory structure to store information
#include "kernutil.h"	// generic kernel utilities
#include "error.h"		// error and stderr print functions
#include "cmnutil.h"	// common definitions and functions

// Should be needed only here
#include <sys/resource.h>
#include <dirent.h>

#define RTLIM_UNL	"-1"		// out of 10000000 = 95%
#define RTLIM_DEF	"950000"	// out of 10000000 = 95%
#define RTLIM_PERC	95			// percentage limitation for calculus

#define CHKNUISBETTER 1	// new CPU if available better than perfect match?
#define MAX_UL 0.90
#define SCHED_UKNLOAD	10 		// 10% load extra per task
#define SCHED_RRTONATTR	1000000 // conversion factor from sched_rr_timeslice_ms to sched_attr

/*
 * --------------------- FROM HERE WE ASSUME RW LOCK ON NHEAD ------------------------
 */

/*
 * setPidRlimit(): sets given resource limits
 *
 * Arguments: - PID node
 *			   - soft resource limit
 *			   - hard resource limit
 *			   - resource limit tag
 *			   - resource limit string-name for prints
 * Return value: ---
 */
static inline void
setPidRlimit(pid_t pid, int32_t rls, int32_t rlh, int32_t type, char* name ) {

	struct rlimit rlim;
	if (-1 != rls || -1 != rlh) {
		if (prlimit(pid, type, NULL, &rlim))
			err_msg_n(errno, "getting %s for PID %d", name,
				pid);
		else {
			if (-1 != rls)
				rlim.rlim_cur = rls;
			if (-1 != rlh)
				rlim.rlim_max = rlh;
			if (prlimit(pid, type, &rlim, NULL ))
				err_msg_n(errno,"setting %s for PID %d", name,
					pid);
			else
				cont("PID %d %s set to %d-%d", pid, name,
					rlim.rlim_cur, rlim.rlim_max);
		}
	}
}

/*
 *	setPidAffinity: sets the affinity of a PID
 *				the task is present in the common 'docker' CGroup
 *
 *	Arguments: - pointer to node with data
 *			   - bit-mask for affinity
 *
 *	Return value: 0 on success, -1 otherwise
 */
static int
setPidAffinity (node_t * node){

	int ret = 0;

	{	// add PID to docker CGroup

		char pid[6]; // PID is 5 digits + \0
		(void)sprintf(pid, "%d", node->pid);

		if (0 > setkernvar(prgset->cpusetdfileprefix , "tasks", pid, prgset->dryrun)){
			printDbg( "Warn! Can not move task %s\n", pid);
			ret = -1;
		}

	}

	if (!(node->param) || !(node->param->rscs->affinity_mask)){
		err_msg("No valid parameters or bit-mask allocation!");
		return -1;
	}

	struct bitmask * bmold = numa_allocate_cpumask();
	if (!bmold){
		err_msg("Could not allocate bit-mask for compare!");
		return -1;
	}

	// get affinity WARN wrongly specified in man(7), returns error or number of bytes read
	if ((0 > numa_sched_getaffinity(node->pid, bmold)))
		err_msg_n(errno,"getting affinity for PID %d", node->pid);

	if (numa_bitmask_equal(node->param->rscs->affinity_mask, bmold)){

		// get textual representation for log
		char affinity[CPUSTRLEN];
		if (parse_bitmask (node->param->rscs->affinity_mask, affinity, CPUSTRLEN)){
				warn("Can not determine inverse affinity mask!");
				(void)sprintf(affinity, "****");
		}

		// Set affinity
		if (numa_sched_setaffinity(node->pid, node->param->rscs->affinity_mask)){
			err_msg_n(errno,"setting affinity for PID %d",
				node->pid);
			ret = -1;
		}
		else
			cont("PID %d reassigned to CPUs '%s'", node->pid, affinity);
	}

	numa_bitmask_free(bmold);
	return ret;
}

/*
 *	setContainerAffinity: sets the affinity of a container
 *
 *	Arguments: - pointer to node with data
 *			   - bit-mask for affinity
 *
 *	Return value: 0 on success, -1 otherwise
 */
static int
setContainerAffinity(node_t * node){

	if (!(node->param) || !(node->param->rscs->affinity_mask)){
		err_msg("No valid parameters or bit-mask allocation!");
		return -1;
	}

	char *contp = NULL;
	char affinity[CPUSTRLEN];
	char affinity_old[CPUSTRLEN];
	int ret = 0;

	if (parse_bitmask (node->param->rscs->affinity_mask, affinity, CPUSTRLEN)){
			err_msg("Can not determine inverse affinity mask!");
			return -1;
	}

	if ((contp=malloc(strlen(prgset->cpusetdfileprefix)	+ strlen(node->contid)+1))) {
		// copy to new prefix
		contp = strcat(strcpy(contp,prgset->cpusetdfileprefix), node->contid);

		// read old, then compare -> update if different
		if (0 > getkernvar(contp, "/cpuset.cpus", affinity_old, CPUSTRLEN))
			warn("Can not read %.12s's CGroups CPU's", node->contid);

		if (strcmp(affinity, affinity_old)){
			cont( "reassigning %.12s's CGroups CPU's to %s", node->contid, affinity);
			if (0 > setkernvar(contp, "/cpuset.cpus", affinity, prgset->dryrun)){
				warn("Can not set CPU-affinity");
				ret = -1;
			}
		}
	}
	else{
		warn("malloc failed!");
		ret = -1;
	}

	free (contp);

	return ret;
}

/*
 * setPidResources_u(): set PID resources at first detection (after check)
 *
 * Arguments: - pointer to PID item (node_t)
 *
 * Return value: --
 */
static void
setPidResources_u(node_t * node) {

	if (!node->psig)
		node->psig = node->param->psig;

	if (!node->contid && node->param->cont){
		node->contid = node->param->cont->contid;
		warn("Container search resulted in empty container ID!");
	}

	// each PID should have it's OWN container -> Concept

	// update CGroup setting of container if in CGROUP mode
	// save if not successful, only CG mode contains ID's
	if (DM_CGRP == prgset->use_cgroup) {
		node->status |= !(setContainerAffinity(node)) & MSK_STATUPD;
	}
	else{
		if ((SCHED_DEADLINE == node->attr.sched_policy)
				&& (SM_DYNSYSTEM == prgset->sched_mode)){
			warn ("Can not set DL task to PID affinity when using G-EDF!");
			node->status |= MSK_STATUPD;
		}
		else
			node->status |= !(setPidAffinity(node)) & MSK_STATUPD;
	}

	if (0 == node->pid) // PID 0 = detected containers
		return;

	// only do if different than -1, <- not set values = keep default
	if (SCHED_NODATA != node->param->attr->sched_policy) {
		cont("Setting Scheduler of PID %d to '%s'", node->pid,
			policy_to_string(node->param->attr->sched_policy));

		// set the flag right away, if set..
		if ((prgset->setdflag)
			&& (SCHED_DEADLINE == node->param->attr->sched_policy)
			&& (KV_413 <= prgset->kernelversion)) {

			cont("Set dl_overrun flag for PID %d", node->pid);
			node->param->attr->sched_flags |= SCHED_FLAG_DL_OVERRUN | SCHED_FLAG_RECLAIM;
		}

		if (sched_setattr (node->pid, node->param->attr, 0U))
			err_msg_n(errno, "setting attributes for PID %d",
				node->pid);
	}
	else
		cont("Skipping setting of scheduler for PID %d", node->pid);

	// controlling resource limits
	setPidRlimit(node->pid, node->param->rscs->rt_timew,  node->param->rscs->rt_time,
		RLIMIT_RTTIME, "RT-Limit" );

	setPidRlimit(node->pid, node->param->rscs->mem_dataw,  node->param->rscs->mem_data,
		RLIMIT_DATA, "Data-Limit" );
}

/*
 * setPidResources(): set PID resources at first detection
 *
 * Arguments: - pointer to PID item (node_t)
 *
 * Return value: --
 */
void
setPidResources(node_t * node) {

	// parameters unassigned
	if (!prgset->quiet)
		(void)printf("\n");
	if (node->pid)
		info("new PID in list %d", node->pid);
	else if (node->contid)
		info("new container in list '%s'", node->contid);
	else
		warn("SetPidResources: Container not specified");

	if (!node_findParams(node, contparm))  // parameter set found in list -> assign and update
		setPidResources_u(node);
	else
		node->status |= MSK_STATUPD | MSK_STATNMTCH;
}

/*
 * updatePidAttr : update PID scheduling attributes and check for flags (update)
 *
 * Arguments: - node_t item
 *
 * Return value: -
 */
void
updatePidAttr(node_t * node){

	// parameters still to upload?
	if ((node->param) && !(node->status & MSK_STATUPD)){
		setPidResources_u(node);
		return;
	}

	// storage for actual attributes
	struct sched_attr attr_act;

	// try reading
	if (sched_getattr (node->pid, &attr_act, sizeof(struct sched_attr), 0U) != 0){
		warn("Unable to read parameters for PID %d: %s", node->pid, strerror(errno));
		return;
	}

	if (memcmp(&(node->attr), &attr_act, sizeof(struct sched_attr))) {
		// inform if attributes changed
		if (SCHED_NODATA != node->attr.sched_policy)
			info("Scheduling attributes changed for pid %d", node->pid);
		node->attr = attr_act;
	}

	// With Throttle active, doesn't work
	// set the flag for GRUB reclaim operation if not enabled yet
	if ((prgset->setdflag)															// set flag parameter enabled
		&& (SCHED_DEADLINE == node->attr.sched_policy)								// deadline scheduling task
		&& (KV_413 <= prgset->kernelversion)										// kernel version sufficient
		&& !(SCHED_FLAG_RECLAIM == (node->attr.sched_flags & SCHED_FLAG_RECLAIM))){	// flag not set yet

		cont("Set dl_overrun flag for PID %d", node->pid);
		node->attr.sched_flags |= SCHED_FLAG_RECLAIM;

		if (sched_setattr (node->pid, &(node->attr), 0U))
			err_msg_n(errno, "Can not set overrun flag");
	}

}


/*
 * updatePidWCET : update PID WCET parameter to computed value
 *
 * Arguments: - node_t item
 * 			  - wcet to set
 *
 * Return value: -
 */
void
updatePidWCET(node_t * node, uint64_t wcet){

	node->attr.sched_runtime = wcet;

	if (sched_setattr (node->pid, &(node->attr), 0U))
		err_msg_n(errno, "Can not set new WCET");
	else
		node->status |= MSK_STATWCUD;
}

/*
 * updatePidCmdline : update PID command line
 *
 * Arguments: - node_t item
 *
 * Return value: -
 */
void
updatePidCmdline(node_t * node){
	char * cmdline;
	char kparam[15]; // pid{x}/cmdline read string

	if ((cmdline = malloc(MAXCMDLINE))) { // alloc memory for strings

		(void)sprintf(kparam, "%d/cmdline", node->pid);
		if (0 > getkernvar("/proc/", kparam, cmdline, MAXCMDLINE))
			// try to read cmdline of pid
			warn("can not read pid %d's command line: %s", node->pid, strerror(errno));

		// cut to exact (reduction = no issue)
		cmdline=realloc(cmdline,
			strlen(cmdline)+1);

		// check for change.. NULL or different
		if (!(node->psig) || (strcmp(cmdline, node->psig))){
			// PIDs differ, update
			if (node->psig)
				free (node->psig);
			// assign
			node->psig = cmdline;
		}
		else
			free(cmdline);
	}
	else // FATAL, exit and execute atExit
		err_msg("Could not allocate memory!");
}

/*
 * --------------------- UNTIL HERE WE ASSUME RW LOCK ON NHEAD ------------------------
 */


/*
 * resetContCGroups : reset existing containers CGroups settings to default
 *
 * Arguments: - configuration parameter structure
 * 			  - cpu online string
 * 			  - numa nodes string
 *
 * Return value: -
 */
void
resetContCGroups(prgset_t *set, char * constr, char * numastr) {

	DIR *d;
	struct dirent *dir;
	d = opendir(set->cpusetdfileprefix);// -> pointing to global
	if (d) {

		// CLEAR exclusive flags in all existing containers
		{
			char *contp = NULL; // clear pointer
			while ((dir = readdir(d)) != NULL) {
			// scan trough docker CGroup, find container IDs
				if (64 == (strspn(dir->d_name, "abcdef1234567890"))) {
					if ((contp=realloc(contp,strlen(set->cpusetdfileprefix)  // container strings are very long!
						+ strlen(dir->d_name)+1))) {
						// copy to new prefix
						contp = strcat(strcpy(contp,set->cpusetdfileprefix),dir->d_name);

						// remove exclusive!
						if (0 > setkernvar(contp, "/cpuset.cpu_exclusive", "0", set->dryrun)){
							warn("Can not remove CPU exclusive : %s", strerror(errno));
						}
					}
					else // realloc error
						err_exit("could not allocate memory!");
				}
			}
			free (contp);
		}

		// clear Docker CGroup settings and affinity first..
		if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpu_exclusive", "0", set->dryrun)){
			warn("Can not remove CPU exclusive : %s", strerror(errno));
		}
		if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", constr, set->dryrun)){
			// global reset failed, try affinity only
			if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", set->affinity, set->dryrun)){
				warn("Can not reset CPU-affinity. Expect malfunction!"); // set online cpus as default
			}
		}
		if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.mems", numastr, set->dryrun)){
			warn("Can not set NUMA memory nodes");
		}

		// rewind, start configuring
		rewinddir(d);


		{
			char *contp = NULL; // clear pointer
			/// Reassigning pre-existing containers?
			while ((dir = readdir(d)) != NULL) {
			// scan trough docker CGroup, find them?
				if  ((DT_DIR == dir->d_type)
					 && (64 == (strspn(dir->d_name, "abcdef1234567890")))) {
					if ((contp=realloc(contp,strlen(set->cpusetdfileprefix)  // container strings are very long!
						+ strlen(dir->d_name)+1))) {
						// copy to new prefix
						contp = strcat(strcpy(contp,set->cpusetdfileprefix),dir->d_name);

						if (0 > setkernvar(contp, "/cpuset.cpus", set->affinity, set->dryrun)){
							warn("Can not set CPU-affinity");
						}
						if (0 > setkernvar(contp, "/cpuset.mems", numastr, set->dryrun)){
							warn("Can not set NUMA memory nodes");
						}
					}
					else // realloc error
						err_exit("could not allocate memory!");
				}
			}
			free (contp);
		}

		// Docker CGroup settings and affinity
		if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", set->affinity, set->dryrun)){
			warn("Can not set CPU-affinity");
		}
		if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.mems", numastr, set->dryrun)){
			warn("Can not set NUMA memory nodes");
		}
		if (AFFINITY_USEALL != set->setaffinity) // set exclusive only if not use-all
			if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpu_exclusive", "1", set->dryrun)){
				warn("Can not set CPU exclusive");
			}

		closedir(d);
	}
}

/*
 * resetRTthrottle : reset RT throttle system-wide
 *
 * Arguments: - configuration parameter structure
 * 			  - percent amount value for throttle, -1 for unlimited
 *
 * Return value: returns 0 on success, -1 on failure
 */
int
resetRTthrottle (prgset_t *set, int percent){
	char * value;	// pointer to value to write
	char buf[20];	// temporary stack buffer

	// all modes except  Dynamic, set to -1 = unconstrained
	if (-1 == percent){
		cont( "Set real-time bandwidth limit to (unconstrained)..");
		value = RTLIM_UNL;
	}

	// in Dynamic System Schedule, limit to 95% of period (a limit is requirement of G-EDF)
	else{
		// Limit value range
		percent = MIN(MAX(percent, 10), 100);

		cont( "Set real-time bandwidth limit to %d%%..", RTLIM_PERC);
		// on error use default
		value = RTLIM_DEF;

		// read period
		if (0 > getkernvar(set->procfileprefix, "sched_rt_period_us", buf, sizeof(buf)))
			warn("Could not read throttle limit. Use default" RTLIM_DEF ".");
		else{
			// compute 95% of period
			long period = atol(buf);
			if (period){
				(void)sprintf(buf, "%ld", period*percent/100);
				value = buf;
			}
			else
				warn("Could not parse throttle limit. Use default" RTLIM_DEF ".");
		}
	}

	// set bandwidth and throttle control to value/unconstrained
	if (0 > setkernvar(set->procfileprefix, "sched_rt_runtime_us", value, set->dryrun)){
		warn("Could not write RT-throttle value. Limitations apply.");
	}
	else{
		set->status |= MSK_STATTRTL;
		// maybe more detailed?
		return 0;
	}

	return -1;
}

/* ------------------- RES-TRACER resource management from here ---------------------------- */

/*
 *  createResTracer(): create resource tracing memory elements
 *  				   set them to default value
 *
 *  Arguments:
 *
 *  Return value:
 */
void
createResTracer(){
	if (rHead)	// does it exist?
		fatal("Memory management inconsistency: resTracer already present!");

	// backwards, cpu0 on top, we assume affinity_mask ok
	for (int i=(prgset->affinity_mask->size); i >= 0;i--)

		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by selected only
			push((void**)&rHead, sizeof(struct resTracer));
			rHead->affinity = i;
			rHead->U = 0.0;
			rHead->basePeriod = 0;
	}
}

/*
 *  checkUvalue(): verify if task fits into Utilization limits of a resource
 *
 *  Arguments:  - resource entry for this CPU
 * 				- the `attr` structure of the task we are trying to fit in
 * 				- add or test, 0 = test, 1 = add to resources
 *
 *  Return value: a matching score, higher is better. Negative values return error
 * 				  -1 = no space; -2 error
 */
int
checkUvalue(struct resTracer * res, struct sched_attr * par, int add) {
	uint64_t base = res->basePeriod;
	uint64_t used = res->usedPeriod;
	uint64_t basec = par->sched_period;
	int rv = 3; // perfect match -> all cases max value default
	int rvbonus = 1;

	switch (par->sched_policy) {

	// if set to "default", SCHED_OTHER or SCHED_BATCH, how do I react?
	// sched_runtime == 0, no value, reset to 1 second
	default:
		if (0 == base || 0 == par->sched_runtime){
			base = 1000000000; // default to 1 second)
			break;
		}

	/*
	 * DEADLINE return values
	 * 3 = OK, perfect period match;
	 * 2 = OK, but empty CPU
	 * 1 = recalculate base but new period is an exact multiplier;
	 * 0 = OK, but recalculated base;
	 */
		// no break

	case SCHED_DEADLINE:
		// if unused, set to this period
		if (0==base){
			base = par->sched_period;
			rv = 1 + CHKNUISBETTER;
		}

		if (base != par->sched_period) {
			// recompute base
			uint64_t new_base = gcd(base, par->sched_period);

			if (new_base % 1000 != 0){
				warn("Check -> Nanosecond resolution periods not supported!");
				return -2;
			}
			// recompute new values of resource tracer
			used *= new_base/res->basePeriod;
			base = new_base;

			// are the periods a perfect fit?
			if ((new_base == res->basePeriod)
				|| (new_base == par->sched_period))
					rv = 2-CHKNUISBETTER;
			else
				rv =0;

		}
		used += par->sched_runtime * base/par->sched_period;
		break;

	/*
	 *	FIFO return values for different situations
	 *	3 .. perfect match desired repetition matches period of resources
	 *	2 .. empty CPU, = perfect fit on new CPU
	 *  1 .. recalculation of period, but new is perfect fit to both
	 *  0 .. recompute needed
	 *  all with +1 bonus if runtime fits remaining UL
	 */
	case SCHED_FIFO:

		if (0 == par->sched_runtime){
			// can't do anything about computation
			rv = 0;
			used += used*SCHED_UKNLOAD/100; // add 10% to load, as a dummy value
			break;
		}

		// if unused, set to this period
		if (0 == basec){
			rvbonus = 0; // record that we have a fake base
			basec = 1000000000; // default to 1 second)
		}

		if (0==base){
			base = basec;
			rvbonus  = 1; // reset again, we have match
			rv = 1 + CHKNUISBETTER;
		}

		if (rvbonus){
			// free slice smaller than runtime, = additional preemption => lower pts
			if ((MAX_UL-res->U) * (double)base < par->sched_runtime)
				rvbonus=0;
		}

		if (base != basec) {
			// recompute base
			uint64_t new_base = gcd(base, basec);

			if (new_base % 1000 != 0){
				warn("Check -> Nanosecond resolution periods not supported!");
				return -2;
			}

			// are the periods a perfect fit?
			if ((new_base == base)
				|| (new_base == basec))
					rv = 2-CHKNUISBETTER;
			else
				rv = 0;

			// recompute new values of resource tracer
			used *= new_base/base;
			base = new_base;
		}

		// apply bonus
		rv+= rvbonus;

		used += par->sched_runtime * base/basec;
		break;

	/*
	 *	RR return values for different situations
	 *	 3 .. perfect match desired repetition matches period of resources
	 *	 2 .. empty CPU, = perfect fit on new CPU
	 *   1 .. recalculation of period, but new is perfect fit to both
	 *   0 .. recompute needed
	 *  all with +1 bonus if runtime fits remaining UL
	 */
	case SCHED_RR:

		if (0 == par->sched_runtime){
			// can't do anything about computation
			rv = 0;
			used += used*SCHED_UKNLOAD/100; // add 10% to load, as a dummy value
			break;
		}

		// if unused, set to this period
		if (0 == basec){
			basec = 1000000000; // default to 1 second)
		}
		else
			// if the runtime doesn't fit into the preemption slice..
			// reset base to slice as it will be preempted during run increasing task switching
			if (prgset->rrtime*SCHED_RRTONATTR <= par->sched_runtime)
				basec = prgset->rrtime*SCHED_RRTONATTR;

		if (0==base){
			// if unused, set to rr slice. top fit
			base = basec;
			rv = 1 + CHKNUISBETTER;
		}

		if (base != basec)
		{

			// recompute base
			uint64_t new_base = gcd(base, basec);

			// are the periods a perfect fit?
			if ((new_base == basec)
				// .. or equals the original period (could be 0 but would not be a match)
				|| (new_base == par->sched_period))
				rv = 2-CHKNUISBETTER;
			// TODO: new_base == base and base > rrtime vs base < rrtime
			else{
				// GCD doesn't match slice and is bigger than preemption slice ->
				if ((new_base > prgset->rrtime*SCHED_RRTONATTR)
					// or  remaining utilization is enough to keep the thing running w/o preemtion
					|| ((MAX_UL-res->U) * (double)new_base > par->sched_runtime))
						rv = 1; // still fitting into it
				else
					rv=0; // can not guarantee that it fits!
			}

			used *= new_base/base;
			base = new_base;
		}

		if (0 == par->sched_runtime){
			// can't do anything more about usage computation
			break;
		}

		used += par->sched_runtime * base/basec;

		break;

	}

	// calculate and verify utilization rate
	float U = (double)used/(double)base;
	if (MAX_UL < U)
		rv = -1;

	if (add){ // apply changes to res structure?
		res->usedPeriod = used;
		res->basePeriod = base;
		res->U=U;
	}

	return rv;
}

/*
 *  checkPeriod(): find a resource that fits period
 *
 *  Arguments: - the attr structure of the task
 *  		   - the set affinity
 *
 *  Return value: a pointer to the resource tracer
 * 					returns null if nothing is found
 */
resTracer_t *
checkPeriod(struct sched_attr * attr, int affinity) {
	resTracer_t * ftrc = NULL;
	int last = -2;		// last checked tracer's score, error by default
	float Ulast = 10;	// last checked traces's utilization rate
	int res;

	// loop through all and return the best fit
	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		res = checkUvalue(trc, attr, 0);
		if ((res > last) // better match, or matching favorite
			|| ((res == last) &&
				(  (trc->affinity == abs(affinity))
				|| (trc->U < Ulast)) ) )	{
			last = res;
			// reset U if we had an affinity match
			if (trc->affinity == abs(affinity))
				Ulast= 0.0;
			else
				Ulast = trc->U;
			ftrc = trc;
		}
	}
	return ftrc;
}

/*
 *  checkPeriod_R(): find a resource that fits period
 *
 *  Arguments: - the item to check
 *
 *  Return value: a pointer to the resource tracer
 * 					returns null if nothing is found
 */
resTracer_t *
checkPeriod_R(node_t * item) {
	resTracer_t * ftrc = NULL;
	struct sched_attr attr = { 48 };

	int last = -2;		// last checked tracer's score, error by default
	float Ulast = 10;	// last checked traces's utilization rate
	int res;

	// loop through all and return the best fit
	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		if (SCHED_DEADLINE == item->attr.sched_policy)
			res = checkUvalue(trc, &item->attr, 0);
		else{
			attr.sched_policy = item->attr.sched_policy;
			attr.sched_runtime = item->mon.cdf_runtime;
			attr.sched_period = item->mon.cdf_period;
			res = checkUvalue(trc, &attr, 0);
		}
		if ((res > last) // better match, or matching favorite
			|| ((res == last) &&
				(  (trc->affinity == abs(item->param->rscs->affinity))
				|| (trc->U < Ulast)) ) )	{
			last = res;
			// reset U if we had an affinity match
			if (trc->affinity == abs(item->param->rscs->affinity))
				Ulast= 0.0;
			else
				Ulast = trc->U;
			ftrc = trc;
		}
	}

	return ftrc;

	return ftrc;
}

/*
 *  getTracer(): get the resource tracer for CPU x
 *
 *  Arguments: - CPU number to look for
 *
 *  Return value: a pointer to the resource tracer
 *					returns null if nothing is found
 */
resTracer_t *
getTracer(int32_t CPUno) {
	// loop through all and return the best fit
	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		if (CPUno == trc->affinity)
			return trc;
	}

	return NULL;
}

/*
 *  grepTracer(): find an empty or low UL CPU
 *
 *  Arguments: -
 *
 *  Return value: a pointer to the resource tracer
 *					returns null if nothing is found
 */
resTracer_t *
grepTracer() {
	resTracer_t * ftrc = NULL;
	float Umax = -2;

	// loop through all and return the best fit
	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		if (trc->U > Umax) {
			Umax = trc->U;
			ftrc = trc;
		}
	}
	return ftrc;
}

/*
 *  recomputeTimes(): recomputes base and utilization factor of a resource
 *
 *  Arguments:  - resource entry for this CPU
 *
 *  Return value: Negative values return error
 */
static int
recomputeTimes(struct resTracer * res, int32_t CPUno) {

	struct resTracer * resNew = calloc (1, sizeof(struct resTracer));
	struct sched_attr attr = { 48 };
	int rv;

	// find PID switching from
	for (node_t * item = nhead; ((item)); item=item->next){
		if (item->mon.assigned != CPUno)
			continue;

		if (SCHED_DEADLINE == item->attr.sched_policy)
			rv = checkUvalue(resNew, &item->attr, 1);
		else{
			attr.sched_policy = item->attr.sched_policy;
			attr.sched_runtime = item->mon.cdf_runtime;
			attr.sched_period = item->mon.cdf_period;
			rv = checkUvalue(resNew, &attr, 1);
		}

		if ( 0 > rv ){
			free(resNew);
			return rv; // stops here
		}

	}

	res->basePeriod = resNew->basePeriod;
	res->usedPeriod = resNew->usedPeriod;
	res->U = resNew->U;

	free(resNew);
	return 0;
}

/*
 *  recomputeCPUTimes(): recomputes base and utilization factor of a CPU
 *
 *  Arguments:  - CPU number
 *
 *  Return value: Negative values return error
 */
int
recomputeCPUTimes(int32_t CPUno) {
	if (-1 == CPUno)	// default, not assigned
		return 0;

	resTracer_t * trc;

	if ((trc = getTracer(CPUno)))
		return recomputeTimes(trc, CPUno);

	return -2; // not found! ERROR
}

/*
 *	setPidAffinity_R: sets the affinity of a PID based on assinged CPU
 *				the task is present in the common 'docker' CGroup
 *
 *	Arguments: - pointer to node with data
 *
 *	Return value: 0 on success, -1 otherwise
 */
int
setPidAffinity_R (node_t * node){
	if (node->mon.assigned_mask)
		numa_bitmask_clearall(node->mon.assigned_mask);
	else
		node->mon.assigned_mask = numa_allocate_cpumask();

	numa_bitmask_setbit(node->mon.assigned_mask, node->mon.assigned);

	// Set affinity
	if (numa_sched_setaffinity(node->pid, node->mon.assigned_mask)){
		err_msg_n(errno,"setting affinity for PID %d",
			node->pid);
		return -1;
	}
	return 0;
}
