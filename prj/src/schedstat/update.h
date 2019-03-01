
extern pthread_mutex_t dataMutex;
extern node_t * head;

void *thread_update (void *arg); // thread that verifies status and allocates new threads

void prepareEnvironment();

