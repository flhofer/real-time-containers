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

	void resetContCGroups(prgset_t *const set, char * const constr, char * const numastr);
												// loop through present container and reset to default
	void setContCGroups(prgset_t * const set, int setCont);
												// as above, but set to affinity
	int resetRTthrottle (prgset_t * const set,
							int percent); 		// (re)set the system RT throttle setting (run-time percentage to -1)

	// WARN! node is assumed to be already locked!
	void setPidResources(node_t * const node);	// set resources of PID in memory (new or update)
	void updatePidAttr(node_t * const node);	// update PID scheduling attributes and set flags if needed
	void updatePidWCET(node_t * const node,
								uint64_t wcet); // update WCET value to computed result
	void updatePidCmdline(node_t * const node);	// update PID command line

	// resTracer functions for simple and adaptive schedule
	void createResTracer(); 					// create linked list elements for all CPU's
	int checkUvalue(struct resTracer * const res,
		struct sched_attr * const par, int add);		// check utilization value, does task fit?
	resTracer_t * checkPeriod(struct sched_attr	* const attr,
					int affinity, int CPU);		// find a resTracer that fits best
	resTracer_t * checkPeriod_R(node_t * const item, int include);
												// same, but with node for runtime
	resTracer_t * getTracer(int32_t CPUno);		// return resTracer for CPU no
	resTracer_t * grepTracer();					// return resTreacer with lowest Ul
	int	getTracerMainCPU(resTracer_t * const res);	// Return ID of main CPU of resTracer affinity
	int	recomputeCPUTimes(int32_t CPUno);		// recompute UL for CPU
	int recomputeTimes(struct resTracer * const res);	// recompute UL for CPU using Trace
	int	setPidAffinityAssinged (node_t * const node);	// update PID affinity in run-time
	int	getPidAffinityAssingedNr(node_t * const node);// get the number of CPUs that have an affinity with the PID

	uint64_t getPidPeriod(const node_t * const node);	// get configured period, or raw observed period as fallback
	uint64_t getPidPeriodMatch(const node_t * const node);
												// as above, match observed periods to standard values
	uint64_t findPeriodMatch(uint64_t cdf_Period);	// find matching period in 1/40ths

	// runtime manipulation of configuration and PID nodes - MUTEX must be acquired
	int findPidParameters(node_t* const node, containers_t * const conts);

#endif /* RESMGMT_H_ */
