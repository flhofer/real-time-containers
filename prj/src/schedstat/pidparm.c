#include "pidparm.h" // memory structure to store parameter settings

static const struct sched_rscs _rscs_default = { -1, 
												-1, -1,	
												-1, -1};

// maybe changed in a second moment to kernel linked lists

void ppush(parm_t ** head) {
    parm_t * new_node;
    new_node = calloc(sizeof(parm_t), 1);
	// if any sched parameter is set, policy must also be set
	new_node->attr.sched_policy = SCHED_NODATA; // default for not set.
	new_node->rscs = _rscs_default;

    new_node->next = *head;
    *head = new_node;
}

struct sched_attr * pget_node(parm_t * act) {

    if (act == NULL) {
        return NULL;
    }

    return &act->attr;
}

struct sched_attr * pget_next(parm_t ** act) {

    if (*act == NULL) {
        return NULL;
    }

    *act = (*act)->next;
    if (*act == NULL) {
        return NULL;
    }
    return pget_node(*act);
}

