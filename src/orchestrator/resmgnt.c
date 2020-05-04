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

/// setPidRlimit(): sets given resource limits
///
/// Arguments: - PID node
///			   - soft resource limit
///			   - hard resource limit
///			   - resource limit tag
///			   - resource limit string-name for prints
///
/// Return value: ---
static inline void setPidRlimit(pid_t pid, int32_t rls, int32_t rlh, int32_t type, char* name ) {

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

/// setPidResources(): set PID resources at first detection
/// Arguments: - pointer to PID item (node_t)
///
/// Return value: --
///
void setPidResources(node_t * node) {

	struct bitmask * cset = numa_allocate_cpumask();

	// parameters unassigned
	if (!prgset->quiet)
		(void)printf("\n");
	if (node->pid)
		info("new PID in list %d", node->pid);
	else if (node->contid)
		info("new container in list '%s'", node->contid);
	else
		warn("SetPidResources: Container not specified");

	if (!node_findParams(node, contparm)) { // parameter set found in list -> assign and update
		// pre-compute affinity
		if (0 <= node->param->rscs->affinity) {
			// CPU affinity defined to one CPU? set!
			(void)numa_bitmask_clearall(cset);
			(void)numa_bitmask_setbit(cset, node->param->rscs->affinity & ~(SCHED_FAFMSK)); // TODO: ?? FAFMSK
		}
		else
			// CPU affinity to all enabled CPU's
			copy_bitmask_to_bitmask(prgset->affinity_mask, cset);


		if (!node->psig)
			node->psig = node->param->psig;
		// TODO: fix once containers are managed properly
		if (!node->contid && node->param->cont)
			node->contid = node->param->cont->contid;

		// TODO: track failed scheduling update?

		// TODO: change to consider multiple PIDs with different affinity
		// update CGroup setting of container if in CGROUP mode
		if (DM_CGRP == prgset->use_cgroup) {
			if (0 <= (node->param->rscs->affinity)) {

				char *contp = NULL;
				char affinity[11];
				(void)sprintf(affinity, "%d", node->param->rscs->affinity & ~(SCHED_FAFMSK));

				cont( "reassigning %.12s's CGroups CPU's to %s", node->contid, affinity);
				if ((contp=malloc(strlen(prgset->cpusetdfileprefix)	+ strlen(node->contid)+1))) {
					contp[0] = '\0';   // ensures the memory is an empty string
					// copy to new prefix
					contp = strcat(strcat(contp,prgset->cpusetdfileprefix), node->contid);

					if (0 > setkernvar(contp, "/cpuset.cpus", affinity, prgset->dryrun)){
						warn("Can not set CPU-affinity");
					}
				}
				else
					warn("malloc failed!");

				free (contp);
			}
		}
		// should it be else??
		else {

			// add PID to docker CGroup
			char pid[6]; // PID is 5 digits + \0
			(void)sprintf(pid, "%d", node->pid);

			if (0 > setkernvar(prgset->cpusetdfileprefix , "tasks", pid, prgset->dryrun)){
				printDbg( "Warn! Can not move task %s\n", pid);
			}

			// Set affinity
			if (numa_sched_setaffinity(node->pid, cset))
				err_msg_n(errno,"setting affinity for PID %d",
					node->pid);
			else
				cont("PID %d reassigned to CPU%d", node->pid,
					node->param->rscs->affinity);
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
}
