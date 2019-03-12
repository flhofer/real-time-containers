#include "pidlist.h" // memory structure to store information

// maybe changed in a second moment to kernel linked lists

void push_t(node_t * head, pid_t pid, char * psig) {
    node_t * current = head;
    while (current->next != NULL) {
        current = current->next;
    }

    /* now we can add a new variable */
    current->next = calloc(sizeof(node_t), 1);
    current->next->pid = pid;
    current->next->psig= psig;
    current->next->next = NULL;
}

void push(node_t ** head, pid_t pid, char * psig) {
    node_t * new_node;
    new_node = calloc(sizeof(node_t), 1);

    new_node->pid = pid;
    new_node->psig = psig;
    new_node->next = *head;
    *head = new_node;
	//return &new_node->attr;
}

void insert_after(node_t ** head, node_t ** prev, pid_t pid, char * psig) {
	if (*prev == NULL) {
		push (head, pid, psig);
		*prev = *head;
		return;
	}
   	node_t * new_node;
    new_node = calloc(sizeof(node_t), 1);

    new_node->pid = pid;
    new_node->psig = psig;
    new_node->next = (*prev)->next;
    (*prev)->next = new_node;
	*prev = (*prev)->next;
	//return &new_node->attr;
}

pid_t pop(node_t ** head) {
    pid_t retval = -1;
    node_t * next_node = NULL;

    if (*head == NULL) {
        return -1;
    }

    next_node = (*head)->next;
    retval = (*head)->pid;
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
    next_node = (*prev)->next;
    (*prev)->next = next_node->next;
    retval = (*prev)->pid;
    free(next_node);
    *prev = next_node;

    return retval;
}

/*
pid_t remove_last(node_t * head) {
    pid_t retval = 0;
    /* if there is only one item in the list, remove it */
/*    if (head->next == NULL) {
        retval = head->pid;
        free(head);
        return retval;
    }

    /* get to the second to last node in the list */
/*    node_t * current = head;
    while (current->next->next != NULL) {
        current = current->next;
    }

    /* now current points to the second to last item of the list, so let's remove current->next */
/*    retval = current->next->pid;
    free(current->next);
    current->next = NULL;
    return retval;

}

pid_t remove_by_index(node_t ** head, int n) {
    int i = 0;
    pid_t retval = -1;
    node_t * current = *head;
    node_t * temp_node = NULL;

    if (n == 0) {
        return pop(head);
    }

    for (i = 0; i < n-1; i++) {
        if (current->next == NULL) {
            return -1;
        }
        current = current->next;
    }

    temp_node = current->next;
    retval = temp_node->pid;
    current->next = temp_node->next;
    free(temp_node);

    return retval;
}

int remove_by_value(node_t ** head, pid_t pid) {
    node_t *previous, *current;

    if (*head == NULL) {
        return -1;
    }

    if ((*head)->pid == pid) {
        return pop(head);
    }

    previous = current = (*head)->next;
    while (current) {
        if (current->pid == pid) {
            previous->next = current->next;
            free(current);
            return pid;
        }

        previous = current;
        current  = current->next;
    }
    return -1;
} */

// scroll trrough array

struct sched_attr * get_node(node_t * act) {

    if (act == NULL) {
        return NULL;
    }

    return &act->attr;
}

void get_next(node_t ** act) {

    if (*act != NULL) {
	    *act = (*act)->next;
//        return NULL;
    }

 /*   if (*act == NULL) {
        return NULL;
    }
    return get_node(*act);*/
}

