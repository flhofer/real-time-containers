/*
 * adaptive.c
 *
 *  Created on: Feb 10, 2020
 *      Author: Florian Hofer
 */

#include "adaptive.h"

// Custom includes
#include "orchestrator.h"


// ################### duplicates of manager.c simple template ################
#define MAX_UL 0.90
static struct resTracer * rhead;

// TODO: somehow does only checks.. :/

/// createResTracer(): create resource tracing memory elements
//
/// Arguments:
///
/// Return value: N/D - int
///
static int createResTracer(){

	for (int i=0;i<(prgset->affinity_mask->size);i++)

		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by selected only
			push((void**)&rhead, sizeof(struct resTracer));
			rhead->affinity = i;
			rhead->basePeriod = 1;
	}
	return 0;
}

/// checkUvalue(): verify if task fits into Utilization limits of a resource
///
/// Arguments: resource entry for this cpu, the attr structure of the task
///
/// Return value: 0 = ok, -1 = no space, 1 = ok but recalc base
static int checkUvalue(struct resTracer * res, struct sched_attr * par) {
	uint64_t base = res->basePeriod;
	uint64_t used = res->usedPeriod;
	int rv = 0;

	if (base % par->sched_deadline != 0) {
		// realign periods
		uint64_t max_Value = MAX (base, par->sched_period);

		if (base % 1000 != 0 || par->sched_period % 1000 != 0)
			fatal("Nanosecond resolution periods not supported!");
			// temporary solution to avoid very long loops

		while(1) //Always True
		{
			if(max_Value % base == 0 && max_Value % par->sched_period == 0)
			{
				break;
			}
			max_Value+= 1000; // add a microsecond..
		}

		used *= max_Value/base;
		base = max_Value;
		rv=1;
	}

	if (MAX_UL < (double)(used + par->sched_runtime * base/par->sched_period)/(double)(base) )
		rv = -1;

	return rv;
}

/// addUvalue(): verify if task fits into Utilization limits of a resource
///
/// Arguments: resource entry for this cpu, the attr structure of the task
///
/// Return value: -
static void addUvalue(struct resTracer * res, struct sched_attr * par) {

	if (res->basePeriod % par->sched_deadline != 0) {
		// realign periods
		uint64_t max_Value = MAX (res->basePeriod, par->sched_period);

		if (res->basePeriod % 1000 != 0 || par->sched_period % 1000 != 0)
			fatal("Nanosecond resolution periods not supported!");
			// temporary solution to avoid very long loops

		while(1) //Always True
		{
			if(max_Value % res->basePeriod == 0 && max_Value % par->sched_period == 0)
			{
				break;
			}
			max_Value+= 1000; // add a microsecond..
		}

		res->usedPeriod *= max_Value/res->basePeriod;
		res->basePeriod = max_Value;

	}

	res->usedPeriod += par->sched_runtime * res->basePeriod/par->sched_period;

}

// ################################################################################

/// checkPeriod(): find a resource that fits period
///
/// Arguments: the attr structure of the task
///
/// Return value: -
static int checkPeriod(struct sched_attr * par) {


}





/// adaptPrepareSchedule(): Prepare adaptive schedule computation
/// 	compute the resource allocation
///
/// Arguments: -
///
/// Return value: -
///
void adaptPrepareSchedule(){
	// create res tracer structures for all available data
	(void)createResTracer();

	// add all fixed resources
	for (cont_t * cont = contparm->cont; ((cont)); cont=cont->next ){

		// TODO: finish!

	}

	// compute flexible resources
	for (cont_t * cont = contparm->cont; ((cont)); cont=cont->next ){

		checkPeriod(cont->attr);

	}

}

/// adaptScramble(): Re-scramble resource distribution when some issues
/// 	with allocation showed up
///
/// Arguments: -
///
/// Return value: -
///
void adaptScramble(){
	// TODO: implement!
}

/// adaptExecute(): Update actual schedule parameters of containers
///		with adaptive result computed at prepare
///
/// Arguments: -
///
/// Return value: -
///
void adaptExecute() {
	// TODO: implement!
}

struct resTracer * adaptGetAllocations(){
	return rhead;
}

