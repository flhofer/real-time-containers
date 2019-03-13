#include "pidparm.h"

extern pthread_mutex_t dataMutex;
extern node_t * head;

void *thread_update (void *arg); // thread that verifies status and allocates new threads

typedef struct pid_info {
	pid_t pid;
	char * psig; 
} pidinfo_t;
