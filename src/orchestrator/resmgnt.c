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
 *	Arguments: - PID of tasks
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
 *	Return value: 0 on success, -1 or -2 otherwise
 */
static int
setPidAffinityNode (node_t * node){

	int ret = 0;

	{	// add PID to docker CGroup
		//TODO: warn ! this removes it if it's already present in a subgroup!
		// -> can not see subgroup contents! -> visible in /proc/150985/cgroup , v2 format 0::/path/from/cgroup/root

		char pid[11]; // PID is 10 digits + \0 now
		(void)sprintf(pid, "%d", node->pid);
		if (0 > setkernvar(prgset->cpusetdfileprefix , CGRP_PIDS, pid, prgset->dryrun & MSK_DRYNOAFTY)){
			printDbg(PIN2 "Warn! Can not move task %s\n", pid);
			ret = -1;
		}

	}

	if (!(node->param) || !(node->param->rscs->affinity_mask)){
		err_msg("No valid parameters or bit-mask allocation!");
		return -1;
	}

	// return -1(-2) if one (or two) failed
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
		err_msg_n(errno,"setting affinity for PID %d", node->pid);
		return -1;
	}
	// reset no affinity bit (if set)
	node->status &= ~MSK_STATNAFF;
	return 0;
}

/*
 *	getPidAffinityAssingedNr: get the number of CPUs that have an affinity with the PID
 *
 *	Arguments: - pointer to node with data
 *
 *	Return value: CPU-count, -1 otherwise
 */
int
getPidAffinityAssingedNr(node_t * node){
	if (node->mon.assigned_mask)
		numa_bitmask_clearall(node->mon.assigned_mask);
	else
		node->mon.assigned_mask = numa_allocate_cpumask();

	// get affinity WARN wrongly specified in man(7), returns error or number of bytes read
	if (0 > numa_sched_getaffinity(node->pid, node->mon.assigned_mask)){
		err_msg_n(errno,"getting affinity for PID %d", node->pid);
		return -1;
	}

	return (int)numa_bitmask_weight(node->mon.assigned_mask);
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

	if (!(node->param) || !(node->param->cont)){
		err_msg("Trying to set Container affinity without container configuration!");
		return -1;
	}
	if (!node->param->cont->rscs->affinity_mask) {
		err_msg("No valid parameters or bit-mask allocation!");
		return -1;
	}

	char *contp = NULL;
	char affinity[CPUSTRLEN];
	char affinity_old[CPUSTRLEN];
	int ret = 0;

	if (parse_bitmask (node->param->cont->rscs->affinity_mask, affinity, CPUSTRLEN)){
			err_msg("Can not determine container affinity list!");
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
#ifdef CGROUP2
		// CGroup V2 uses separate file to check used value, e.g. inherited from parent
		if (0 > getkernvar(contp, "/cpuset.cpus.effective", affinity_old, CPUSTRLEN))
#else
		if (0 > getkernvar(contp, "/cpuset.cpus", affinity_old, CPUSTRLEN))
#endif
			warn("Can not read %.12s's CGroups CPU's", node->contid);

		if (strcmp(affinity, affinity_old)){
			cont( "reassigning %.12s's CGroups CPU's to %s", node->contid, affinity);
			if (0 > setkernvar(contp, "/cpuset.cpus", affinity, prgset->dryrun & MSK_DRYNOAFTY)){
				warn("Can not set CPU-affinity : %s", strerror(errno));
				ret = -1;
			}
		}
	}
	else{
		err_msg("Failed to allocate memory!");
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
		// If fixed affinity is set, set right away and as active including setAffinity
		if (node->pid && (0 <= node->param->rscs->affinity)){
			// at start, assign node to static/adaptive table affinity match
			node->mon.assigned = node->param->rscs->affinity;

			if (setPidAffinityAssinged(node))
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

	if (0 == node->pid){
		// PID 0 = detected containers, docker-link call
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

		if (sched_setattr (node->pid, node->param->attr, 0U))	// Custom function!
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

	if (!node->pid)
		return;

	// check siblings in container and update them
	int hasSiblings = node->status & MSK_STATSIBL;

	for (node_t * item = nhead; (item); item=item->next)
		if (node != item && item->param && item->param->cont && node->param
			&& item->param->cont == node->param->cont){		// same container parameter = same container
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
		printDbg(PIN "Unable to read parameters for PID %d: %s", node->pid, strerror(errno));
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

		if (sched_setattr (node->pid, &(node->attr), 0U))	// Custom function!
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

	if (sched_setattr (node->pid, &(node->attr), 0U))	// Custom function!
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
	char kparam[20]; // pid{x}/cmdline read string

	if ((cmdline = malloc(MAXCMD_LEN))) { // alloc memory for strings

		(void)sprintf(kparam, "%d/cmdline", node->pid);
		if (0 > getkernvar("/proc/", kparam, cmdline, MAXCMD_LEN)){
			// try to read cmdline of pid
			warn("can not read PID %d's command line: %s", node->pid, strerror(errno));
			free(cmdline);
			return;
		}

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
		err_exit("Could not allocate memory!");
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
 * 			  - CPU online string
 * 			  - NUMA nodes string
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
			if  ((DT_DIR == dir->d_type)
				 && (64 == (strspn(hex, "abcdef1234567890")))) {
				if ((contp=realloc(contp,strlen(set->cpusetdfileprefix)  // container strings are very long!
					+ strlen(dir->d_name)+1))) {
					// copy to new prefix
					contp = strcat(strcpy(contp,set->cpusetdfileprefix),dir->d_name);

					// remove exclusive!
#ifdef CGROUP2
					if (0 > setkernvar(contp, "/cpuset.cpus.partition", "member", set->dryrun & MSK_DRYNOCGRPRT)){
#else
					if (0 > setkernvar(contp, "/cpuset.cpu_exclusive", "0", set->dryrun & MSK_DRYNOCGRPRT)){
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
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus.partition", "member", set->dryrun & MSK_DRYNOCGRPRT)){
#else
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpu_exclusive", "0", set->dryrun & MSK_DRYNOCGRPRT)){
#endif
		warn("Can not remove CPU exclusive partition: %s", strerror(errno));
	}
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", constr, set->dryrun & MSK_DRYNOAFTY)){
		// global reset failed, try affinity only
		if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", set->affinity, set->dryrun & MSK_DRYNOAFTY)){
			warn("Can not reset CPU-affinity. Expect malfunction!"); // set online cpus as default
		}
	}
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.mems", numastr, set->dryrun & MSK_DRYNOAFTY)){
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

	int count = 0;		// counter, see below, to avoid docker reset and block of container start
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
				if  ((DT_DIR == dir->d_type)
					 && (64 == (strspn(hex, "abcdef1234567890")))) {
					if ((contp=realloc(contp,strlen(set->cpusetdfileprefix)  // container strings are very long!
						+ strlen(dir->d_name)+1))) {
						// copy to new prefix
						contp = strcat(strcpy(contp,set->cpusetdfileprefix),dir->d_name);

						if (0 > setkernvar(contp, "/cpuset.cpus", set->affinity, set->dryrun & MSK_DRYNOAFTY)){
							warn("Can not set CPU-affinity : %s", strerror(errno));
						}
						if (0 > setkernvar(contp, "/cpuset.mems", set->numa, set->dryrun & MSK_DRYNOAFTY)){
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
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", set->affinity, set->dryrun & MSK_DRYNOAFTY)){
		warn("Can not set CPU-affinity : %s", strerror(errno));
	}
	if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.mems", set->numa, set->dryrun & MSK_DRYNOAFTY)){
		warn("Can not set NUMA memory nodes : %s", strerror(errno));
	}
	if (AFFINITY_USEALL != set->setaffinity) // set exclusive only if not use-all
#ifdef CGROUP2
		// count = 0, do not set to root as docker will overwrite CPUset on first container start and block task creation
		if (((count) || (!setCont)) && 0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus.partition", "root",
				(set->dryrun & MSK_DRYNOCGRPRT) || AFFINITY_USEALL == set->setaffinity)){ // do not set root if we have all used
#else
		if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpu_exclusive", "1",
				(set->dryrun & MSK_DRYNOCGRPRT) || AFFINITY_USEALL == set->setaffinity)){ // do not set exclusive if we have all used
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
	if (0 > setkernvar(set->procfileprefix, "sched_rt_runtime_us", value, set->dryrun & MSK_DRYNORTTHRT)){
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
	if (rHead || !(prgset->affinity_mask))	// does it exist? is the mask set?
		err_exit("Memory management inconsistency: resTracer already present!");

	// backwards, cpu0 on top, we assume affinity_mask ok
	for (int i=(prgset->affinity_mask->size-1); i >= 0;i--)

		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by selected only
			push((void**)&rHead, sizeof(struct resTracer));
			rHead->affinity = numa_allocate_cpumask();
			numa_bitmask_setbit(rHead->affinity, i);
			rHead->numa = numa_node_of_cpu(i);
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
 *  Return value: a matching score, lower is better, -1 = error / over-utilizzation
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
 *  		   - the set affinity, positive = fixed, negative = preference
 *  		   - the running CPU, -1 if not set yet
 *
 *  Return value: a pointer to the resource tracer
 * 					returns null if nothing is found
 */
resTracer_t *
checkPeriod(struct sched_attr * attr, int affinity, int CPU) {
	resTracer_t * ftrc = NULL;
	int last = INT_MAX;	// last checked tracer's score, max value by default
	float Ulast = 10.0;	// last checked traces's utilization rate
	int res;

	// hard-affinity, return right away
	if (0 <= affinity)
		return getTracer(affinity);

	// loop through	all and return the best fit
	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){

		res = checkUvalue(trc, attr, 0);
		if ((0 <= res && res < last)	// better match
			|| ((res == last) &&		// equal match but!

				( (trc->U <  Ulast * ULTOLMIN) ||	// Load is lower or
				 ((trc->U <= Ulast * ULTOLMAX) &&	// equal Ul (tollerance) with either CPU or -affinity match
					   (((0 <= CPU) &&     (numa_bitmask_isbitset(trc->affinity, CPU)))			// CPU is a favorite
					|| ((0 > affinity) && (numa_bitmask_isbitset(trc->affinity, -affinity))))	// CPU is a favorite
				 	 	 ))
				))	{

			// skip if found tracer is preference and values are the same
			if ((res == last) && (trc->U >= Ulast * ULTOLMIN) && (trc->U <= Ulast * ULTOLMAX) && (ftrc)
					&& (0 > affinity) && (numa_bitmask_isbitset(ftrc->affinity, -affinity)))
				continue;
			last = res;
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

	// temp update without item
	if (!(include))
		if (0 > recomputeCPUTimes_u(item->mon.assigned, item))
			printDbg(PIN2 "Recompute times for CPU %d unsuccessful!", item->mon.assigned);

	if ((item->param) && (item->param->rscs))
		affinity = item->param->rscs->affinity;

	if (SCHED_DEADLINE == item->attr.sched_policy)
		ftrc = checkPeriod(&item->attr, affinity, item->mon.assigned);
	else{
		struct sched_attr attr = { SCHED_ATTR_SIZE };
		attr.sched_policy = item->attr.sched_policy;
		attr.sched_runtime = item->mon.cdf_runtime;
		attr.sched_period = findPeriodMatch(item->mon.cdf_period);
		ftrc = checkPeriod(&attr, affinity, item->mon.assigned);
	}

	// reset to with item
	if (!(include))
		if (0 > recomputeCPUTimes_u(item->mon.assigned, NULL))
			printDbg(PIN2 "Recompute times for CPU %d unsuccessful!", item->mon.assigned);

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

	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		if (numa_bitmask_isbitset(trc->affinity, CPUno))
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
			|| ((ftrc) && (trc->U < ftrc->U * 1.2) // 20% more slack if we are harmonic
				&& !(ftrc->status & MSK_STATHRMC) && (trc->status & MSK_STATHRMC))) {
			Umax = trc->U;
			ftrc = trc;
		}
	}
	return ftrc;
}

/*
 *  getTracerMainCPU(): get the main CPU resource tracer for CPU x
 *
 *  Arguments: - - resource entry for the CPU
 *
 *  Return value: number of primary CPU (=thread)
 *					returns -1 if none is set
 */
int
getTracerMainCPU(resTracer_t * res) {
	if (!res || !res->affinity)
		return -1;

	// loop through all and return first fit
	for (int i = 0; i<res->affinity->size; i++){
		if (numa_bitmask_isbitset(res->affinity, i))
			return i;
	}

	return -1;
}

/*
 *  recomputeTimes_u(): recomputes base and utilization factor of a resource
 *
 *  Arguments:  - resource entry for this CPU
 *  			- node to skip for computation
 *
 *  Return value: Negative values return error; -1 = error / over-utilizzation
 */
static int
recomputeTimes_u(struct resTracer * res, node_t * skip) {

	struct resTracer * resNew = calloc (1, sizeof(struct resTracer));
	int rv = 0;

	// find PID switching from
	for (node_t * item = nhead; ((item)); item=item->next){
		if (!numa_bitmask_isbitset(res->affinity, item->mon.assigned)
				|| 0 > item->pid || item == skip)
			continue;

		if (SCHED_DEADLINE == item->attr.sched_policy)
			rv = MIN(checkUvalue(resNew, &item->attr, 1), rv);
		else{
			struct sched_attr attr = { SCHED_ATTR_SIZE };
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
 *  recomputeTimes(): recomputes base and utilization factor of a resource
 *
 *  Arguments:  - resource entry for this CPU
 *
 *  Return value: Negative values return error; -1 = error / over-utilizzation
 */
int
recomputeTimes(struct resTracer * res) {

	return recomputeTimes_u(res, NULL);
}

/*
 *  recomputeCPUTimes_u(): recomputes base and utilization factor of a CPU
 *  						variant used for updates with skip item
 *
 *  Arguments:  - CPU number
 *
 *  Return value: Negative values return error
 *  		-1 = error / over-utilizzation
 *  		-2 = tracer not found
 */
static int
recomputeCPUTimes_u(int32_t CPUno, node_t * skip) {
	if (0 > CPUno)	// default, not assigned
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
 *  Return value: Negative values return error; -1 = error / over-utilizzation
 *  		-1 = error / over-utilizzation
 *  		-2 = tracer/CPU number not found
 */
int
recomputeCPUTimes(int32_t CPUno) {
	return recomputeCPUTimes_u(CPUno, NULL);
}

/* ------------------- Node configuration management from here ---------------------------- */

/*
 *  duplicateOrRefreshContainer(): duplicate or update container configuration with data based on names
 *  						data from DockerLink
 *
 *  We use dlNode to create a new container configuration as dlCont is name-based and has no ID info
 *
 *  Arguments: - Node with data from docker_link ( psig = contName, contid = id )
 *  		   - Configuration structure
 *  		   - Pointer to container/image configuration found by search (using NAME = contid )
 *
 *	Return: - pointer to duplicate or refreshed container
 */
static cont_t *
duplicateOrRefreshContainer(node_t* dlNode, struct containers * configuration, cont_t * dlCont) {

	cont_t * cont;

	// check if the container has already been added with ID, update PIDs
	for (cont = configuration->cont; (cont); cont=cont->next){
		if (cont != dlCont && (cont->status & MSK_STATCCRT)
				&& !strncmp(cont->contid, dlNode->contid,
						MIN(strlen(cont->contid), strlen(dlNode->contid))))
			break;
	}
	if (!cont){
		// not found? add new configuration
		push((void**)&configuration->cont, sizeof(cont_t));
		cont = configuration->cont;

		cont->contid = strdup(dlNode->contid); // transfer to avoid 12 vs 64 len issue
	}

	copyResourceConfigC(dlCont, cont);

	// fill image field
	cont->img = dlCont->img;
	if (cont->img){
		push((void**)&cont->img->conts, sizeof(conts_t));
		cont->img->conts->cont = cont; // add back reference from Img to Cont
	}

	// update connected PIDs (if present - from RT detect), they share configuration with the container (= update)
	for (pids_t * pids = cont->pids; (pids) ; pids=pids->next){
		//update container link and shared resources (flag set at creation)
		pids->pid->attr = cont->attr;
		pids->pid->rscs = cont->rscs;
		pids->pid->img = cont->img;

		if (cont->img){
			push((void**)&cont->img->pids, sizeof(pids_t));
			cont->img->pids->pid = pids->pid;// add back reference from Img to PID
		}
	}

	// duplicate Original container PID configurations (if present)
	for (pids_t * pids = dlCont->pids; (pids) ; pids=pids->next){

		// add link to pid and pid
		push((void**)&cont->pids, sizeof(pids_t));
		push((void**)&configuration->pids, sizeof(pidc_t));
		cont->pids->pid=configuration->pids;

		if (pids->pid->psig)
			configuration->pids->psig = strdup(pids->pid->psig);
		configuration->pids->cont = cont;

		copyResourceConfigP(pids->pid ,configuration->pids);

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
			// reset update flag (-> do update)
			item->status &= ~MSK_STATUPD;

	return cont;
}


/*
 *  checkContainerMatch(): checks if a container matches node, by id or name
 *  				and creates (duplicates...) configuration if needed (call)
 *
 *  Arguments: - pointer to next container configuration
 *  		   - node to check for matching parameters
 *
 *  Return value: 1 if found - updates cont!
 */
static int
checkContainerMatch(cont_t ** cont, node_t * node, containers_t * configuration){
	if((*cont)->contid && node->contid) {
		// Match by ID
		if (!strncmp((*cont)->contid, node->contid,
				MIN(strlen((*cont)->contid), strlen(node->contid))))
			return 1;

		// Match by name: if node pid = 0, psig is the name of the container coming from docker-link
		else if (!(node->pid) && node->psig && !strcmp((*cont)->contid, node->psig)) {
			(*cont) = duplicateOrRefreshContainer(node, configuration, (*cont));
			return 1;
		}
	}
	return 0;
}

/*
 *  findPidParameters(): assigns the PID parameters list of a running container
 *
 *  Arguments: - node to check for matching parameters
 *  		   - configuration list head
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

			conts_t * imgcont = img->conts;
			printDbg(PIN2 "Image match %s\n", img->imgid);
			// check for container match
			while (NULL != imgcont) {
				cont = imgcont->cont;

				if ((checkContainerMatch(&cont, node, configuration)))
					break;
				imgcont = imgcont->next;
			}
			cont = NULL;
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
			if ((checkContainerMatch(&cont, node, configuration)))
				break;
			cont = cont->next;
		}
	}

	// did we find a container or image match?
	if (img || cont) {
		// Image and/or container configuration found

		int useimg = (img && !cont);

		if (useimg) {
			// if container does note exist yet, add new container from image configuration
			push((void**)&configuration->cont, sizeof(cont_t));
			push((void**)&img->conts, sizeof(conts_t));
			img->conts->cont = configuration->cont;
			cont = configuration->cont;
			cont->img = img;

			// assign values
			cont->contid = strdup(node->contid);
			cont->status |= MSK_STATSHAT | MSK_STATSHRC;
			cont->rscs = img->rscs;
			cont->attr = img->attr;
		}

		// Do we have a Docker-Link update or PID information
		if (!node->pid){ // = 0 means psig is a container name from dlink -> can not use it
			// container created at runtime and we have Image info from docker -> update with image info
			if ((img) && (cont->status & MSK_STATCCRT)) {

				push((void**)&img->conts, sizeof(conts_t));
				img->conts->cont = cont;
				cont->img = img;

				int oldstat = cont->status;
				// free old resources if set
				if (!(oldstat & MSK_STATSHAT))
					free(cont->attr);
				if (!(oldstat & MSK_STATSHRC)){
					numa_free_cpumask(cont->rscs->affinity_mask);
					free(cont->rscs);
				}
				oldstat &= ~(MSK_STATSHAT | MSK_STATSHRC | MSK_STATCCRT); // Remove shared and created at runtime

				// copy and update
				copyResourceConfigC((cont_t*)img, cont);
				cont->status |= oldstat;			// restore status
				cont->status &= ~MSK_STATCCRT;		// Update, no longer created at runtime
			}

			// create fake node parameters, used to assign img/cont only
			node->param = calloc(1, sizeof(pidc_t));
			node->param->cont = cont;
			node->param->img = img;
			return 0;
		}

		// PID detection result =>
		// read all associated PIDs. Is it there?

		if (!img && cont && cont->img){
			// detected container knows parent -> assign
			img=cont->img;
		}

		// assign PIDs from cont or img, depending what is found
		struct pids_parm * curr = (useimg) ? img->pids : cont->pids;

		// check the first result
		while (NULL != curr) {
			if(curr->pid->psig && node->psig && strstr(node->psig, curr->pid->psig)) {
				// found a matching PID in root container
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
					// found a matching PID in root container
					node->param = curr->pid;
					return 0;
				}
				curr = curr->next;
			}
		}

		// found? if not, create PID parameter entry
		printDbg(PIN2 "... parameters not found, creating from PID and assigning container settings\n");
		push((void**)&configuration->pids, sizeof(pidc_t));

		// add new PID to container PIDs
		push((void**)&cont->pids, sizeof(pids_t));
		cont->pids->pid = configuration->pids;

		configuration->pids->status |= MSK_STATSHAT | MSK_STATSHRC;
		configuration->pids->rscs = cont->rscs;
		configuration->pids->attr = cont->attr;

		// Assign configuration to node
		node->param = configuration->pids;
		node->param->img = NULL;		// even if we know it, keep dedicated to this container
		node->param->cont = cont;
		node->param->psig = NULL;		// do not set psig-> = specific parameters for this process
		// update counter
		configuration->nthreads++;
		return 0;
	}
	else
	 if (node->pid) { // !=0 means not container or image
		// no match found. and now?
		printDbg(PIN2 "... container not found, trying PID scan\n");

		// start from scratch in the PID configuration list only. Maybe Container ID is new
		struct pidc_parm * curr = configuration->pids;

		while (NULL != curr) {
			if(curr->psig && node->psig && strstr(node->psig, curr->psig)
				&& !(curr->cont) && !(curr->img) ) { // only un-asociated items
				warn("assigning configuration to unrelated PID");

				// duplicate PID configuration and copy all info (can not detect if configuration is shared!)
				push((void**)&configuration->pids, sizeof(pidc_t));
				node->param = configuration->pids;
				node->param->psig = strdup(curr->psig);

				int oldst = curr->status;
				curr->status &= ~MSK_STATSHAT & ~MSK_STATSHRC; // Un-mask
				copyResourceConfigP(curr, node->param);
				curr->status=oldst;

				configuration->nthreads++;
				break;
			}
			curr = curr->next;
		}

		if (!node->contid || !node->psig){
			// no container id and PID signature, can't do anything for reconstruction
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
			// PID configuration not found, create PID parameter entry
			printDbg(PIN2 "... parameters not found, creating from PID settings and container\n");
			// create new PID configuration
			push((void**)&configuration->pids, sizeof(pidc_t));
			curr = configuration->pids;

			curr->psig = node->psig;  // keep string, unused will be freed (node_pop)
			curr->status |= MSK_STATSHAT | MSK_STATSHRC;
			curr->rscs = cont->rscs;
			curr->attr = cont->attr;

			node->param = curr;
			// update counter
			configuration->nthreads++;
		}
		else {
			// found use it's values
			free(node->psig);
			node->psig = node->param->psig;
		}
		// add new PID configuration to container PIDs
		push((void**)&cont->pids, sizeof(pids_t));
		cont->pids->pid = curr;

		// PID configuration 'curr' gets container configuration 'cont'
		node->param->cont = cont;

		return 0;
	}

	// we end here if DL detects container/image and we have no configuration
	return -1;
}
