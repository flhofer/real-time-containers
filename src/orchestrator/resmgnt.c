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
 *
 *	Arguments: - pointer to node with data
 *			   - bit-mask for affinity
 *
 *	Return value: 0 on success, -1 otherwise
 */
static int
setPidAffinity (node_t * node, struct bitmask * cset){
	// add PID to docker CGroup
	char pid[6]; // PID is 5 digits + \0
	(void)sprintf(pid, "%d", node->pid);
	int ret = 0;

	if (0 > setkernvar(prgset->cpusetdfileprefix , "tasks", pid, prgset->dryrun)){
		printDbg( "Warn! Can not move task %s\n", pid);
		ret = -1;
	}

	// Set affinity
	if (numa_sched_setaffinity(node->pid, cset)){
		err_msg_n(errno,"setting affinity for PID %d",
			node->pid);
		ret = -1;
	}
	else
		cont("PID %d reassigned to CPU%d", node->pid,
			node->param->rscs->affinity);

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
setContainerAffinity(node_t * node, struct bitmask * cset){
	char *contp = NULL;
	char affinity[CPUSTRLEN];
	char affinity_old[CPUSTRLEN];
	int ret = 0;

	if (parse_bitmask (cset, affinity, CPUSTRLEN)){
			err_msg("Can not determine inverse affinity mask!");
			return -1;
	}

	if ((contp=malloc(strlen(prgset->cpusetdfileprefix)	+ strlen(node->contid)+1))) {
		contp[0] = '\0';   // ensures the memory is an empty string
		// copy to new prefix
		contp = strcat(strcat(contp,prgset->cpusetdfileprefix), node->contid);

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

	// pre-compute affinity
	struct bitmask * cset = numa_allocate_cpumask();

	if (0 <= node->param->rscs->affinity) {
		// CPU affinity defined to one CPU? set!
		(void)numa_bitmask_clearall(cset);
		(void)numa_bitmask_setbit(cset, node->param->rscs->affinity);
	}
	else
		// affinity < 0 = CPU affinity to all enabled CPU's
		copy_bitmask_to_bitmask(prgset->affinity_mask, cset);


	if (!node->psig)
		node->psig = node->param->psig;

	if (!node->contid && node->param->cont){
		node->contid = node->param->cont->contid;
		warn("Container search resulted in empty container ID!");
	}

	// change to consider multiple PIDs with different affinity,
	// however.. each PID should have it's OWN container -> Concept

	// update CGroup setting of container if in CGROUP mode
	// save if not successful
	if (DM_CGRP == prgset->use_cgroup) {
		if (0 <= (node->param->rscs->affinity))
			node->status |= !(setContainerAffinity(node, cset)) & MSK_STATUPD;
	}
	else
		node->status |= !(setPidAffinity(node, cset)) & MSK_STATUPD;

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
	if (!(node->status & MSK_STATUPD)){
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
						contp[0] = '\0';   // ensures the memory is an empty string
						// copy to new prefix
						contp = strcat(strcat(contp,set->cpusetdfileprefix),dir->d_name);

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
						contp[0] = '\0';   // ensures the memory is an empty string
						// copy to new prefix
						contp = strcat(strcat(contp,set->cpusetdfileprefix),dir->d_name);

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
 * resetRTthrottle : reset RT throttle system-wide (works only with present tasks)
 *
 * Arguments: - configuration parameter structure
 *
 * Return value: -
 */
void
resetRTthrottle (prgset_t *set){
	char * value;	// pointer to value to write
	char buf[10];	// temporary stack buffer

	// all modes except  Dynamic, set to -1 = unconstrained
	if (SM_DYNSYSTEM != set->sched_mode){
		cont( "Set real-time bandwidth limit to (unconstrained)..");
		value = RTLIM_UNL;
	}

	// in Dynamic System Schedule, limit to 95% of period (a limit is requirement of G-EDF)
	else{
		cont( "Set real-time bandwidth limit to 95%..");
		// on error use default
		value = RTLIM_DEF;

		// read period
		if (0 > getkernvar(set->procfileprefix, "sched_rt_period_us", buf, sizeof(buf)))
			warn("Could not read throttle limit. Use default" RTLIM_DEF ".");
		else{
			// compute 95% of period
			long period = atol(value);
			if (period){
				(void)sprintf(buf, "%ld", period*100/95);
				value = buf;
			}
		}
	}

	// set bandwidth and throttle control to value/unconstrained
	if (0 > setkernvar(set->procfileprefix, "sched_rt_runtime_us", value, set->dryrun)){
		warn("RT-throttle still enabled. Limitations apply.");
	}
	else
		set->status |= MSK_STATTRTL;
}

