#ifndef __DOCKERLINK_H_
	#define __DOCKERLINK_H_
	
	#include <stdint.h>
	#include <pthread.h>

	enum cont_events { cnt_add, cnt_remove, cnt_pending };

	typedef struct cont_event {
		int event;			// enum cont_events
		char * name;		// name-tag of the container
		char * id;			// id of the container
		char * image;		// image id or tag (docker chooses)
		uint64_t timenano;	// timestamp in nanoseconds
	} contevent_t;

	extern pthread_mutex_t containerMutex; // data access mutex
	extern contevent_t * containerEvent; // data

	void *thread_watch_docker(void *arg);

#endif

