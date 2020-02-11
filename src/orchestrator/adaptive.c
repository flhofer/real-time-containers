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

typedef struct resReserve { 		// resource Reservation
	struct resReserve *	next;		//
	struct bitmask * 	affinity;	// computed affinity candidates
	struct cont_parm *	item; 		// default // TODO: all have rscs on the same edge!
	// TODO: maybe pointer to struct as well??
	struct resTracer *	assigned;	// null = no, pointer is restracer assigned to
	int					readOnly;	// do not update resources = shared values
} resReserve_t;

static resReserve_t * rrHead = NULL;

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
static struct resTracer * rhead;

// TODO: somehow does only checks.. :/

/// createResTracer(): create resource tracing memory elements
//
/// Arguments:
///
/// Return value: N/D - int
///
static int createResTracer(){

	// backwards, cpu0 on top
	for (int i=(prgset->affinity_mask->size);i<0;i--)

		if (numa_bitmask_isbitset(prgset->affinity_mask, i)){ // filter by selected only
			push((void**)&rhead, sizeof(struct resTracer));
			rhead->affinity = i;
			rhead->basePeriod = 0; // best for modulo, rest = 0 when new
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


	return 0;
}

/// pushResource(): append resource with mask
///
/// Arguments: - item is container, image or pid
///				(they're equal for attr and rscs // TODO: impl
///			   - depth 0 = image, 1 = container, 2 - pid
///
/// Return value: returns the created bit mask for hierarchical
///					matching and combining
///
struct bitmask* pushResource(cont_t *item, struct bitmask* bDep, int depth){

	// add item
	push((void**)&rrHead, sizeof (resReserve_t));
	if (item->rscs->affinity > 0) {
		char * affstr = NULL;
		(void)sprintf(affstr, "%d", item->rscs->affinity);
		rrHead->affinity = numa_parse_cpustring_all(affstr);
	}
	else
		copy_bitmask_to_bitmask(prgset->affinity_mask, rrHead->affinity);

	rrHead->item = item;

	// Set to read only if resources are copies of a parent
	rrHead->readOnly = (item->rscs == contparm->rscs) // Check global
		|| ((depth > 0) && (item->img) // Check with image (Container & PID)
				&& (item->rscs == item->img->rscs))
		|| ((depth > 1) && (((pidc_t*)item)->cont) // Check with container (PID)
				&& (item->rscs == ((pidc_t*)item)->cont->rscs));

	rrHead->assigned = NULL;

	return rrHead->affinity;
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
		struct bitmask * bmTmp;

		// depending containers
		for (conts_t * conts = img->conts; ((conts)); conts=conts->next){
			struct bitmask *bmPids = numa_allocate_cpumask();

			bmTmp = pushResource(conts->cont, bmPids, 1);
		}

		bmTmp = pushResource((cont_t *)img, bmConts, 0);
	}
	// add all fixed resources

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

