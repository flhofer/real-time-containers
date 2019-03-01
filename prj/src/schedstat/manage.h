
#ifndef __MANAGE_H_
#define __MANAGE_H_

extern pthread_mutex_t dataMutex;
extern node_t * head;

void *thread_manage (void *arg); // thread that scans peridically for new entry pids

#endif
