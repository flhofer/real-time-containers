#include "orchdata.h" // memory structure to store information
// TODO: FIXME: need return value to deal with memory allocation problems

/* -------------------- COMMON, SHARED functions ----------------------*/

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

// TODO: this consumes a ton of stack. convert to array, use other alg, than back to ll
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

static inline void duplicateContainer(node_t* node, struct containers * conts, cont_t ** cont) {
	push((void**)&conts->cont, sizeof(cont_t));
	// copy contents but skip first pointer, PID-list can be referenced -> forking, add only
	(void)memcpy((void*)conts->cont + sizeof(cont_t *), (void*)*cont + sizeof(cont_t *), 
		sizeof(cont_t) - sizeof(cont_t *));
	// update pointer to newly updated, assign id
	*cont = conts->cont;
	(*cont)->contid = strdup(node->contid);

	free(node->psig); // clear entry to avoid confusion
	node->psig = NULL;
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
		// 12 is standard docker short signature
		if(img->imgid && node->imgid && !strncmp(img->imgid, node->imgid, 12)) {
			conts_t * imgcont = img->conts;	
			// check for container match
			while (NULL != imgcont) {
				if (imgcont->cont->contid && node->contid) {
					// 12 is standard docker short signature
					if  (!strncmp(imgcont->cont->contid, node->contid, 12)) {
						cont = imgcont->cont;
						break;
					}
					// if node pid = 0, psig is the name of the container coming from dockerlink
					else if (!(node->pid) && node->psig && !strcmp(imgcont->cont->contid, node->psig)) {
						cont = imgcont->cont;
						duplicateContainer(node, conts, &cont);
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
			// 12 is standard docker short signature
			if(cont->contid && node->contid) {
				if (!strncmp(cont->contid, node->contid, 12))
					break;

				// if node pid = 0, psig is the name of the container coming from dockerlink
				else if (!(node->pid) && node->psig && !strcmp(cont->contid, node->psig)) {
					duplicateContainer(node, conts, &cont);
					break;
				}
			}
			cont = cont->next; 
		}
	}

	// did we find a container match?
	if (img || cont) {
		// read all associated pids. Is it there?

		// TODO: if container has no settings, use image. Even just for one

		// assign pids from cont or img, depending whats found
		int useimg = (img && !cont);
		struct pids_parm * curr = (useimg) ? img->pids : cont->pids;

		while (NULL != curr) {
			if(curr->pid->psig && node->psig && strstr(node->psig, curr->pid->psig)) {
				// found a matching pid inc root container
				node->param = curr->pid;
				return 0;
			}
			curr = curr->next; 
		}

		// TODO: - Expand to double check also image.

		// found? if not, create entry
		printDbg("... parameters not found, creating from PID and assigning container settings\n");
		push((void**)&conts->pids, sizeof(pidc_t));
		if (useimg) {
			// add new items
			push((void**)&conts->cont, sizeof(cont_t));
			push((void**)&img->conts, sizeof(conts_t));
			img->conts->cont = conts->cont; 
			cont = conts->cont;
			cont->img = img;

			// assign values
			cont->contid = node->contid;
			cont->rscs = img->rscs;
			cont->attr = img->attr;
		}
		// add to container pids
		push((void**)&cont->pids, sizeof(pids_t));
		cont->pids->pid = conts->pids; // add new empty item -> pid list, container pids list
		conts->pids->rscs = cont->rscs;
		conts->pids->attr = cont->attr;

		node->param = conts->pids;
		node->param->img = img;
		node->param->cont = cont;
		node->psig = node->param->psig;
		// update counter
		conts->nthreads++;
		return 0;
	}
	else{ 
		// no match found. an now?
		printDbg("... container not found, trying PID scan\n");

		// TODO: if containerid is valid, create entry?

		// start from scratch in the pid config list only. Maybe ID is new?
		struct pidc_parm * curr = conts->pids;

		while (NULL != curr) {
			if(curr->psig && node->psig && strstr(node->psig, curr->psig)) {
				warn("assigning container configuration to unrelated PID");
				node->param = curr;
				return 0;
			}
			curr = curr->next; 
		}
	}
	printDbg("... PID not found. Ignoring\n");

	return -1;
	// TODO: what if NULL?
/*
	// didnt find it anywhere  -> plant an empty container and pidc
	push((void**)&conts->cont, sizeof(cont_t));
	conts->cont->contid = node->psig; // use program signature for container TODO: maybe use pid instead?
	conts->cont->rscs = conts->rscs;
	conts->cont->attr = conts->attr;

	push((void**)&conts->pids, sizeof(pidc_t));
	push((void**)&cont->pids, sizeof(pids_t));
	cont->pids->pid = conts->pids; // add new empty item -> pid list, container pids list

	conts->pids->rscs = conts->rscs;
	conts->pids->attr = conts->attr;
	node->param = conts->pids;

	// update counters
	conts->nthreads++;
	conts->num_cont++;		
	return -1;
*/
}

/* -------------------- default PID values structure ----------------------*/

static const node_t _node_default = { NULL,				// *next, 
						0, 0, NULL, NULL, NULL,			// PID, det_mode, *psig, *contid, *imgid
						{ 48, SCHED_NODATA }, 			// init size and scheduler 
						{ INT64_MAX, 0, INT64_MIN,		// statistics, max and min to min and max
						0, 0, 0, 0, 0,
						0, INT64_MAX, 0, INT64_MIN},
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
	// TODO: temporary for free hooks
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
	// TODO: configuration of PID and container maybe as well?

	pop((void**)head);
}

/* -------------------- END RUNTIME structure ----------------------*/
 