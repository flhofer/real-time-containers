#include "pidparm.h" // memory structure to store parameter settings

void ppush_t(parm_t * head, pid_t pid) {
    parm_t * current = head;
    while (current->next != NULL) {
        current = current->next;
    }

    /* now we can add a new variable */
    current->next = calloc(sizeof(parm_t), 1);
//    current->next->pid = pid;
    current->next->next = NULL;
}

struct sched_attr * ppush(parm_t ** head, pid_t pid) {
    parm_t * new_node;
    new_node = calloc(sizeof(parm_t), 1);

//    new_node->pid = pid;
    new_node->next = *head;
    *head = new_node;
	return &new_node->attr;
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

