#ifndef __DOCKERLINK_H_
	#define __DOCKERLINK_H_
	
	#include <stdint.h>
	#include <pthread.h>

	enum cont_events { cnt_add, cnt_remove, cnt_pending };

	typedef struct cont_event {
		int event;
		char * id;
		char * image;
		uint64_t timenano;
	} contevent_t;

	extern pthread_mutex_t containerMutex; // data access mutex
	extern contevent_t * containerEvent; // data

	void *thread_watch_docker(void *arg);

#endif

