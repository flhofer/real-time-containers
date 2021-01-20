#include "orchdata.h"	// memory structure to store information
#include "cmnutil.h"	// general definitions
#include <numa.h>		// for numa free cpu-mask
#include <time.h>		// time management and constants

static void freeParm(cont_t * item);

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

/*
 *  duplicateContainer(): duplicate container configuration with data based on names
 *  						data from DockerLink
 *
 *  Arguments: - Node with data from docker_link
 *  		   - Configuration structure
 *  		   - Pointer to newly created container
 *
 *	Return: -
 */
static void
duplicateContainer(node_t* dlNode, struct containers * containers, cont_t ** cont) {
	push((void**)&containers->cont, sizeof(cont_t));
	// copy contents but skip first pointer, PID-list can be referenced -> forking, add only
	(void)memcpy((void*)containers->cont + sizeof(cont_t *), (void*)*cont + sizeof(cont_t *), 
		sizeof(cont_t) - sizeof(cont_t *));
	// update pointer to newly updated, assign id
	*cont = containers->cont;
	(*cont)->contid = strdup(dlNode->contid);

	free(dlNode->psig); // clear entry to avoid confusion
	dlNode->psig = NULL;
}

/*
 *  refreshContainers() : move PID data from unrecognized container to
 * 						DockerLink passed data, refresh PIDs (happens sometimes)
 *
 *  Arguments: - Configuration structure
 *
 *  Return: -
 */
static void
refreshContainers(containers_t * containers) {

	// NOTE, the first container is the new entry, we use address of ->next as holder
	for (cont_t * cont = containers->cont; (cont->next); cont=cont->next){
		// container id present, task found before info
		if (!strncmp(containers->cont->contid, cont->next->contid,
				MIN(strlen(containers->cont->contid), strlen(cont->next->contid)))){

			// Receive container list from old container
			containers->cont->pids = cont->next->pids;
			// remove old container
			freeParm(cont->next);
			pop((void**)&cont->next);

			// update connected PIDs
			for (pids_t * pids = containers->cont->pids; (pids) ; pids=pids->next){
				//set update flag
				pids->pid->status &= ~MSK_STATUPD;
				//update container link
				pids->pid->cont = containers->cont;
			}

			break;
		}
	}
}


/// node_findParams(): assigns the PID parameters list of a running container
///
/// Arguments: - node to chek for matching parameters
/// 		   - pid configuration list head
///
/// Return value: 0 if successful, -1 if unsuccessful
///
int node_findParams(node_t* node, struct containers * conts){

	struct img_parm * img = conts->img;
	struct cont_parm * cont = NULL;
	// check for image match first
	while (NULL != img) {

		if(img->imgid && node->imgid && !strncmp(img->imgid, node->imgid
				, MIN(strlen(img->imgid), strlen(node->imgid)))) {
			conts_t * imgcont = img->conts;	
			printDbg("Image match %s\n", img->imgid);
			// check for container match
			while (NULL != imgcont) {
				if (imgcont->cont->contid && node->contid) {

					if  (!strncmp(imgcont->cont->contid, node->contid,
							MIN(strlen(imgcont->cont->contid), strlen(node->contid)))
							&& ((node->pid) || !(imgcont->cont->status & MSK_STATCCRT))) {
						cont = imgcont->cont;
						break;
					}
					// if node pid = 0, psig is the name of the container coming from dockerlink
					else if (!(node->pid) && node->psig && !strcmp(imgcont->cont->contid, node->psig)) {
						cont = imgcont->cont;
						duplicateContainer(node, conts, &cont);
						refreshContainers(conts); // refresh prematurely added containers
						break;
					}
				}
				imgcont = imgcont->next; 
			}
			break; // if imgid is found, keep trace in img -> default if nothing else found
		}
		img = img->next; 
	}

	// we might have found the image, but still 
	// not in the images, check all containers
	if (!cont) {
		cont = conts->cont;

		// check for container match
		while (NULL != cont) {

			if(cont->contid && node->contid) {
				if (!strncmp(cont->contid, node->contid,
						MIN(strlen(cont->contid), strlen(node->contid)))
						&& ((node->pid) || !(cont->status & MSK_STATCCRT)))
					break;

				// if node pid = 0, psig is the name of the container coming from dockerlink
				else if (!(node->pid) && node->psig && !strcmp(cont->contid, node->psig)) {
					duplicateContainer(node, conts, &cont);
					refreshContainers(conts); // refresh prematurely added containers
					break;
				}
			}
			cont = cont->next; 
		}
	}

	// did we find a container or image match?
	if (img || cont) {
		// read all associated PIDs. Is it there?

		// assign pids from cont or img, depending what is found
		int useimg = (img && !cont);
		struct pids_parm * curr = (useimg) ? img->pids : cont->pids;

		// check the first result
		while (NULL != curr) {
			if(curr->pid->psig && node->psig && strstr(node->psig, curr->pid->psig)) {
				// found a matching pid inc root container
				node->param = curr->pid;
				return 0;
			}
			curr = curr->next;
		}

		// if both were found, check again in image
		if (img && cont){
			curr = img->pids;
			while (NULL != curr) {
				if(curr->pid->psig && node->psig && strstr(node->psig, curr->pid->psig)) {
					// found a matching pid inc root container
					node->param = curr->pid;
					return 0;
				}
				curr = curr->next;
			}
		}

		// found? if not, create PID parameter entry
		printDbg("... parameters not found, creating from PID and assigning container settings\n");
		push((void**)&conts->pids, sizeof(pidc_t));
		if (useimg) {
			// add new container
			push((void**)&conts->cont, sizeof(cont_t));
			push((void**)&img->conts, sizeof(conts_t));
			img->conts->cont = conts->cont; 
			cont = conts->cont;
			cont->img = img;

			// assign values
			cont->contid = strdup(node->contid);
			img->status |= MSK_STATSHAT | MSK_STATSHRC;
			cont->rscs = img->rscs;
			cont->attr = img->attr;
		}
		// add new PID to container PIDs
		push((void**)&cont->pids, sizeof(pids_t));
		cont->pids->pid = conts->pids; // add new empty item -> pid list, container pids list
		cont->status |= MSK_STATSHAT | MSK_STATSHRC;
		conts->pids->rscs = cont->rscs;
		conts->pids->attr = cont->attr;

		// assing configuration to node
		node->param = conts->pids;
		node->param->img = img;
		node->param->cont = cont;
		node->psig = node->param->psig;
		// update counter
		conts->nthreads++;
		return 0;
	}
	else{ 
		// no match found. and now?
		printDbg("... container not found, trying PID scan\n");

		// start from scratch in the PID config list only. Maybe Container ID is new
		struct pidc_parm * curr = conts->pids;

		while (NULL != curr) {
			if(curr->psig && node->psig && strstr(node->psig, curr->psig)
				&& !(curr->cont) && !(curr->img) ) { // only unasociated items
				warn("assigning configuration to unrelated PID");
				node->param = curr; // TODO: duplicate PIDC
				break;
			}
			curr = curr->next; 
		}

		if (!node->contid){
			// no containerid, can't do anything
			printDbg("... PID not found. Ignoring\n");
			return -1;
		}

		// add new container
		push((void**)&conts->cont, sizeof(cont_t));
		cont = conts->cont;

		// assign values
		cont->contid = strdup(node->contid);
		cont->status |= MSK_STATCCRT; // (created at runtime from node)
		cont->rscs = conts->rscs;
		cont->attr = conts->attr;

		if (!curr){
			// found? if not, create PID parameter entry
			printDbg("... parameters not found, creating from PID and assigning container settings\n");
			// create new pidconfig
			push((void**)&conts->pids, sizeof(pidc_t));
			curr = conts->pids;

			curr->psig = strdup(node->psig);
			cont->status |= MSK_STATSHAT | MSK_STATSHRC;
			curr->rscs = cont->rscs;
			curr->attr = cont->attr;


			// add new PID to container PIDs
			push((void**)&cont->pids, sizeof(pids_t));
			cont->pids->pid = curr; // add new empty item -> pid list, container pids list
			cont->status |= MSK_STATSHAT | MSK_STATSHRC;
			curr->rscs = cont->rscs;
			curr->attr = cont->attr;

			node->param = curr;
		}
		else {
			// found use it's values
			node->psig = node->param->psig;
		}
		// pidconfig curr gets container config cont
		curr->cont = cont;
		// update counter
		conts->nthreads++;
	}

	return -1;
}

/* -------------------- special for Param structures --------------------- */

static void
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
		pop ((void**)&contparm->img);
	}

	while (contparm->cont){
		while (contparm->cont->pids)
			pop ((void**)&contparm->cont->pids);
		freeParm (contparm->cont);
		pop((void**)&contparm->cont);
	}

	while (contparm->pids){
		freeParm ((cont_t*)contparm->pids);
		pop ((void**)&contparm->pids);
	}

	free(contparm->attr);
	contparm->attr = NULL;
	if (contparm->rscs){
		numa_free_cpumask(contparm->rscs->affinity_mask);
		contparm->rscs->affinity_mask = NULL;
	}
	free(contparm->rscs);
	contparm->rscs = NULL;
	free(contparm);
}

void
freePrgSet(prgset_t * prgset){

	numa_bitmask_free(prgset->affinity_mask);
	free(prgset->logdir);
	free(prgset->logbasename);

	// signatures and folders
	free(prgset->cont_ppidc);
	free(prgset->cont_pidc);
	free(prgset->cont_cgrp);

	// filepaths virtual file system
	free(prgset->procfileprefix);
	free(prgset->cpusetfileprefix);
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
	while (*rHead)
		pop((void**)rHead);
}


/* -------------------- default PID values structure ----------------------*/

static const node_t _node_default = { NULL,				// *next, 
						0, 0, NULL, NULL, NULL,			// PID, status, *psig, *contid, *imgid
						{ 48, SCHED_NODATA }, 			// init size and scheduler 
						{ 								// statistics, max and min to min and max
							INT64_MAX, 0, INT64_MIN,	//		rt min/avg/max
							0, 0,						//		last ts, deadline
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
	if (!((*head)->param) || (((*head)->param->cont) 
		&& (*head)->contid != (*head)->param->cont->contid))
#ifdef DEBUG
	{
		free((*head)->contid);
		(*head)->contid = NULL;		
	}
#else
		free((*head)->contid);
#endif
	if (!((*head)->param) || (((*head)->param->img) 
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
 
