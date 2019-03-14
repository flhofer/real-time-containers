
#ifndef __MANAGE_H_
#define __MANAGE_H_

#include "jsmn.h"
#include "errno.h"


extern pthread_mutex_t dataMutex;
extern node_t * head;

void *thread_manage (void *arg); // thread that scans peridically for new entry pids

char *policyname(uint32_t policy); // from no to name

#define JSMN_STRICT // force json conformance when parsing

#endif
