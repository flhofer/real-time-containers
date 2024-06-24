/*
 * resmgmt.h
 *
 *  Created on: May 3, 2020
 *      Author: Florian Hofer
 */
#include "orchdata.h"	// memory structure to store information

#ifndef RESMGMT_H_
	#define RESMGMT_H_

	// Combining and or bit masks
	#define __numa_XXX_cpustring(a,b,c)	for (int i=0;i<a->size;i++)  \
										  if ((numa_bitmask_isbitset(a, i)) \
											c (numa_bitmask_isbitset(b, i))) \
											  numa_bitmask_setbit(b, i); \
										  else \
											  numa_bitmask_clearbit(b, i);

	#define numa_or_cpumask(from,to)	__numa_XXX_cpustring(from,to, || )
	#define numa_and_cpumask(from,to)	__numa_XXX_cpustring(from,to, && )

	void resetContCGroups(prgset_t *set, char * constr, char * numastr);
											// loop through present container and reset to default
	void setContCGroups(prgset_t *set, int setCont);
											// as above, but set to affinity
	int resetRTthrottle (prgset_t *set,
							int percent); 	// (re)set the system RT throttle setting (run-time percentage to -1)

	// WARN! node is assumed to be already locked!
	void setPidResources(node_t * node);	// set resources of PID in memory (new or update)
	void updatePidAttr(node_t * node);		// update PID scheduling attributes and set flags if needed
	void updatePidWCET(node_t * node, uint64_t wcet); // update WCET value to computed result
	void updatePidCmdline(node_t * node);	// update PID command line

	// resTracer functions for simple and adaptive schedule
	void createResTracer(); 					// create linked list elements for all CPU's
	int checkUvalue(struct resTracer * res,
		struct sched_attr * par, int add);		// check utilization value, does task fit?
	resTracer_t * checkPeriod(struct sched_attr
			* attr, int affinity);				// find a resTracer that fits best
	resTracer_t * checkPeriod_R(node_t * item, int include);
												// same, but with node for runtime
	resTracer_t * getTracer(int32_t CPUno);		// return resTracer for CPU no
	resTracer_t * grepTracer();					// return resTreacer with lowest Ul
	int	recomputeCPUTimes(int32_t CPUno);		// recompute UL for CPU
	int recomputeTimes(struct resTracer * res);	// recompute UL for CPU using Trace
	int	setPidAffinityAssinged (node_t * node);	// update PID affinity in run-time

	uint64_t findPeriodMatch(uint64_t cdf_Period);	// find matching period in 1/40ths

	// runtime manipulation of configuration and PID nodes - MUTEX must be acquired
	int findPidParameters(node_t* node, containers_t * conts);

#endif /* RESMGMT_H_ */
