/*
 * adaptive.c
 *
 *  Created on: Feb 10, 2020
 *      Author: Florian Hofer
 */

#include "adaptive.h"

// Custom includes
#include "orchestrator.h"
#include "error.h"			// error and std error print functions
#include "cmnutil.h"	// common definitions and functions
#include "resmgmt.h"	// resource management for PIDs and Containers

// Things that should be needed only here
#include <numa.h>			// Numa node identification
#include <linux/sched.h>
#include <sched.h>

// TODO: standardize printing
#undef PFX
#define PFX "[adapt] "


typedef struct resAlloc { 		// resource allocations mapping
	struct resAlloc *	next;		//
	struct bitmask * 	affinity;	// computed affinity candidates
	struct cont_parm *	item; 		// default
	struct resTracer *	assigned;	// null = no, pointer is resTracer assigned to
	int					readOnly;	// do not update resources = shared values
} resAlloc_t;

static resAlloc_t * aHead = NULL;

static struct resTracer * rHead;

#define CHKNUISBETTER 1	// new CPU if available better than perfect match?
#define MAX_UL 0.90
#define SCHED_UKNLOAD	10 		// 10% load extra per task
#define SCHED_RRTONATTR	1000000 // conversion factor from sched_rr_timeslice_ms to sched_attr

/// cmpresItem(): compares two resource allocation items for Qsort, ascending
///
/// Arguments: pointers to the items to check
///
/// Return value: difference
static int cmpPidItem (const void * a, const void * b) {
	int64_t diff = ((int64_t)((resAlloc_t *)a)->item->attr->sched_period
			- (int64_t)((resAlloc_t *)b)->item->attr->sched_period);
	if (!diff)
		return (int)((int64_t)(((resAlloc_t *)a)->item->attr->sched_runtime
				- (int64_t)((resAlloc_t *)b)->item->attr->sched_runtime)  % INT32_MAX);
	return (int)(diff % INT32_MAX); // reduce but keep sign
}

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

/// createResTracer(): create resource tracing memory elements
/// 				   set them to default value
//
/// Arguments:
///
/// Return value:
///
static void createResTracer(){

	// backwards, cpu0 on top, we assume affinity_mask ok
	for (int i=(prgset->affinity_mask->size); i >= 0;i--)

		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by selected only
			push((void**)&rHead, sizeof(struct resTracer));
			rHead->affinity = i;
			rHead->U = 0.0;
			rHead->basePeriod = 0;
	}
}

/// checkUvalue(): verify if task fits into Utilization limits of a resource
///
/// Arguments:  - resource entry for this CPU
///				- the `attr` structure of the task we are trying to fit in
///				- add or test, 0 = test, 1 = add to resources // TODO : variants, add-if
///
/// Return value: a matching score, higher is better. Negative values return error
///				  -1 = no space; -2 error // TODO: maybe use ERRNO?
///
static int checkUvalue(struct resTracer * res, struct sched_attr * par, int add) {
	uint64_t base = res->basePeriod;
	uint64_t used = res->usedPeriod;
	uint64_t basec = par->sched_period;
	int rv = 3; // perfect match -> all cases max value default

	// TODO: case areas are NOT a new scope
	// TODO: generic checks for 0 base, basec
	switch (par->sched_policy) {

	// TODO: if set to "default", SCHED_OTHER or SCHED_BATCH, how do I react?
	default:
		if (0 == base || 0 == par->sched_runtime){
			base = 1000000000; // default to 1 second)
			break;
		}
		// TODO : else fall through
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
				// TODO: warn or error if add is set?
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

		// TODO: maybe add subtraction instead of equal rv
		if (0 == par->sched_runtime){
			// can't do anything about computation
			rv = 0;
			used += used*SCHED_UKNLOAD/100; // add 10% to load, as a dummy value
			break;
		}

		int rvbonus = 1;

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

	/* TODO: review
	 *	RR return values for different situations
	 *	1000 .. perfect match desired repetition matches period of resources
	 *	   2 .. empty CPU, = perfect fit on new CPU
	 *     1 .. recalculation of period, but new is perfect fit to both
	 *     0 .. recompute needed
	 *  all with +1 bonus if runtime fits remaining UL
	 */
	case SCHED_RR:

		// TODO: maybe add subtraction instead of equal rv
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
		else // TODO: check that
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

/// checkPeriod(): find a resource that fits period
///
/// Arguments: the attr structure of the task
///
/// Return value: a pointer to the resource tracer
///					returns null if nothing is found
static resTracer_t * checkPeriod(cont_t * item) {
	resTracer_t * ftrc = NULL;
	int last = -2;		// last checked tracer's score, error by default
	float Ulast = 10;	// last checked traces's utilization rate
	int res;

	// loop through all and return the best fit
	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		res = checkUvalue(trc, item->attr, 0);
		if ((res > last) // better match, or matching favorite
			|| ((res == last) &&
				(  (trc->affinity == abs(item->rscs->affinity))
				|| (trc->U < Ulast)) ) )	{
			last = res;
			// reset U if we had an affinity match
			if (trc->affinity == abs(item->rscs->affinity))
				Ulast= 0.0;
			else
				Ulast = trc->U;
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
/// Arguments: - res is the resource reservation record
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
			int ret = checkUvalue(trc, res->item->attr, 1);
			if (-1 == ret)
				warn(PFX "Utilization limit reached for CPU%d", trc->affinity);
			res->assigned = trc;
			return ret;
		}
	}
	return -1; // error! not found
}


/// addTracerFix(): append resource with mask for fixed values
///
/// Arguments: - res is the resource reservation record
///
/// Return value:
///
static void addTracerFix(resAlloc_t * res) {
	if (numa_bitmask_weight(res->affinity) == 1)
		if (0 > addTracer(res, -1))
			err_exit("The resource plan does not fit your system!");
}

/// pushResource(): append resource to resource task list with mask
///
/// Arguments: - item is container, image or pid
///				(they're equal for attr and rscs )
///			   - depth 0 = image, 1 = container, 2 - pid
///
/// Return value: returns the created resource info for hierarchical
///					matching and combining
///
static resAlloc_t * pushResource(cont_t *item, struct bitmask* bDep, int depth){

	// add item
	push((void**)&aHead, sizeof (resAlloc_t));
	if (item->rscs->affinity > 0) {
		char  affstr[11];
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

				// if fix assignment, add to tracer
				addTracerFix(rTmp);
			}

			// update affinity values of container, keep only necessary
			numa_and_cpumask(bmPids,rCont->affinity);

			// merge mask to image shared, free unused
			numa_or_cpumask(rCont->affinity,bmConts);
			numa_free_cpumask(bmPids);

			// if fix assignment, add to tracer
			addTracerFix(rCont);
		}

		// depending PIDs
		for (pids_t * pids = img->pids; ((pids)); pids=pids->next){

			rTmp = pushResource((cont_t*)pids->pid, rImg->affinity, 1);
			numa_or_cpumask(rTmp->affinity,bmConts);

			// if fix assignment, add to tracer
			addTracerFix(rTmp);
		}

		// update affinity values of image, keep only necessary
		numa_and_cpumask(bmConts,rImg->affinity);
		// free unused
		numa_free_cpumask(bmConts);

		// if fix assignment, add to tracer
		addTracerFix(rImg);
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

			// if fix assignment, add to tracer
			addTracerFix(rTmp);
		}

		// update affinity values, keep only necessary
		numa_and_cpumask(bmPids,rCont->affinity);

		//push container mask, and merge to image shared
		numa_free_cpumask(bmPids);

		// if fix assignment, add to tracer
		addTracerFix(rCont);
	}

	// transform all masks, PIDs
	for (pidc_t * pid = contparm->pids; ((pid)); pid=pid->next ){
		if (pid->img || pid->cont) // part of tree, skip
			continue;

		resAlloc_t * rTmp = pushResource((cont_t*)pid, NULL, 2);

		// if fix assignment, add to tracer
		addTracerFix(rTmp);
	}

	// ################## from here use resource masks ##############

	// order by period and runtime
	qsortll((void **)&aHead, cmpPidItem);

	int unmatched = 0;
	{ // compute flexible resources for tasks with defined runtime and period (desired)
		resTracer_t * trc = NULL;
		for (resAlloc_t * res = aHead; ((res)); res=res->next){
			if (!res->assigned){
				// is a runtime defined? if not,..
				if (!res->item->attr->sched_runtime ||
						!res->item->attr->sched_period)
					unmatched++;
				else {
					// allocate resources for flexible tasks
					trc= checkPeriod(res->item);
					if (trc){
						checkUvalue(trc, res->item->attr, 1);
						res->assigned = trc;
					}
					else
						warn("Could not assign a resource!");
				}
			}
		}
	} // END dedicated resources

	{ // compute flexible resources with partially defined detail
		resTracer_t * trc = NULL;
		for (resAlloc_t * res = aHead; ((res)); res=res->next){
			if (!res->assigned){
				// is a runtime defined? if not,..
				if (!res->item->attr->sched_runtime)
					unmatched++;
				else {
					// allocate resources for flexible tasks
					trc= checkPeriod(res->item);
					if (trc){
						checkUvalue(trc, res->item->attr, 1);
						res->assigned = trc;
					}
					else
						warn("Could not assign a resource!");
				}
			}
		}
	} // END dedicated resources

	{ // compute flexible resources with undefined detail
		resTracer_t * FFtrc = NULL;
		resTracer_t * RRtrc = NULL;
		resTracer_t * BTtrc = NULL;
		for (resAlloc_t * res = aHead; ((res)); res=res->next){
			if (!res->assigned)
				switch (res->item->attr->sched_policy) {

					case SCHED_FIFO:
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
						// NORMAL/IDLE/OTHER tasks can be floating and should not exist in general
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

