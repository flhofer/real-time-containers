/*
 * adaptive.c
 *
 *  Created on: Feb 10, 2020
 *      Author: Florian Hofer
 */

#include "adaptive.h"

// standard includes
#include <numa.h>			// Numa node identification
#include <linux/sched.h>
#include <sched.h>

// Custom includes
#include "orchestrator.h"	// Definitions of global variables
#include "error.h"			// error and std error print functions
#include "cmnutil.h"		// common definitions and functions
#include "resmgnt.h"		// resource management for PIDs and Containers

#undef PFX
#define PFX "[adapt] "

/*
 * 	LOCAL definitions and variables
 */

typedef struct resAlloc { 		// resource allocations mapping
	struct resAlloc *	next;		//
	struct cont_parm *	item; 		// default, could be any element (img, cont, pid)
	struct resTracer *	assigned;	// null = no, pointer is resTracer assigned to
} resAlloc_t;

resAlloc_t * aHead = NULL;

/*
 *  cmpPidItemP(): compares two resource allocation attributes for Qsort, descending
 *  				Criterion by period
 *
 *  Arguments: pointers to the items to check
 *
 *  Return value: difference
 */
static int
cmpPidItemP (const void * a, const void * b) {
	int64_t diff = ((int64_t)((resAlloc_t *)b)->item->attr->sched_period
			- (int64_t)((resAlloc_t *)a)->item->attr->sched_period);
	if (!diff)
		return (int)((int64_t)(((resAlloc_t *)b)->item->attr->sched_runtime
				- (int64_t)((resAlloc_t *)a)->item->attr->sched_runtime)  % INT32_MAX);
	return (int)(diff % INT32_MAX); // reduce but keep sign
}

/*
 *  cmpPidItemU(): compares two resource allocation attributes for Qsort,
 *  			   descending by period first then Utilization
 *
 *  Arguments: pointers to the items to check
 *
 *  Return value: difference
 */
static int
cmpPidItemU (const void * a, const void * b) {
	// order by period first
	if ((((resAlloc_t *)a)->item->attr->sched_period) !=
		 (((resAlloc_t *)b)->item->attr->sched_period))
		return cmpPidItemP (a, b);


	// if both are 0, return bigger runtime item
	if (!((resAlloc_t *)a)->item->attr->sched_period
			&& !((resAlloc_t *)b)->item->attr->sched_period)
		return ((resAlloc_t *)b)->item->attr->sched_runtime
		- ((resAlloc_t *)a)->item->attr->sched_runtime;

	// if one period 0, return other as bigger
	if (!((resAlloc_t *)a)->item->attr->sched_period)
			return 1;
	if (!((resAlloc_t *)b)->item->attr->sched_period)
			return -1;

	// both periods are present, use Utilization value
	double U1 = ((double)((resAlloc_t *)a)->item->attr->sched_runtime /
			(double)((resAlloc_t *)a)->item->attr->sched_period);
	double U2 = ((double)((resAlloc_t *)b)->item->attr->sched_runtime /
			(double)((resAlloc_t *)b)->item->attr->sched_period);
	return (U2-U1)*10000;
}

/*
 *  cmpPidItemS(): compares two resource allocation attributes for Qsort,
 *  			   descending by scheduler type, then period then U
 *
 *  Arguments: pointers to the items to check
 *
 *  Return value: difference
 */
static int
cmpPidItemS (const void * a, const void * b) {
	// order by period first-utilization
	if (((resAlloc_t *)a)->item->attr->sched_period
		|| ((resAlloc_t *)b)->item->attr->sched_period
		|| ((resAlloc_t *)a)->item->attr->sched_runtime
		|| ((resAlloc_t *)b)->item->attr->sched_runtime)
		return cmpPidItemU (a, b);

	// no parameters known, group by scheduler (order not important)
	return ((resAlloc_t *)a)->item->attr->sched_policy
		-  ((resAlloc_t *)b)->item->attr->sched_policy;
}

/*
 *  recomputeTimes(): recomputes base and utilization factor of a resource
 *
 *  Arguments:  - resource entry for this CPU
 *
 *  Return value: Negative values return error
 */
static int
recomputeTimes_S(struct resTracer * res) {

	struct resTracer * resNew = calloc (1, sizeof(struct resTracer));

	int rv = 0;

	for (resAlloc_t * alloc = aHead; ((alloc)); alloc=alloc->next){
		if (alloc->assigned != res)
			continue;

		rv = MIN(checkUvalue(resNew, alloc->item->attr, 1), rv);
	}

	res->basePeriod = resNew->basePeriod;
	res->usedPeriod = resNew->usedPeriod;
	res->U = resNew->U;

	free(resNew);
	return rv;
}

/*
 *  addTracer(): append resource with mask
 *
 *  Arguments: - res is the resource reservation record
 * 			   - CPU, number to allocate, -1 for default (if weight 1)
 *
 *  Return value: error, or 0 if successful
 */
static int
addTracer(resAlloc_t * res, int cpu){
	for (resTracer_t * trc = rHead; ((trc)); trc=trc->next){
		if (((-1 == cpu)
			&& (numa_bitmask_isbitset(res->item->rscs->affinity_mask, trc->affinity)))
			|| (cpu == trc->affinity)){

			// check first. add and return check value
			int ret = checkUvalue(trc, res->item->attr, 1);
			if (0 > ret)
				warn(PFX "Utilization limit reached for CPU%d", trc->affinity);
			res->assigned = trc;
			return ret;
		}
	}
	return -1; // error! not found
}

/*
 *  addTracerFix(): append resource with mask for fixed values
 *
 *  Arguments: - res is the resource reservation record
 *
 *  Return value:
 */
static void
addTracerFix(resAlloc_t * res) {
	if (numa_bitmask_weight(res->item->rscs->affinity_mask) == 1){
		if (0 > addTracer(res, -1))
			err_exit("The resource plan does not fit your system!");
		else
			res->item->status |= MSK_STATCFIX;
	}
}


/*
 *  createAffinityMask():create affinity mask based on configuration
 *
 *  Arguments: - resource structure
 * 			   - dependent bit-mask (hierarchy upper)
 *
 *  Return value: returns the created resource info for hierarchical
 * 					matching and combining */
static void
createAffinityMask(rscs_t * rscs, struct bitmask* bDep){
	// Create CPU mask only if not shared (e.g. -1 = no fixed CPU affinity)
	if (rscs->affinity >= 0) {
		char  affstr[11];
		(void)sprintf(affstr, "%d", rscs->affinity);
		rscs->affinity_mask = numa_parse_cpustring_all(affstr);
	}
	else{
		rscs->affinity_mask = numa_allocate_cpumask();
		copy_bitmask_to_bitmask(prgset->affinity_mask, rscs->affinity_mask);
		if (bDep){
			numa_and_cpumask(bDep, rscs->affinity_mask);
		}
	}
}

/*
 *  pushResource(): append resource to resource task list with mask
 *
 *  Arguments: - item is container, image or pid
 * 				(they're equal for attr and rscs )
 * 			   - dependent bit-mask (hierarchy upper)
 *
 *  Return value: returns the created resource info for hierarchical
 * 					matching and combining
 */
static resAlloc_t *
pushResource(cont_t *item, struct bitmask* bDep){

	// add item
	push((void**)&aHead, sizeof (resAlloc_t));
	aHead->item = item;
	if (item->status & MSK_STATSHRC)
		return aHead;

	createAffinityMask (item->rscs, bDep);
	return aHead;
}

/* --- SOME FUNCS TO EASE READABILITY ----- */
static void
ddPids(pids_t * pids, resAlloc_t * parAlloc, struct bitmask * bm){
	resAlloc_t * rTmp;
	// PIDs mask update and fill
	for (;((pids)); pids=pids->next){

		rTmp = pushResource((cont_t*)pids->pid, parAlloc->item->rscs->affinity_mask);
		numa_or_cpumask(rTmp->item->rscs->affinity_mask,bm);

		// if fix assignment, add to tracer
		addTracerFix(rTmp);
	}
}

static void
ddConts(conts_t * conts, resAlloc_t * parAlloc, struct bitmask * bm){
	// depending containers
	for (; ((conts)); conts=conts->next){
		struct bitmask *bmPids = numa_allocate_cpumask();
		resAlloc_t * rCont;

		// add reserve container, keep
		rCont = pushResource(conts->cont, parAlloc->item->rscs->affinity_mask);

		ddPids(conts->cont->pids,rCont, bmPids);

		// update affinity values of container, keep only necessary
		numa_and_cpumask(bmPids,rCont->item->rscs->affinity_mask);

		// merge mask to image shared, free unused
		numa_or_cpumask(rCont->item->rscs->affinity_mask, bm);
		numa_free_cpumask(bmPids);

		// if fix assignment, add to tracer
		addTracerFix(rCont);
	}
}
/* --- END SOME FUNCS TO EASE READABILITY ----- */


/*
 *  adaptPrepareSchedule(): Prepare adaptive schedule computation
 *  	compute the resource allocation
 *
 *  Arguments: -
 *
 *  Return value: -
 */
void
adaptPrepareSchedule(){
	// create res tracer structures for all available data
	createResTracer();
	createAffinityMask(contparm->rscs, NULL);

	// transform all masks, starting from images
	for (img_t * img = contparm->img; ((img)); img=img->next ){
		struct bitmask * bmConts = numa_allocate_cpumask();
		resAlloc_t * rImg;

		// add reserve image, keep reference
		rImg = pushResource((cont_t *)img, NULL);

		// continue Cont->PIDs
		ddConts(img->conts, rImg, bmConts);
		// continue PIDs
		ddPids(img->pids,rImg, bmConts);

		// update affinity values of image, keep only necessary
		numa_and_cpumask(bmConts,rImg->item->rscs->affinity_mask);
		// free unused
		numa_free_cpumask(bmConts);

		// if fix assignment, add to tracer
		addTracerFix(rImg);
	}

	// transform all masks, solo containers
	for (cont_t * cont = contparm->cont; ((cont)); cont=cont->next){
		if (cont->img) // part of tree, skip
			continue;

		struct bitmask *bmPids = numa_allocate_cpumask();
		resAlloc_t * rCont;

		// add reserve container, keep
		rCont = pushResource(cont, NULL);

		// continue PIDs
		ddPids(cont->pids, rCont, bmPids);

		// update affinity values, keep only necessary
		numa_and_cpumask(bmPids,rCont->item->rscs->affinity_mask);

		//push container mask, and merge to image shared
		numa_free_cpumask(bmPids);

		// if fix assignment, add to tracer
		addTracerFix(rCont);
	}


	// transform all masks, PIDs
	for (pidc_t * pid = contparm->pids; ((pid)); pid=pid->next ){
		if (pid->img || pid->cont) // part of tree, skip
			continue;

		resAlloc_t * rTmp = pushResource((cont_t*)pid, NULL);

		// if fix assignment, add to tracer
		addTracerFix(rTmp);
	}
}

/*
 *  adaptPrepareSchedule(): Prepare adaptive schedule computation
 *  	compute the resource allocation, uses resoure masks only
 *
 *  Arguments: -
 *
 *  Return value: -
 */
void
adaptPlanSchedule(){

	// order by period and/or utilization
	qsortll((void **)&aHead, cmpPidItemS);

	int unmatched = 0; // count unmatched
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
					trc= checkPeriod(res->item->attr, res->item->rscs->affinity);
					if (trc){
						(void)checkUvalue(trc, res->item->attr, 1);
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
					trc= checkPeriod(res->item->attr, res->item->rscs->affinity);
					if (trc){
						(void)checkUvalue(trc, res->item->attr, 1);
						res->assigned = trc;
					}
					else
						warn("Could not assign a resource!");
				}
			}
		}
	} // END dedicated resources

	printDbg(PFX "After pre-compute, un-match count %d\n", unmatched);
	{ // compute flexible resources with undefined detail
		resTracer_t * FFtrc = NULL;
		resTracer_t * RRtrc = NULL;
		resTracer_t * BTtrc = NULL;
		resTracer_t * NTtrc = NULL;
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
						// NORMAL/IDLE/OTHER tasks can be floating and should not exist in general
					default:
						// OR undefined configuration, put in to a common tracer. Accompanying tasks
						if (!NTtrc)
							NTtrc = grepTracer();
						res-> assigned = NTtrc;
						break;
				}
			}
	} // END dedicated resources
}

/*
 *  adaptScramble(): Re-scramble resource distribution when some issues
 *  	with allocation showed up
 *
 *  Arguments: -
 *
 *  Return value: -
 */
void
adaptScramble(){

	resTracer_t * trc = NULL;

	for (resAlloc_t * res= aHead; ((res)); res=res->next){

		if (!(res->assigned) ||(res->item->status & MSK_STATCFIX))
				continue;

		trc = checkPeriod(res->item->attr, res->item->rscs->affinity);
		// found a better option?
		if (trc && (trc != res->assigned)){
			// recompute and add
			(void)checkUvalue(trc, res->item->attr, 1);
			resTracer_t * trcOld = res->assigned;

			res->assigned = trc;
			(void)recomputeTimes_S(trcOld); // reset times of old resource
		}
	}

}

/*
 *  adaptExecute(): Update actual schedule parameters of containers
 * 		with adaptive result computed at prepare
 *
 *  Arguments: -
 *
 *  Return value: -
 */
void
adaptExecute() {
	// apply only if not shared and if fixed cpu is assigned
	for (resAlloc_t * res = aHead; ((res)); res=res->next)
		if (!(res->item->status & MSK_STATSHRC) && (res->assigned))
			res->item->rscs->affinity = res->assigned->affinity;
}

/*
 *  adaptFreeTracer(): free resources
 *
 *  Arguments: -
 *
 *  Return value: -
 */
void
adaptFree(){
	while (aHead){
		pop((void**)&aHead);
	}
}
