#include "orchdata.h"	// memory structure to store information
#include "cmnutil.h"	// general definitions
#include <numa.h>		// for numa free cpu-mask
#include <time.h>		// time management and constants

/* -------------------- COMMON, SHARED functions ----------------------*/

// Programmable clock source values
int clocksources[] = {
	CLOCK_MONOTONIC,
	CLOCK_REALTIME,
	CLOCK_PROCESS_CPUTIME_ID,
	CLOCK_THREAD_CPUTIME_ID
};

///	generic push pop, based on the fact that all structures have the next l.l
/// pointer right as first element

// -- dummy structure for generic push and pop functions
struct base {
	struct base * next; 
};

void push(void ** head, size_t size) {
    struct base * new_node = calloc(1,size);
	if (!new_node)
		err_exit("could not allocate memory!");

    new_node->next = *head;
    *head = new_node;
}

void pop(void ** head) {
    if (NULL == *head) {
        return;
    }

    struct base * next_node = NULL;
    next_node = ((struct base *)*head)->next;
	free(*head);
    *head = next_node;
}

static struct base *getTail(struct base *cur) 
{ 
    while (cur != NULL && cur->next != NULL) 
        cur = cur->next; 
    return cur; 
} 

// Partitions the list taking the last element as the pivot 
static struct base *qsortll_partition(struct base *head, struct base *end, 
                       struct base **newHead, struct base **newEnd,
						int (*compar)(const void *, const void*)) 
{ 
    struct base *pivot = end; 
    struct base *prev = NULL, *cur = head, *tail = pivot; 
  
    // During partition, both the head and end of the list might change 
    // which is updated in the newHead and newEnd variables 
    while (cur != pivot) 
    { 
        if (compar(cur,pivot) < 0) 
        { 
            // First node that has a value less than the pivot - becomes 
            // the new head 
            if ((*newHead) == NULL) 
                (*newHead) = cur; 
  
            prev = cur;   
            cur = cur->next; 
        } 
        else // If cur node is greater than pivot 
        { 
            // Move cur node to next of tail, and change tail 
            if (prev) 
                prev->next = cur->next; 
            struct base *tmp = cur->next; 
            cur->next = NULL; 
            tail->next = cur; 
            tail = cur; 
            cur = tmp; 
        } 
    } 
  
    // If the pivot data is the smallest element in the current list, 
    // pivot becomes the head 
    if ((*newHead) == NULL) 
        (*newHead) = pivot; 
  
    // Update newEnd to the current last node 
    (*newEnd) = tail; 
  
    // Return the pivot node 
    return pivot; 
} 
  
  
//here the sorting happens exclusive of the end node 
struct base *qsortll_recur(struct base *head, struct base *end,
	int (*compar)(const void *, const void*)) 
{ 
    // base condition 
    if (!head || head == end) 
        return head; 
  
    struct base *newHead = NULL, *newEnd = NULL; 
  
    // Partition the list, newHead and newEnd will be updated 
    // by the partition function 
    struct base *pivot = qsortll_partition(head, end, &newHead, &newEnd, compar); 
  
    // If pivot is the smallest element - no need to recur for 
    // the left part. 
    if (newHead != pivot) 
    { 
        // Set the node before the pivot node as NULL 
        struct base *tmp = newHead; 
        while (tmp->next != pivot) 
            tmp = tmp->next; 
        tmp->next = NULL; 
  
        // Recur for the list before pivot 
        newHead = qsortll_recur(newHead, tmp, compar); 
  
        // Change next of last node of the left half to pivot 
        tmp = getTail(newHead); 
        tmp->next =  pivot; 
    } 
  
    // Recur for the list after the pivot element 
    pivot->next = qsortll_recur(pivot->next, newEnd, compar); 
  
    return newHead; 
} 
  
/// qsortll(): quick-sort for generic linked lists, uses a recursive compare function
///
/// Arguments: - address of head of the linked list
///			   - address to the comparison function to call
///
/// Return value: -
///
void qsortll(void **head, int (*compar)(const void *, const void*) ) 
{ 
	if ((head) && (*head) && (compar)) // check parameters are not null
		(*(struct base **)head) = qsortll_recur((struct base *)*head,
			 getTail((struct base *)*head), compar); 
}

/* -------------------- special for Param structures --------------------- */

void
copyResourceConfigC(cont_t * from, cont_t * to){
	// duplicate resources if needed
	to->status = from->status;
	if (!(to->status & MSK_STATSHAT)){
		to->attr = malloc(sizeof(struct sched_attr));
		(void)memcpy(to->attr, from->attr, sizeof(struct sched_attr));
	}
	else
		to->attr = from->attr;

	if (!(to->status & MSK_STATSHRC)){
		to->rscs = malloc(sizeof(struct sched_rscs));
		(void)memcpy(to->rscs, from->rscs, sizeof(struct sched_rscs));
		to->rscs->affinity_mask = numa_allocate_cpumask();
		if (from->rscs->affinity_mask){
			copy_bitmask_to_bitmask(from->rscs->affinity_mask, to->rscs->affinity_mask);
		}
	}
	else
		to->rscs = from->rscs;
}

void
copyResourceConfigP(pidc_t * from, pidc_t * to){
	copyResourceConfigC((cont_t*)from, (cont_t*) to);
}

void
freeParm(cont_t * item){

	if (!(item->status & MSK_STATSHAT))
#ifdef DEBUG
	{
		free(item->attr);
		item->attr = NULL;
	}
#else
		free(item->attr);
#endif

	if ((item->rscs) && !(item->status & MSK_STATSHRC)){
		if (item->rscs->affinity_mask)
			numa_free_cpumask(item->rscs->affinity_mask);
		free(item->rscs);
#ifdef DEBUG
		item->rscs = NULL;
#endif
	}
}

void
freeContParm(containers_t * contparm){
	// free resources!!
	while (contparm->img){
		while (contparm->img->conts)
			pop((void**)&contparm->img->conts);
		while (contparm->img->pids)
			pop ((void**)&contparm->img->pids);

		freeParm ((cont_t*)contparm->img);
		free(contparm->img->imgid);
		pop ((void**)&contparm->img);
	}

	while (contparm->cont){
		while (contparm->cont->pids)
			pop ((void**)&contparm->cont->pids);
		freeParm (contparm->cont);
		free(contparm->cont->contid);
		pop((void**)&contparm->cont);
	}

	while (contparm->pids){
		freeParm ((cont_t*)contparm->pids);
		free(contparm->pids->psig);
		pop ((void**)&contparm->pids);
	}

	free(contparm->attr);
	contparm->attr = NULL;
	if (contparm->rscs && contparm->rscs->affinity_mask){
		numa_free_cpumask(contparm->rscs->affinity_mask);
		contparm->rscs->affinity_mask = NULL;
	}
	free(contparm->rscs);
	contparm->rscs = NULL;
	free(contparm);
}

int
checkContParam(containers_t * contparm){

	int fix = 0;	// number of links fixed
	int fil = 0;	// number of inconsistent data (present both sides but differs)

	// check image consistency
	for (struct img_parm * img = contparm->img; (img); img = img->next){

		for (struct conts_parm * conts = img->conts; (conts); conts = conts->next){
			if (!conts->cont->img){
				printDbg(PFX "Unconnected image %.12s w/ container %.12s\n", img->imgid, conts->cont->contid);
				conts->cont->img = img;

				fix++;
			}
			if (conts->cont->img != img){
				printDbg(PFX "Inconsistent image %.12s x container %.12s\n", img->imgid, conts->cont->contid);
				fil++;
			}
		}

		for (struct pids_parm * pids = img->pids; (pids); pids = pids->next){
			if (!pids->pid->img){
				printDbg(PFX "Unconnected image %.12s w/ PID %s\n", img->imgid, pids->pid->psig);
				pids->pid->img = img;
				fix++;
			}
			if (pids->pid->img != img){
				printDbg(PFX "Inconsistent image %.12s x PID %s\n", img->imgid, pids->pid->psig);
				fil++;
			}
		}
	}

	// check container consistency
	for (struct cont_parm * cont = contparm->cont; (cont); cont = cont->next){

		if (cont->img){
			struct conts_parm * conts = cont->img->conts;
			for (; (conts); conts = conts->next)
				if (cont == conts->cont)
					break;

			if (!conts){
				printDbg(PFX "Unconnected container %.12s w/ image %.12s\n", cont->contid, cont->img->imgid);
				push((void**)&cont->img->conts, sizeof(conts_t));
				cont->img->conts->cont = cont;
				fix++;
			}
		}

		for (struct pids_parm * pids = cont->pids; (pids); pids = pids->next){
			if (!pids->pid->cont){
				printDbg(PFX "Unconnected container %.12s w/ PID %s\n", cont->contid, pids->pid->psig);
				pids->pid->cont = cont;
				fix++;
			}
			if (pids->pid->cont != cont){
				printDbg(PFX "Inconsistent container %.12s x PID %s\n", cont->contid, pids->pid->psig);
				fil++;
			}
		}
	}

	// check PID consistency
	for (struct pidc_parm * pid = contparm->pids; (pid); pid = pid->next){

		if (pid->img){
			struct pids_parm * pids = pid->img->pids;
			for (; (pids); pids = pids->next)
				if (pid == pids->pid)
					break;

			if (!pids){
				printDbg(PFX "Unconnected PID %s w/ image %.12s\n", pid->psig, pid->img->imgid);
				push((void**)&pid->img->pids, sizeof(pids_t));
				pid->img->pids->pid = pid;
				fix++;
			}
		}

		if (pid->cont){
			struct pids_parm * pids = pid->cont->pids;
			for (; (pids); pids = pids->next)
				if (pid == pids->pid)
					break;

			if (!pids){
				printDbg(PFX "Unconnected PID %s w/ container %.12s\n", pid->psig, pid->cont->contid);
				push((void**)&pid->cont->pids, sizeof(pids_t));
				pid->cont->pids->pid = pid;
				fix++;
			}
		}
	}

	if ((fix) || (fil))
		printDbg(PFX "WARNING: Fixed %d links and found %d corrupted links!\n", fix, fil);

	return (fil) ? -1 : fix;
}

void
freePrgSet(prgset_t * prgset){

	numa_bitmask_free(prgset->affinity_mask);
	free(prgset->affinity);
	free(prgset->logdir);
	free(prgset->logbasename);

	// signatures and folders
	free(prgset->cont_ppidc);
	free(prgset->cont_pidc);
	free(prgset->cont_cgrp);

	// filepaths virtual file system
	free(prgset->procfileprefix);
	free(prgset->cgroupfileprefix);
	free(prgset->cpusystemfileprefix);

	free(prgset->cpusetdfileprefix);

	free(prgset);
}

/*
 *  freeTracer(): free resources
 *
 *  Arguments: - head of resource tracer to free
 *
 *  Return value: -
 */
void
freeTracer(resTracer_t ** rHead){
	while (*rHead){
		numa_free_cpumask((*rHead)->affinity);
		pop((void**)rHead);
	}
}


/* -------------------- default PID values structure ----------------------*/

static const node_t _node_default = { NULL,				// *next, 
						0, 0, NULL, NULL, NULL,			// PID, status, *psig, *contid, *contid
						{ 48, SCHED_NODATA }, 			// init size and scheduler 
						{ 								// statistics, max and min to min and max
							INT64_MAX, 0, INT64_MIN,	//		rt min/avg/max
							0, 0, 0,					//		last ts, last ts period. deadline
							0, 0,						//		dl rf, dl diff
							0, 0, 0,					//		scan counters, fail, overrun
							INT64_MAX, 0, INT64_MIN,	// 		dl diff min/avg/max

							0, 0,						//		computed values histogram
							NULL, NULL,					// 		*pointer to fitting data for runtime
							NULL, NULL,					// 		*pointer to fitting data for period (NON_RT)
							-1, NULL, 0					//		assignment CPU, *assignment mask runtime, resched
						},
						NULL};							// *param structure pointer

/* -------------------- RUNTIME structure ----------------------*/

void node_push(node_t ** head) {
	push((void**)head, sizeof(node_t));
	// set default and get back
	void* next = (*head)->next;
	**head = _node_default;
    (*head)->next = next;
}

void node_pop(node_t ** head) {
    if (*head == NULL) {
        return;
    }

    // free strings id specifically created for this pid
	if (!((*head)->param) || (*head)->psig != (*head)->param->psig)
#ifdef DEBUG
	{
		free((*head)->psig);
		(*head)->psig = NULL;		
	}
#else
		free((*head)->psig);
#endif
	if (!((*head)->param) || (!((*head)->param->cont))
		|| (((*head)->param->cont)
		&& (*head)->contid != (*head)->param->cont->contid))
#ifdef DEBUG
	{
		free((*head)->contid);
		(*head)->contid = NULL;		
	}
#else
		free((*head)->contid);
#endif
	if (!((*head)->param) || (!((*head)->param->img))
		|| (((*head)->param->img)
		&& (*head)->imgid != (*head)->param->img->imgid))
#ifdef DEBUG
	{
		free((*head)->imgid);
		(*head)->imgid = NULL;		
	}
#else
		free((*head)->imgid);
#endif
	// curve fitting parameters runtime
	if ((*head)->mon.pdf_hist)
		runstats_histFree((*head)->mon.pdf_hist);
	if ((*head)->mon.pdf_cdf)
		runstats_cdfFree(&(*head)->mon.pdf_cdf);
	// curve fitting parameters period
	if ((*head)->mon.pdf_phist)
		runstats_histFree((*head)->mon.pdf_phist);
	if ((*head)->mon.pdf_pcdf)
		runstats_cdfFree(&(*head)->mon.pdf_pcdf);
	// runtime affinity mask
	if ((*head)->mon.assigned_mask)
		numa_bitmask_free((*head)->mon.assigned_mask);
#ifdef DEBUG
	(*head)->mon.pdf_hist = NULL;
	(*head)->mon.pdf_cdf = NULL;
	(*head)->mon.pdf_phist = NULL;
	(*head)->mon.pdf_pcdf = NULL;
	(*head)->mon.assigned_mask = NULL;
#endif

	pop((void**)head);
}

/* -------------------- END RUNTIME structure ----------------------*/
 
