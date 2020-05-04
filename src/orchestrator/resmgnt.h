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
											  numa_bitmask_setbit(b, i);

	#define numa_or_cpumask(from,to)	__numa_XXX_cpustring(from,to, || )
	#define numa_and_cpumask(from,to)	__numa_XXX_cpustring(from,to, && )

	void setPidResources(node_t * node);	// set resources of PID in memory (new or update)

#endif /* RESMGMT_H_ */
