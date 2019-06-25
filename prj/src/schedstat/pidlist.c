#include "pidlist.h" // memory structure to store information


static const node_t _node_default = { 0, NULL, NULL,	// pid, *psig, *contid
						{ 48, SCHED_NODATA }, 			// init size and scheduler 
						 { INT64_MAX, 0, INT64_MIN,		// statistics, max and min to min and max
						 0, 0, 0, 0, 0,
						 0, INT64_MAX, 0, INT64_MIN},
						NULL, NULL};					// *param, *next
						
static const struct sched_rscs _rscs_default = { -1, 
												-1, -1,	
												-1, -1};

void ppush(parm_t ** head) {
    parm_t * new_node;
    new_node = calloc(sizeof(parm_t), 1);
	// if any sched parameter is set, policy must also be set
	new_node->attr.sched_policy = SCHED_NODATA; // default for not set.
	new_node->rscs = _rscs_default;

    new_node->next = *head;
    *head = new_node;
}

void rpush(struct resTracer ** head) {
    struct resTracer * new_node = calloc(sizeof(struct resTracer), 1);

    new_node->next = *head;
    *head = new_node;
}

void push(node_t ** head, pid_t pid, char * psig, char * contid) {
    node_t * new_node = malloc(sizeof(node_t));
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

