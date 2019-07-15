#include "orchdata.h" // memory structure to store information
// TODO: FIXME: neeed return value to deal with memory allocation problems

/* -------------------- cONFIGURATION structure ----------------------*/

/// pcpush(): adds a new PID configuration structure to the global l.l. and 
/// 			links it to the container associated
///
/// Arguments: - adr of head of the pid configuration linked list
///			   - adr of head of the conainer's pid association list
///
/// Return value: -
void pcpush(struct pidc_parm ** head, struct pids_parm ** phead){
    pidc_t * new_node;
    new_node = calloc(sizeof(pidc_t), 1);
	if (!new_node)
		err_exit("could not allocate memory!");

    new_node->next = *head;
    *head = new_node;

	// chain new element to container pids list, pointing to this pid
	pids_t *new_pnode = malloc (sizeof(pids_t));
	new_pnode->pid = *head;
	new_pnode->next = *phead;
	*phead = new_pnode;
}

/// cpush(): adds a new container configuration structure to the global l.l. 
///
/// Arguments: - adr of head of the container configuration linked list
///
/// Return value: -
void cpush(cont_t ** head) {
    cont_t * new_node;
    new_node = calloc(sizeof(cont_t), 1);
	if (!new_node)
		err_exit("could not allocate memory!");

    new_node->next = *head;
    *head = new_node;
}

/// findParams(): assigns the PID parameters list of a running container
//
/// Arguments: - node to chek for matching parameters
/// 		   - pid configuration list head
///
/// Return value: 0 if successful, -1 if unsuccessful
///
int findParams(node_t* node, struct containers * conts){

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
				node->param = curr;
				return 0;
			}
			curr = curr->next; 
		}

		// found? if not, create entry
		printDbg("... parameters not found, creating empty from PID\n");
		pcpush(&conts->pids, &cont->pids); // add new empty item -> pid list, container pids list
		node->param = conts->pids;				
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

	return -1;
}

/* -------------------- Old CONFIG structure ----------------------*/

static const node_t _node_default = { 0, NULL, NULL,	// pid, *psig, *contid
						{ 48, SCHED_NODATA }, 			// init size and scheduler 
						 { INT64_MAX, 0, INT64_MIN,		// statistics, max and min to min and max
						 0, 0, 0, 0, 0,
						 0, INT64_MAX, 0, INT64_MIN},
						NULL, NULL};					// *param, *next
						

/* -------------------- RESOURCE tracing structure ----------------------*/

void rpush(struct resTracer ** head) {
    struct resTracer * new_node = calloc(sizeof(struct resTracer), 1);
	if (!new_node)
		err_exit("could not allocate memory!");

    new_node->next = *head;
    *head = new_node;
}

/* -------------------- RUNTIME structure ----------------------*/

void push(node_t ** head, pid_t pid, char * psig, char * contid) {
    node_t * new_node = malloc(sizeof(node_t));
	if (!new_node)
		err_exit("could not allocate memory!");

	*new_node = _node_default;

    new_node->pid = pid;
    new_node->psig = psig;
    new_node->contid = contid;
    new_node->next = *head;
    *head = new_node;
}

void insert_after(node_t ** head, node_t ** prev, pid_t pid, char * psig, char * contid) {
	if (*prev == NULL) {
		push (head, pid, psig, contid);
		*prev = *head; // shift to new
		return;
	}
   	node_t * new_node = malloc(sizeof(node_t));
	if (!new_node)
		err_exit("could not allocate memory!");
	
	*new_node = _node_default;

    new_node->pid = pid;
    new_node->psig = psig;
    new_node->contid = contid;
    new_node->next = (*prev)->next;
    (*prev)->next = new_node;
	*prev = (*prev)->next; // shift to new
}

static void check_free(node_t * node) {
	// verify if we have to free things (pointing outside param
	if ((long)node->psig < (long)node->param || // is it inside the param structure?? if not, free
		(long)node->psig > (long)node->param + sizeof(parm_t))
		free(node->psig);

	if ((long)node->contid < (long)node->param || // is it inside the param structure?? if not, free
		(long)node->contid > (long)node->param + sizeof(parm_t))
		free(node->contid);
}

pid_t pop(node_t ** head) {
    pid_t retval = -1;
    node_t * next_node = NULL;

    if (*head == NULL) {
        return -1;
    }

    next_node = (*head)->next;
    retval = (*head)->pid;
 
	check_free(*head);
	free(*head);
    *head = next_node;

    return retval;
}

pid_t drop_after(node_t ** head, node_t ** prev) {
	// special case, drop head, has no prec
	if (*prev == NULL) {
		return pop (head);
	}

    pid_t retval = -1;
    node_t * next_node = NULL;

	// next node is the node to be dropped
	if (NULL !=  (*prev)->next) {
		next_node =  (*prev)->next->next;

	    retval = (*prev)->next->pid;
		check_free((*prev)->next);
	    free((*prev)->next);
	}
	
	(*prev)->next = next_node;
    retval = (*prev)->pid;

    return retval;
}

/* -------------------- END RUNTIME structure ----------------------*/
