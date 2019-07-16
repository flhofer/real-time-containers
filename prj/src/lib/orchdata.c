#include "orchdata.h" // memory structure to store information
// TODO: FIXME: neeed return value to deal with memory allocation problems

/* -------------------- COMMON, SHARED functions ----------------------*/

///	generic push pop, based on the fact that all structures have the next l.l
/// pointer right as first element

// -- dummy structure for generic push and pop functions
struct base {
	struct base * next; 
};

static void push(void ** head, size_t size) {
    struct base * new_node = calloc(size,1);
	if (!new_node)
		err_exit("could not allocate memory!");

    new_node->next = *head;
    *head = new_node;
}

static void pop(void** head) {
    if (NULL == *head) {
        return;
    }

    struct base * next_node = NULL;
    next_node = ((struct base *)*head)->next;
	free(*head);
    *head = next_node;
}

/* -------------------- CONFIGURATION structure ----------------------*/

/// pcpush(): adds a new PID configuration structure to the global l.l. and 
/// 			links it to the container associated
///
/// Arguments: - adr of head of the pid configuration linked list
///			   - adr of head of the conainer's pid association list
///
/// Return value: -
void pcpush(struct pidc_parm ** head, struct pids_parm ** phead){
	push((void**)head, sizeof(pidc_t));

	// chain new element to container pids list, pointing to this pid
	push((void**)phead, sizeof(pids_t));
	(*phead)->pid = *head;
}

/// cpush(): adds a new container configuration structure to the global l.l. 
///
/// Arguments: - adr of head of the container configuration linked list
///
/// Return value: -
void cpush(cont_t ** head) {
	push((void**)head, sizeof(cont_t));
}

/* -------------------- RESOURCE tracing structure ----------------------*/

/// rpush(): adds a new resource tracing structure to the l.l. 
///
/// Arguments: - adr of head of the linked list
///
/// Return value: -
void rpush(struct resTracer ** head) {
	push((void**)head, sizeof(struct resTracer));
}


/// findParams(): assigns the PID parameters list of a running container
//
/// Arguments: - node to chek for matching parameters
/// 		   - pid configuration list head
///
/// Return value: 0 if successful, -1 if unsuccessful
///
int node_findParams(node_t* node, struct containers * conts){

	struct cont_parm * cont = conts->cont;

	// check for container match
	while (NULL != cont) {
		// 12 is standard docker short signature
		if(cont->contid && node->contid && !strncmp(cont->contid, node->contid, 12)) {
			break;
		}
		cont = cont->next; 
	}

	// did we find a container match?
	if (cont) {
		// read all associated pids. Is it there?
		struct pids_parm * curr = cont->pids;

		while (NULL != curr) {
			if(curr->pid->psig && node->psig && !strcmp(curr->pid->psig, node->psig)) {
				// found a matching pid inc root container
				node->param = curr->pid;
				return 0;
			}
			curr = curr->next; 
		}

		// found? if not, create entry
		printDbg("... parameters not found, creating empty from PID\n");
		pcpush(&conts->pids, &cont->pids); // add new empty item -> pid list, container pids list
		node->param = conts->pids;
		// update counters
		conts->nthreads++;
		conts->num_cont++;		
	}
	else{ 
		// no match found. an now?
		printDbg("... container not found, trying PID scan\n");

		// TODO: if containerid is valid, create entry?

		// start from scatch in the pid config list only. Maybe ID is new?
		struct pidc_parm * curr = conts->pids;

		while (NULL != curr) {
			if(curr->psig && node->psig && !strcmp(curr->psig, node->psig)) {
				warn("assigning container configuration to unrelated PID");
				node->param = curr;
				return 0;
			}
			curr = curr->next; 
		}

	}

	// didnt find it anywhere :( leave it as it is
	return -1;
}

/* -------------------- default PID values structure ----------------------*/

static const node_t _node_default = { NULL,				// *next, 
						0, NULL, NULL,					// pid, *psig, *contid
						{ 48, SCHED_NODATA }, 			// init size and scheduler 
						 { INT64_MAX, 0, INT64_MIN,		// statistics, max and min to min and max
						 0, 0, 0, 0, 0,
						 0, INT64_MAX, 0, INT64_MIN},
						NULL};							// *param
						

/* -------------------- RUNTIME structure ----------------------*/

void node_push(node_t ** head, pid_t pid, char * psig, char * contid) {
	push((void**)head, sizeof(node_t));
	void* next = (*head)->next;

	**head = _node_default;
    (*head)->pid = pid;
    (*head)->psig = psig;
    (*head)->contid = contid;
    (*head)->next = next;
}

void node_insert_after(node_t ** head, node_t ** prev, pid_t pid, char * psig, char * contid) {
	if (*prev == NULL) {
		node_push (head, pid, psig, contid);
		*prev = *head; // shift to new
		return;
	}
	node_push (&(*prev)->next, pid, psig, contid);
}

static void node_check_free(node_t * node) {
	// verify if we have to free things (pointing outside param
	if ((long)node->psig < (long)node->param || // is it inside the param structure?? if not, free
		(long)node->psig > (long)node->param + sizeof(pidc_t))
		free(node->psig);

	if ((long)node->contid < (long)node->param->cont || // is it inside the param structure?? if not, free
		(long)node->contid > (long)node->param->cont + sizeof(cont_t))
		free(node->contid);
}

void node_pop(node_t ** head) {
    if (*head == NULL) {
        return;
    }
	node_check_free(*head);
	pop((void**)head);
}

void node_drop_after(node_t ** head, node_t ** prev) {
	// special case, drop head, has no prec
	if (*prev == NULL) {
		node_pop (head);
		return;
	}
	node_pop(&(*prev)->next);
}

/* -------------------- END RUNTIME structure ----------------------*/
