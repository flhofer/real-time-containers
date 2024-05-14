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
#include <math.h>			// EXP and other math functions
#include <sys/resource.h>	// resource limit constants
#include <dirent.h>			// DIR function to read directory stats

// Custom includes
#include "orchestrator.h"

#include "orchdata.h"	// memory structure to store information
#include "kernutil.h"	// generic kernel utilities
#include "error.h"		// error and stderr print functions
#include "cmnutil.h"	// common definitions and functions
#include "rt-sched.h"	// scheduling attribute struct

#undef PFX
#define PFX "[resmgnt] "

#define RTLIM_UNL	"-1"		// out of 10000000 = 95%
#define RTLIM_DEF	"950000"	// out of 10000000 = 95%
#define RTLIM_PERC	95			// percentage limitation for calculus

#define MAX_UL 1						// 90% fixed Ul for all CPUs!
#define SCHED_UKNLOAD	10 				// 10% load extra per task if runtime and period are unknown
#define SCHED_RRTONATTR	1000000 		// conversion factor from sched_rr_timeslice_ms to sched_attr, NSEC_PER_MS
#define SCHED_PDEFAULT	NSEC_PER_SEC	// default starting period if none is specified
#define SCHED_UHARMONIC	3				// offset for non-harmonic scores in checkUvalue (MIN)

static int recomputeCPUTimes_u(int32_t CPUno, node_t * skip);

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
 *	setPidAffinity: sets the affinity of a PID at startup (NO CGRP)
 *
 *	Arguments: - PID of taks
 *			   - bit-mask for affinity
 *
 *	Return value: 0 on success, -1 otherwise
 */
static int
setPidAffinity(pid_t pid, struct bitmask * mask) {
	int ret = 0;

	struct bitmask * bmold = numa_allocate_cpumask();
	if (!bmold){
		err_msg("Could not allocate bit-mask for compare!");
		return -1;
	}

	// get affinity WARN wrongly specified in man(7), returns error or number of bytes read
	if ((0 > numa_sched_getaffinity(pid, bmold)))
		err_msg_n(errno,"getting affinity for PID %d", pid);

	if (numa_bitmask_equal(mask, bmold)){

		// get textual representation for log
		char affinity[CPUSTRLEN];
		if (parse_bitmask (mask, affinity, CPUSTRLEN)){
				warn("Can not determine inverse affinity mask!");
				(void)sprintf(affinity, "****");
		}

		// Set affinity
		if (numa_sched_setaffinity(pid, mask)){
			err_msg_n(errno,"setting affinity for PID %d", pid);
			ret = -1;
		}
		else
			cont("PID %d reassigned to CPUs '%s'", pid, affinity);
	}

	numa_bitmask_free(bmold);
	return ret;
}


/*
 *	setPidAffinityNode: sets the affinity of a PID at startup based on node configuration
 *				the task is present or moved to the in the common 'docker' CGroup (NO CGRP)
 *
 *	Arguments: - pointer to node with data
 *
 *	Return value: 0 on success, -1 otherwise
 */
static int
setPidAffinityNode (node_t * node){

	int ret = 0;

	{	// add PID to docker CGroup
		//TODO: warn ! this removes it if it's already present in a subgroup!
		// -> can not see subgroup contents! -> visible in /proc/150985/cgroup , v2 format 0::/path/from/cgroup/root

		char pid[6]; // PID is 5 digits + \0
		(void)sprintf(pid, "%d", node->pid);
		if (0 > setkernvar(prgset->cpusetdfileprefix , CGRP_PIDS, pid, prgset->dryrun)){
			printDbg(PIN2 "Warn! Can not move task %s\n", pid);
			ret = -1;
		}

	}

	if (!(node->param) || !(node->param->rscs->affinity_mask)){
		err_msg("No valid parameters or bit-mask allocation!");
		return -1;
	}

	// return -1(-2) if one failed
	return ret - setPidAffinity(node->pid,
			node->param->rscs->affinity_mask);
}

/*
 *	setPidAffinityAssinged: sets the affinity of a PID based on assigned CPU
 *				to select the assigned CPU at runtime when multiple affinity is present
 *
 *	Arguments: - pointer to node with data
 *
 *	Return value: 0 on success, -1 otherwise
 */
int
setPidAffinityAssinged (node_t * node){
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
	// reset no affinity bit (if set)
	node->status &= ~MSK_STATNAFF;
	return 0;
}


/*
 *	setContainerAffinity: sets the affinity of a container containing PIDs (CGRP ONLY)
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

	if ((contp=malloc(strlen(prgset->cpusetdfileprefix)	+ strlen(node->contid)
#ifndef CGROUP2
			+1))) {
		// copy to new prefix
		contp = strcat(strcpy(contp,prgset->cpusetdfileprefix), node->contid);
#else
			+strlen(CGRP_DCKP CGRP_DCKS)+1))) { // 'docker-' + '.scope' = '\n'

		// copy to new prefix
		contp = strcat(strcpy(contp,prgset->cpusetdfileprefix), CGRP_DCKP);
		contp = strcat(strcat(contp,node->contid), CGRP_DCKS);
#endif

		// read old, then compare -> update if different
		if (0 > getkernvar(contp, "/cpuset.cpus", affinity_old, CPUSTRLEN)) // TODO: all cpuset.cpus have to be checked -> read from effective
			warn("Can not read %.12s's CGroups CPU's", node->contid);

		if (strcmp(affinity, affinity_old)){
			cont( "reassigning %.12s's CGroups CPU's to %s", node->contid, affinity);
			if (0 > setkernvar(contp, "/cpuset.cpus", affinity, prgset->dryrun)){
				warn("Can not set CPU-affinity : %s", strerror(errno));
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
		// at start, assign node to static/adaptive table affinity match, only if there is a clear candidate
		if (node->pid && 0 <= node->param->rscs->affinity){
			node->mon.assigned = node->param->rscs->affinity;

			if (0 <= node->mon.assigned && setPidAffinityAssinged(node))
				warn("Can not assign startup allocation for PID %d", node->pid);

			// put start values as dist initial values
			if (node->param && node->param->attr){
				if (node->param->attr->sched_period)
					node->mon.cdf_period = node->param->attr->sched_period;
				if (node->param->attr->sched_runtime)
					node->mon.cdf_runtime = node->param->attr->sched_runtime;
			}
		}
	}
	else{
		// NO CGroups
		if ((SCHED_DEADLINE == node->attr.sched_policy)
				&& (SM_PADAPTIVE <= prgset->sched_mode)){
			warn ("Can not set DL task to PID affinity when using G-EDF!");
			node->status |= MSK_STATUPD;
		}
		else
			node->status |= !(setPidAffinityNode(node)) & MSK_STATUPD;
	}

	if (0 == node->pid){ // PID 0 = detected containers
		// TODO: cleanup
		return;
	}

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

	if (!findPidParameters(node, contparm))  // parameter set found in list -> assign and update
		setPidResources_u(node);
	else
		node->status |= MSK_STATUPD | MSK_STATNMTCH;

	// check if we have siblings in container TODO: not all cases are found
	int hasSiblings = node->status & MSK_STATSIBL;
	for (node_t * item = nhead; (item) && !hasSiblings; item=item->next)
		if (item->param && item->param->cont && node->param
			&& item->param->cont == node->param->cont){
			hasSiblings = MSK_STATSIBL;
			item->status |= MSK_STATSIBL;
		}
	node->status |= hasSiblings;
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
	struct sched_attr attr_act = { sizeof(struct sched_attr) };

	// try reading
	if (sched_getattr (node->pid, &attr_act, attr_act.size, 0U) != 0){
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
 *
 * ---------------------- PREPARE - COMMON RESOURCE STUFF -----------------------------
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
		char *contp = NULL; // clear pointer
		while ((dir = readdir(d)) != NULL) {
#ifdef CGROUP2
			char * hex, * t, * t_tok;	// used to extract hex identifier from slice/scope in v2 fmt:'docker-<hex>.scope''
			t = strdup(dir->d_name);
			if (!(strtok_r(t, "-", &t_tok))
					|| !(hex = strtok_r(NULL, ".", &t_tok))) {
				free(t);
				continue;
			}
#else
			char * hex = dir->d_name;
#endif
			// scan trough docker CGroup, find them?
			// TODO: Restart removes pinning? -> maybe reset here only if outside affinity range -- see setContCGroups
			if  ((DT_DIR == dir->d_type)
				 && (64 == (strspn(hex, "abcdef1234567890")))) {
				if ((contp=realloc(contp,strlen(set->cpusetdfileprefix)  // container strings are very long!
					+ strlen(dir->d_name)+1))) {
					// copy to new prefix
					contp = strcat(strcpy(contp,set->cpusetdfileprefix),dir->d_name);

					// remove exclusive!
#ifdef CGROUP2
					if (0 > setkernvar(contp, "/cpuset.cpus.partition", "member", set->dryrun)){
#else
					if (0 > setkernvar(contp, "/cpuset.cpu_exclusive", "0", set->dryrun)){
#endif
						warn("Can not remove CPU exclusive partition: %s", strerror(errno));
					}
				}
				else // realloc error
					err_exit("could not allocate memory!");
			}
#ifdef CGROUP2
			free(t);
#endif
		}
		free (contp);
		closedir(d);
	}
	else
		warn("Can not open Docker CGroup directory: %s", strerror(errno));

	// clear Docker CGroup settings and affinity first..
#ifdef CGROUP2
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus.partition", "member", set->dryrun)){
#else
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpu_exclusive", "0", set->dryrun)){
#endif
		warn("Can not remove CPU exclusive partition: %s", strerror(errno));
	}
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", constr, set->dryrun)){
		// global reset failed, try affinity only
		if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", set->affinity, set->dryrun)){
			warn("Can not reset CPU-affinity. Expect malfunction!"); // set online cpus as default
		}
	}
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.mems", numastr, set->dryrun)){
		warn("Can not set NUMA memory nodes : %s", strerror(errno));
	}

}

/*
 * setContCGroups : set docker group and  existing containers CGroups settings
 *
 * Arguments: - configuration parameter structure
 * 			  - numa nodes string
 *
 * Return value: -
 */
void
setContCGroups(prgset_t *set, int setCont) {

	int count = 0;		// FIXME: temp counter, see below to avoid docker reset and block of container start
	if (setCont){
		DIR *d;
		struct dirent *dir;
		d = opendir(set->cpusetdfileprefix);// -> pointing to global
		if (d) {
			char *contp = NULL; // clear pointer
			/// Reassigning pre-existing containers?
			while ((dir = readdir(d)) != NULL) {
#ifdef CGROUP2
				char * hex, * t, * t_tok;	// used to extract hex identifier from slice/scope in v2 fmt:'docker-<hex>.scope''
				t = strdup(dir->d_name);
				if (!(strtok_r(t, "-", &t_tok))
						|| !(hex = strtok_r(NULL, ".", &t_tok))) {
					free(t);
					continue;
				}
#else
				char * hex = dir->d_name;
#endif
				// scan trough docker CGroup, find them?
				// TODO: Restart removes pinning?
				if  ((DT_DIR == dir->d_type)
					 && (64 == (strspn(hex, "abcdef1234567890")))) {
					if ((contp=realloc(contp,strlen(set->cpusetdfileprefix)  // container strings are very long!
						+ strlen(dir->d_name)+1))) {
						// copy to new prefix
						contp = strcat(strcpy(contp,set->cpusetdfileprefix),dir->d_name);

						if (0 > setkernvar(contp, "/cpuset.cpus", set->affinity, set->dryrun)){
							warn("Can not set CPU-affinity : %s", strerror(errno));
						}
						if (0 > setkernvar(contp, "/cpuset.mems", set->numa, set->dryrun)){
							warn("Can not set NUMA memory nodes : %s", strerror(errno));
						}
						count++;
					}
					else // realloc error
						err_exit("could not allocate memory!");
				}
#ifdef CGROUP2
				free(t);
#endif
			}
			free (contp);
			closedir(d);
		}
		else
			warn("Can not open Docker CGroup directory: %s", strerror(errno));
	}

	// Docker CGroup settings and affinity
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", set->affinity, set->dryrun)){
		warn("Can not set CPU-affinity : %s", strerror(errno));
	}
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.mems", set->numa, set->dryrun)){
		warn("Can not set NUMA memory nodes : %s", strerror(errno));
	}
	if (AFFINITY_USEALL != set->setaffinity) // set exclusive only if not use-all
#ifdef CGROUP2
		// FIXME: count = 0, do not set to root as docker will overwrite cpuset on first container start and block task creation
		if (((count) || (!setCont)) && 0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus.partition", "root", set->dryrun)){
#else
		if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpu_exclusive", "1", set->dryrun)){
#endif
			warn("Can not set CPU exclusive partition: %s", strerror(errno));
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
	if (rHead || !(prgset->affinity_mask))	// does it exist? is the mast set?
		fatal("Memory management inconsistency: resTracer already present!");

	// backwards, cpu0 on top, we assume affinity_mask ok
	for (int i=(prgset->affinity_mask->size); i >= 0;i--)

		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by selected only
			push((void**)&rHead, sizeof(struct resTracer));
			rHead->affinity = i;
			rHead->status = MSK_STATHRMC;
//			rHead->U = 0.0;
//			rHead->Umax = 0.0;
			rHead->Umin = 1.0;
//			rHead->Uavg = 0.0;
//			rHead->basePeriod = 0;
	}
}

/*
 *  findPeriodMatch(): find a Period value that fits more the typical standards
 *
 *  Arguments:  - measured CDF period for non periodically scheduled tasks
 *
 *  Return value: - an approximation (nearest) period
 */
uint64_t
findPeriodMatch(uint64_t cdf_Period){
	if (!cdf_Period) // nothing defined. use default
		return NSEC_PER_SEC;

	// closing step size => find next higher power of 10 and divide by 40
	double step =  pow(10, ceil(log10((double)cdf_Period))) / 40.0;

	// return closest match with step distance to cdf_period
	return (uint64_t)(round((double)cdf_Period/step) * step);
}

/*
 *  checkUvalue(): verify if task fits into Utilization limits of a resource
 *
 *  Arguments:  - resource entry for this CPU
 * 				- the `attr` structure of the task we are trying to fit in
 * 				- add or test, 0 = test, 1 = add to resources
 *
 *  Return value: a matching score, lower is better, -1 = error
 */
int
checkUvalue(struct resTracer * res, struct sched_attr * par, int add) {
	uint64_t base = res->basePeriod;
	uint64_t used = res->usedPeriod;
	uint64_t baset = par->sched_period == 0 ? SCHED_PDEFAULT : par->sched_period;
	int hm = res->status & MSK_STATHRMC;	// harmonic?
	int rv = 0; // return value, perfect periods, best fit

	// review task parameters by scheduling type
	switch (par->sched_policy) {

		case SCHED_DEADLINE:
			break;

		case SCHED_RR:
			// if the runtime doesn't fit into the preemption slice..
			// reset base to slice as it will be preempted during run increasing task switching
			if (0 != par->sched_period)
				if (prgset->rrtime*SCHED_RRTONATTR <= par->sched_runtime)
					baset = prgset->rrtime*SCHED_RRTONATTR;

			// no break
		case SCHED_FIFO:
		// if set to "default", SCHED_OTHER or SCHED_BATCH, how do I react?
		default:

			if (0 == par->sched_runtime){
				// can't do anything about computation
				rv = INT_MAX;
				used += used*SCHED_UKNLOAD/100; // add 10% to load, as a dummy value
				break;
			}
	}

	// CPU unused, score = 2, preference to used resources
	if (0 == base){
		base = baset;
		rv = 2;
	}
	else {
		/*
		 * Recalculate hyper-period and check score
		 *
		 * base = baset = lcm  + harmonic	... 0 ideal
		 * base < baset = lcm  + harmonic   ... 1 subideal
		 * base = lcm > baset  + harmonic   ... 1 subideal
		 * start non harmonic >= 2
		 * baset < base -> ceil [ U * base / baset ] + non_harmonic
		 *
		 */

		// compute hyper-period
		uint64_t hyperP = lcm(base, baset);

		// are the periods a perfect fit?
		if (hm && base == baset)
				rv = 0;				// harmonic and p_i = p_m
		else if (hm && hyperP == baset)
				rv = 1;				// harmonic and p_i > p_m, candidate gets interrupted
		else if (hm && hyperP == base)
				rv = 2;				// harmonic and p_i < p_m, candidate interrupts
		else{
			// interruption score -> non harmonic !: verify how often new baset fits in runtime tot -> max interr.
			rv = MIN((int)((res->U * (double)base)/(double)baset)+SCHED_UHARMONIC, INT_MAX);
			hm = 0;
		}

		// recompute new values of resource tracer
		used = used * hyperP / base;
		base = hyperP;
	}

	used += par->sched_runtime * base/baset;

	// calculate and verify utilization rate
	float U = (double)used/(double)base;
	if (MAX_UL < U)
		rv = -1;

	if (add){ // apply changes to res structure?
		res->usedPeriod = used;
		res->basePeriod = base;
		res->U=U;
		res->status = (res->status & ~MSK_STATHRMC) | hm;
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
// TODO: exclude CPUs not in a PIDs associated affinity_mask
resTracer_t *
checkPeriod(struct sched_attr * attr, int affinity) {
	resTracer_t * ftrc = NULL;
	int last = INT_MAX;	// last checked tracer's score, max value by default
	float Ulast = 10.0;	// last checked traces's utilization rate
	int res;

	// loop through	all and return the best fit
	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		res = checkUvalue(trc, attr, 0);
		if ((0 <= res && res < last) // better match, or matching favorite
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
 *  				 uses run-time values for non-DEADLINE scheduled tasks
 *
 *  Arguments: - the item to check
 *  		   - include item in count
 *
 *  Return value: a pointer to the resource tracer
 * 					returns null if nothing is found
 */
resTracer_t *
checkPeriod_R(node_t * item, int include) {

	if (0 > item->mon.assigned)
		return NULL;

	int affinity = INT_MIN;
	resTracer_t * ftrc = NULL;

	if (!(include))
		recomputeCPUTimes_u(item->mon.assigned, item);

	if ((item->param) && (item->param->rscs))
		affinity = item->param->rscs->affinity;

	if (SCHED_DEADLINE == item->attr.sched_policy)
		ftrc = checkPeriod(&item->attr, affinity);
	else{
		struct sched_attr attr = { 48 };
		attr.sched_policy = item->attr.sched_policy;
		attr.sched_runtime = item->mon.cdf_runtime;
		attr.sched_period = findPeriodMatch(item->mon.cdf_period);
		ftrc = checkPeriod(&attr, affinity);
	}

	if (!(include) && 0 < item->mon.assigned)
		recomputeCPUTimes_u(item->mon.assigned, NULL);

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
 * 				Either U is smaller OR within 20% and Harmonic vs. not-Harmonic
 *
 *  Arguments: -
 *
 *  Return value: a pointer to the resource tracer
 *					returns null if nothing is found
 */
resTracer_t *
grepTracer() {
	resTracer_t * ftrc = NULL;
	float Umax = 1.0;

	// loop through all and return the best fit
	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		if ((trc->U < Umax)
			|| ((ftrc) && (trc->U < ftrc->U * 1.2)
				&& (ftrc->status & MSK_STATHRMC) && !(trc->status & MSK_STATHRMC))) {
			Umax = trc->U;
			ftrc = trc;
		}
	}
	return ftrc;
}

/*
 *  recomputeTimes_u(): recomputes base and utilization factor of a resource
 *
 *  Arguments:  - resource entry for this CPU
 *  			- node to skip for computation
 *
 *  Return value: Negative values return error
 */
static int
recomputeTimes_u(struct resTracer * res, node_t * skip) {

	struct resTracer * resNew = calloc (1, sizeof(struct resTracer));
	int rv = 0;

	// find PID switching from
	for (node_t * item = nhead; ((item)); item=item->next){
		if (item->mon.assigned != res->affinity
				|| 0 > item->pid || item == skip)
			continue;

		if (SCHED_DEADLINE == item->attr.sched_policy)
			rv = MIN(checkUvalue(resNew, &item->attr, 1), rv);
		else{
			struct sched_attr attr = { 48 };
			attr.sched_policy = item->attr.sched_policy;
			attr.sched_runtime = item->mon.cdf_runtime;
			attr.sched_period = findPeriodMatch(item->mon.cdf_period);
			rv = MIN(checkUvalue(resNew, &attr, 1), rv);
		}
	}

	res->basePeriod = resNew->basePeriod;
	res->usedPeriod = resNew->usedPeriod;
	res->U = resNew->U;

	free(resNew);
	return rv;
}

/*
 *  recomputeCPUTimes_u(): recomputes base and utilization factor of a CPU
 *  						variant used for updates with skip item
 *
 *  Arguments:  - CPU number
 *
 *  Return value: Negative values return error
 */
static int
recomputeCPUTimes_u(int32_t CPUno, node_t * skip) {
	if (-1 == CPUno)	// default, not assigned
		return 0;

	resTracer_t * trc;

	if ((trc = getTracer(CPUno)))
		return recomputeTimes_u(trc, skip);

	return -2; // not found! ERROR
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
	return recomputeCPUTimes_u(CPUno, NULL);
}

/* ------------------- Node configuration management from here ---------------------------- */

/*
 *  duplicateOrRefreshContainer(): duplicate or container configuration with data based on names
 *  						data from DockerLink
 *
 *  Arguments: - Node with data from docker_link
 *  		   - Configuration structure
 *  		   - Pointer to container/image configuration found by search
 *
 *	Return: - pointer to duplicate or refreshed container
 */
static cont_t *
duplicateOrRefreshContainer(node_t* dlNode, struct containers * configuration, cont_t * dlCont) {

	cont_t * cont;

	// check if the container has already been added with ID, update PIDs
	for (cont = configuration->cont; (cont); cont=cont->next){
		if (cont != dlCont && (cont->status & MSK_STATCCRT)
				&& !strncmp(cont->contid, cont->contid,
						MIN(strlen(cont->contid), strlen(dlCont->contid))))
			break;
	}
	if (!cont){
		// not found?
		push((void**)&configuration->cont, sizeof(cont_t));
		cont = configuration->cont;

		cont->contid = dlNode->contid; // tranfer to avoid 12 vs 64 len issue
	}

	{
		// TODO: refactor - combine with others in findPidParameters
		// duplicate resources if needed
		cont->status = dlCont->status;
		if (!(cont->status & MSK_STATSHAT)){
			cont->attr = malloc(sizeof(struct sched_attr));
			(void)memcpy(cont->attr, dlCont->attr, sizeof(struct sched_attr));
		}
		else
			cont->attr = dlCont->attr;

		if (!(cont->status & MSK_STATSHRC)){
			cont->rscs = malloc(sizeof(struct sched_rscs));
			(void)memcpy(cont->rscs, dlCont->rscs, sizeof(struct sched_rscs));
			cont->rscs->affinity_mask = numa_allocate_cpumask();
			if (dlCont->rscs->affinity_mask){
				numa_or_cpumask(dlCont->rscs->affinity_mask, cont->rscs->affinity_mask);
			}
		}
		else
			cont->rscs = dlCont->rscs;
	}

	// fill image field
	cont->img = dlCont->img;
	if (cont->img){
		push((void**)&cont->img->conts, sizeof(conts_t));
		cont->img->conts->cont = cont;
	}

	// update connected PIDs (if present), they share configuration with the container (= update)
	for (pids_t * pids = cont->pids; (pids) ; pids=pids->next){
		//update container link and shared resources
		// TODO: needs shared rcs flag?
		pids->pid->attr = cont->attr;
		pids->pid->rscs = cont->rscs;
		pids->pid->img = cont->img;

		// fill image field
		pids->pid->img = cont->img;
		if (cont->img){
			push((void**)&cont->img->pids, sizeof(pids_t));
			cont->img->pids->pid = pids->pid;
		}
	}

	// duplicate Original container PIDs (if present)
	for (pids_t * pids = dlCont->pids; (pids) ; pids=pids->next){

		// add link to pid and pid
		push((void**)&cont->pids, sizeof(pids_t));
		push((void**)&configuration->pids, sizeof(pidc_t));
		cont->pids->pid=configuration->pids;

		configuration->pids->status = pids->pid->status;

		{
			// TODO: Refactor with above and findPidParameters
			if (!(configuration->pids->status & MSK_STATSHAT)){
				configuration->pids->attr = malloc(sizeof(struct sched_attr));
				(void)memcpy(configuration->pids->attr, pids->pid->attr, sizeof(struct sched_attr));
			}
			else
				configuration->pids->attr = pids->pid->attr;


			if (!(cont->status & MSK_STATSHRC)){
				configuration->pids->rscs = malloc(sizeof(struct sched_rscs));
				(void)memcpy(configuration->pids->rscs, pids->pid->rscs, sizeof(struct sched_rscs));
				configuration->pids->rscs->affinity_mask = numa_allocate_cpumask();
				if (pids->pid->rscs->affinity_mask){
					numa_or_cpumask(pids->pid->rscs->affinity_mask, configuration->pids->rscs->affinity_mask);
				}
			}
			else
				configuration->pids->rscs = pids->pid->rscs;
		}

		// fill image field
		configuration->pids->img = pids->pid->img;
		if (pids->pid->img){
			push((void**)&configuration->pids->img->pids, sizeof(pids_t));
			configuration->pids->img->pids->pid = configuration->pids;
		}
	}

	// update all running nodes
	for (node_t * item = nhead; (item); item=item->next)
		if (item->param && item->param->cont
				&& item->param->cont == cont)
			//set update flag
			item->status &= ~MSK_STATUPD;

	free(dlNode->psig); // clear entry to avoid confusion
	dlNode->psig = NULL;

	return cont;
}

/*
 *  findPidParameters(): assigns the PID parameters list of a running container
 *
 *  Arguments: - node to check for matching parameters
 *  		   - pid configuration list head
 *
 *  Return value: 0 if successful, -1 if unsuccessful
 */
int
findPidParameters(node_t* node, containers_t * configuration){

	struct img_parm * img = configuration->img;
	struct cont_parm * cont = NULL;
	// check for image match first
	while (NULL != img) {

		if(img->imgid && node->imgid && !strncmp(img->imgid, node->imgid
				, MIN(strlen(img->imgid), strlen(node->imgid)))) {
			// TODO: refactor below with cont!
			conts_t * imgcont = img->conts;
			printDbg(PIN2 "Image match %s\n", img->imgid);
			// check for container match
			while (NULL != imgcont) {
				if (imgcont->cont->contid && node->contid) {

					if  (!strncmp(imgcont->cont->contid, node->contid,
							MIN(strlen(imgcont->cont->contid), strlen(node->contid)))
							&& ((node->pid) || !(imgcont->cont->status & MSK_STATCCRT))) {
						cont = imgcont->cont;
						break;
					}
					// if node pid = 0, psig is the name of the container coming from dockerlink
					else if (!(node->pid) && node->psig && !strcmp(imgcont->cont->contid, node->psig)) {
						cont = imgcont->cont;
						cont = duplicateOrRefreshContainer(node, configuration, cont);
						break;
					}
				}
				imgcont = imgcont->next;
			}
			break; // if imgid is found, keep trace in img -> default if nothing else found
		}
		img = img->next;
	}

	// we might have found the image, but still
	// not in the images, check all containers
	if (!cont) {
		cont = configuration->cont;

		// check for container match
		while (NULL != cont) {

			if(cont->contid && node->contid) {
				if (!strncmp(cont->contid, node->contid,
						MIN(strlen(cont->contid), strlen(node->contid)))
						&& ((node->pid) || !(cont->status & MSK_STATCCRT)))
					break;

				// if node pid = 0, psig is the name of the container coming from dockerlink
				else if (!(node->pid) && node->psig && !strcmp(cont->contid, node->psig)) {
					cont = duplicateOrRefreshContainer(node, configuration, cont);
					break;
				}
			}
			cont = cont->next;
		}
	}

	// did we find a container or image match?
	if (img || cont) {
		// read all associated PIDs. Is it there?

		// assign pids from cont or img, depending what is found
		int useimg = (img && !cont);
		struct pids_parm * curr = (useimg) ? img->pids : cont->pids;

		// check the first result
		while (NULL != curr) {
			if(curr->pid->psig && node->psig && strstr(node->psig, curr->pid->psig)) {
				// found a matching pid inc root container
				node->param = curr->pid;
				return 0;
			}
			curr = curr->next;
		}

		// if both were found, check again in image
		if (img && cont){
			curr = img->pids;
			while (NULL != curr) {
				if(curr->pid->psig && node->psig && strstr(node->psig, curr->pid->psig)) {
					// found a matching pid inc root container
					node->param = curr->pid;
					return 0;
				}
				curr = curr->next;
			}
		}

		// found? if not, create PID parameter entry
		printDbg(PIN2 "... parameters not found, creating from PID and assigning container settings\n");
		push((void**)&configuration->pids, sizeof(pidc_t));
		if (useimg) {
			// add new container unmatched container signature
			push((void**)&configuration->cont, sizeof(cont_t));
			push((void**)&img->conts, sizeof(conts_t));
			img->conts->cont = configuration->cont;
			cont = configuration->cont;
			cont->img = img;

			// assign values
			// CAN be null, should not happen, i.e. img & !cont
			cont->contid = node->contid; // keep string, unused will be freed (node_pop)
			cont->status |= MSK_STATSHAT | MSK_STATSHRC;
			cont->rscs = img->rscs;
			cont->attr = img->attr;
		}

		// add new PID to container PIDs
		push((void**)&cont->pids, sizeof(pids_t));
		cont->pids->pid = configuration->pids; // add new empty item -> pid list, container pids list

		configuration->pids->status |= MSK_STATSHAT | MSK_STATSHRC;
		configuration->pids->rscs = cont->rscs;
		configuration->pids->attr = cont->attr;

		// Assign configuration to node
		node->param = configuration->pids;
		node->param->img = img;
		node->param->cont = cont;
//		node->param->psig = node->psig;	# do not set psig-> = specific parameters for this process
		// update counter
		if (node->pid)
			configuration->nthreads++;
		return 0;
	}
	else
	 if (node->pid) { // !=0 means not container or image
		// no match found. and now?
		printDbg(PIN2 "... container not found, trying PID scan\n");

		// start from scratch in the PID config list only. Maybe Container ID is new
		struct pidc_parm * curr = configuration->pids;

		while (NULL != curr) {
			if(curr->psig && node->psig && strstr(node->psig, curr->psig)
				&& !(curr->cont) && !(curr->img) ) { // only un-asociated items
				warn("assigning configuration to unrelated PID");

				// TODO: make function - refactor
				// duplicate pidc and copy all info (can not detect if config is shared!)
				push((void**)&configuration->pids, sizeof(pidc_t));
				node->param = configuration->pids;
				node->param->psig = strdup(curr->psig);

				node->param->rscs = malloc(sizeof(rscs_t));
				(void)memcpy(node->param->rscs, curr->rscs, sizeof(rscs_t));
				if (curr->rscs->affinity_mask){
					node->param->rscs->affinity_mask = numa_allocate_cpumask();
					copy_bitmask_to_bitmask(node->param->rscs->affinity_mask, curr->rscs->affinity_mask);
				}
				node->param->attr = malloc(sizeof(struct sched_attr));
				(void)memcpy(node->param->attr, curr->attr, sizeof(struct sched_attr));

				// update counter FIXME: needed?
				configuration->nthreads++;
				break;
			}
			curr = curr->next;
		}

		if (!node->contid || !node->psig){
			// no container id and psig, can't do anything for reconstruction
			if (curr)
				return 0;
			printDbg(PIN2 "... PID not found. Ignoring\n");
			return -1;
		}

		// add new container for the purpose of grouping
		push((void**)&configuration->cont, sizeof(cont_t));
		cont = configuration->cont;

		// assign values
		cont->contid = node->contid;  // keep string, unused will be freed (node_pop)
		cont->status |= MSK_STATCCRT | MSK_STATSHAT | MSK_STATSHRC; // (created at runtime from node)
		cont->rscs = configuration->rscs;
		cont->attr = configuration->attr;

		if (!curr){
			// config not found, create PID parameter entry
			printDbg(PIN2 "... parameters not found, creating from PID settings and container\n");
			// create new pidconfig
			push((void**)&configuration->pids, sizeof(pidc_t));
			curr = configuration->pids;

			curr->psig = node->psig;  // keep string, unused will be freed (node_pop)
			curr->status |= MSK_STATSHAT | MSK_STATSHRC;
			curr->rscs = cont->rscs;
			curr->attr = cont->attr;

			// add new PID configuration to container PIDs
			push((void**)&cont->pids, sizeof(pids_t));
			cont->pids->pid = curr;

			node->param = curr;
			// update counter
			configuration->nthreads++;
		}
		else {
			// found use it's values
			free(node->psig);
			node->psig = node->param->psig;
		}
		// pidconfig curr gets container config cont
		curr->cont = cont;
		return 0;
	}

	return -1;
}
