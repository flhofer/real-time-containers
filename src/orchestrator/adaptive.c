/*
 * adaptive.c
 *
 *  Created on: Feb 10, 2020
 *      Author: Florian Hofer
 */

#include "adaptive.h"

// Custom includes
#include "orchestrator.h"
#include "error.h"		// error and std error print functions

// Things that should be needed only here
#include <numa.h>			// Numa node identification
#include <linux/sched.h>
#include <sched.h>

// TODO: standardize printing
#define PFX "[adapt] "
#define PFL "         "PFX
#define PIN PFX"    "
#define PIN2 PIN"    "
#define PIN3 PIN2"    "

typedef struct resAlloc { 		// resource allocations mapping
	struct resAlloc *	next;		//
	struct bitmask * 	affinity;	// computed affinity candidates
	struct cont_parm *	item; 		// default // TODO: all have rscs on the same edge!
	// TODO: maybe pointer to struct as well??
	struct resTracer *	assigned;	// null = no, pointer is restracer assigned to
	int					readOnly;	// do not update resources = shared values
} resAlloc_t;

static resAlloc_t * aHead = NULL;

// Combining and or bitmasks
// TODO: make universal!
#define __numa_XXX_cpustring(a,b,c)	for (int i=0;i<a->size;i++)  \
									  if ((numa_bitmask_isbitset(a, i)) \
										c (numa_bitmask_isbitset(b, i))) \
										  numa_bitmask_setbit(b, i);

#define numa_or_cpumask(from,to)	__numa_XXX_cpustring(from,to, || )
#define numa_and_cpumask(from,to)	__numa_XXX_cpustring(from,to, && )

#define CHKNUISBETTER 1	// new CPU if available better than perfect match?

/// gcd(): greatest common divisor, iterative
//
/// Arguments: - candidate values in uint64_t
///
/// Return value: - greatest value that fits both
///
static uint64_t gcd(uint64_t a, uint64_t b)
{
	uint64_t temp;
    while (b != 0)
    {
        temp = a % b;

        a = b;
        b = temp;
    }
    return a;
}

// ################### duplicates of manager.c simple template ################
#define MAX_UL 0.90
static struct resTracer * rHead;

// TODO: somehow does only checks.. :/

/// createResTracer(): create resource tracing memory elements
//
/// Arguments:
///
/// Return value: N/D - int
///
static int createResTracer(){

	// backwards, cpu0 on top, we assume affinity_mask ok
	for (int i=(prgset->affinity_mask->size); i >= 0;i--)

		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by selected only
			push((void**)&rHead, sizeof(struct resTracer));
			rHead->affinity = i;
			rHead->U = 0.0;
			rHead->basePeriod = 0;
	}
	return 0;
}

/// checkUvalue(): verify if task fits into Utilization limits of a resource
///
/// Arguments: resource entry for this CPU, the attr structure of the task
///
/// Return value: the matching level property. negative are problems
///				  higher is better
///				  3 = OK, perfect period match; 0 = OK, but recalculated base;
///				  2 = OK, but empty cpu
///				  1 = same but new period is an exact multiplier;
///				  -1 = no space; -2 error
static int checkUvalue(struct resTracer * res, struct sched_attr * par) {
	uint64_t base = res->basePeriod;
	uint64_t used = res->usedPeriod;
	int rv = 3; // perfect match

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
			// todo temporary solution to avoid very long loops
			return -2;
		}
		// recompute new values of resource tracer
		used *= new_base/res->basePeriod;
		base = new_base;

		// are the periods a perfect fit?
		if ((0 == res->basePeriod % par->sched_period)
			|| (0 == par->sched_period % res->basePeriod))
				rv = 2-CHKNUISBETTER;
		else
			rv =0;
	}

	used += par->sched_runtime * base/par->sched_period;
	if (MAX_UL < ((double)used/(double)base))
		rv = -1;

	return rv;
}

/// addUvalue(): verify if task fits into Utilization limits of a resource
///
/// Arguments: resource entry for this cpu, the attr structure of the task
///
/// Return value: -
static void addUvalue(struct resTracer * res, struct sched_attr * par) {

	// if unused, set to this period
	if (0==res->basePeriod)
		res->basePeriod = par->sched_period;

	if (res->basePeriod != par->sched_period) {
		// recompute base
		uint64_t new_base = gcd(res->basePeriod, par->sched_period);

		if (new_base % 1000 != 0)
			fatal("Nanosecond resolution periods not supported!");
			// todo temporary solution to avoid very long loops

		// recompute new values of tracer
		res->usedPeriod *= new_base/res->basePeriod;
		res->basePeriod = new_base;
	}

	res->usedPeriod += par->sched_runtime * res->basePeriod/par->sched_period;
	res->U = ((double)res->usedPeriod/(double)res->basePeriod);
}

// ################################################################################

/// checkPeriod(): find a resource that fits period
///
/// Arguments: the attr structure of the task
///
/// Return value: a pointer to the resource tracer
///					returns null if nothing is found
static resTracer_t * checkPeriod(cont_t * item) {
	resTracer_t * ftrc = NULL;
	int match = -2; // error by default
	int res;

	// loop through all and return the best fit
	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		res = checkUvalue(trc, item->attr);
		if ((res > match) // better match, or matching favorite
			|| ((res == match) && (trc->affinity == abs(item->rscs->affinity))) )	{
			match = res;
			ftrc = trc;
		}
	}
	return ftrc;
}

/// grepTracer(): find an empty or low UL CPU
///
/// Arguments: -
///
/// Return value: a pointer to the resource tracer
///					returns null if nothing is found
static resTracer_t * grepTracer() {
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

/// addTracer(): append resource with mask
///
/// Arguments: - item is container, image or pid
///			   - CPU, number to allocate, -1 for default (if weight 1)
///
/// Return value: error, or 0 if successful
///
static int addTracer(resAlloc_t * res, int cpu){
	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		if (((-1 == cpu)
			&& (numa_bitmask_isbitset(res->affinity, trc->affinity)))
			|| (cpu == trc->affinity)){

			// check first. add and return check value
			int ret = checkUvalue(trc, res->item->attr);
			if (-1 == ret)
				warn(PFX "Utilization limit reached for CPU%d", trc->affinity);
			addUvalue(trc, res->item->attr);
			res->assigned = trc;
			return ret;
		}
	}
	return -1; // error! not found
}


/// pushResource(): append resource to resource task list with mask
///
/// Arguments: - item is container, image or pid
///				(they're equal for attr and rscs // TODO: impl
///			   - depth 0 = image, 1 = container, 2 - pid
///
/// Return value: returns the created resource info for hierarchical
///					matching and combining
///
static resAlloc_t * pushResource(cont_t *item, struct bitmask* bDep, int depth){

	// add item
	push((void**)&aHead, sizeof (resAlloc_t));
	if (item->rscs->affinity > 0) {
		char  affstr[6];
		(void)sprintf(affstr, "%d", item->rscs->affinity);
		aHead->affinity = numa_parse_cpustring_all(affstr);
	}
	else{
		aHead->affinity = numa_allocate_cpumask();
		copy_bitmask_to_bitmask(prgset->affinity_mask, aHead->affinity);
		if (bDep)
			numa_and_cpumask(bDep, aHead->affinity);
	}

	aHead->item = item;

	// Set to read only if resources are copies of a parent
	aHead->readOnly = (item->rscs == contparm->rscs) // Check global
		|| ((depth > 0) && (item->img) // Check with image (Container & PID)
				&& (item->rscs == item->img->rscs))
		|| ((depth > 1) && (((pidc_t*)item)->cont) // Check with container (PID)
				&& (item->rscs == ((pidc_t*)item)->cont->rscs));

	return aHead;
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
	// TODO: return value!
	(void)createResTracer();

	// transform all masks, starting from images
	for (img_t * img = contparm->img; ((img)); img=img->next ){
		struct bitmask * bmConts = numa_allocate_cpumask();
		resAlloc_t * rTmp, * rImg;

		// add reserve image, keep reference
		rImg = pushResource((cont_t *)img, NULL, 0);

		// depending containers
		for (conts_t * conts = img->conts; ((conts)); conts=conts->next){
			struct bitmask *bmPids = numa_allocate_cpumask();
			resAlloc_t * rCont;

			// add reserve container, keep
			rCont = pushResource(conts->cont, rImg->affinity, 1);

			// container's PIDs
			for (pids_t * pids = conts->cont->pids; ((pids)); pids=pids->next){

				rTmp = pushResource((cont_t*)pids->pid, rCont->affinity, 2);
				numa_or_cpumask(rTmp->affinity,bmPids);
			}

			// update affinity values of container, keep only necessary
			numa_and_cpumask(bmPids,rCont->affinity);

			// merge mask to image shared, free unused
			numa_or_cpumask(rCont->affinity,bmConts);
			numa_free_cpumask(bmPids);
		}

		// depending PIDs
		for (pids_t * pids = img->pids; ((pids)); pids=pids->next){

			rTmp = pushResource((cont_t*)pids->pid, rImg->affinity, 1);
			numa_or_cpumask(rTmp->affinity,bmConts);
		}

		// update affinity values of image, keep only necessary
		numa_and_cpumask(bmConts,rImg->affinity);
		// free unused
		numa_free_cpumask(bmConts);
	}

	// transform all masks, solo containers
	for (cont_t * cont = contparm->cont; ((cont)); cont=cont->next ){
		if (cont->img) // part of tree, skip
			continue;

		struct bitmask *bmPids = numa_allocate_cpumask();
		resAlloc_t * rTmp, * rCont;

		// add reserve container, keep
		rCont = pushResource(cont, NULL, 1);

		// container's PIDs
		for (pids_t * pids = cont->pids; ((pids)); pids=pids->next){

			rTmp = pushResource((cont_t*)pids->pid, rCont->affinity, 2);
			numa_or_cpumask(rTmp->affinity,bmPids);
		}

		// update affinity values, keep only necessary
		numa_and_cpumask(bmPids,rCont->affinity);

		//push container mask, and merge to image shared
		numa_free_cpumask(bmPids);
	}

	// transform all masks, PIDs
	for (pidc_t * pid = contparm->pids; ((pid)); pid=pid->next ){
		if (pid->img || pid->cont) // part of tree, skip
			continue;

		(void)pushResource((cont_t*)pid, NULL, 2);
	}

	// ################## from here use resource masks ##############
	// add all fixed resources // TODO: push up to mask for efficiency
	for (resAlloc_t * res = aHead; ((res)); res=res->next)
		if (numa_bitmask_weight(res->affinity) == 1)
			if (addTracer(res, -1))
				err_exit("The resource plan does not fit your system!");

	{ // compute flexible resources
		resTracer_t * DLtrc = NULL;
		resTracer_t * FFtrc = NULL;
		resTracer_t * RRtrc = NULL;
		resTracer_t * BTtrc = NULL;
//		resTracer_t * NRtrc = NULL;
		for (resAlloc_t * res = aHead; ((res)); res=res->next){
			if (!res->assigned)
				switch (res->item->attr->sched_policy) {
					case SCHED_DEADLINE:
							// allocate resources for flexible tasks
							DLtrc= checkPeriod(res->item);
							if (DLtrc){
								addUvalue(DLtrc, res->item->attr);
								res->assigned = DLtrc;
							}
							else
								warn("Could not assign an resource!");
							break;
					case SCHED_FIFO: // TODO: allocation?
						// allocate FIFO tasks to
						if (!FFtrc)
							FFtrc = grepTracer();
						res->assigned = FFtrc;

						break;
					case SCHED_RR:
						// allocate RR tasks to dedicated CPU
						if (!RRtrc)
							RRtrc = grepTracer();
						res->assigned = RRtrc;

						break;

					case SCHED_BATCH:
						// allocate BATCH tasks to dedicated CPU
						if (!BTtrc)
							BTtrc = grepTracer();
						res->assigned = BTtrc;

						break;

					case SCHED_NORMAL:
	//				case SCHED_OTHER:
					case SCHED_IDLE:
					default:
						// NORMAL/IDLE/OTHER tasks can be floating
						break;
				}
			}
	} // END dedicated resources
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
	for (resAlloc_t * res = aHead; ((res)); res=res->next)
		if (!(res->readOnly) && (res->assigned))
			res->item->rscs->affinity = res->assigned->affinity;
}

/// adaptGetAllocations(): returns resource tracing info
///
/// Arguments: -
///
/// Return value: head of resource allocations
///
struct resTracer * adaptGetTracers(){
	return rHead;
}

void adaptFreeTracer(){
	while (aHead){
		numa_free_cpumask(aHead->affinity);
		pop((void**)&aHead);
	}

	while (rHead)
		pop((void**)&rHead);
}

