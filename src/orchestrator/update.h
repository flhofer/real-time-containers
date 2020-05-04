#ifndef UPDATE_H_
	#define UPDATE_H_

	// TODO: separate PID resource management from threads!
	void setPidResources(node_t * node);	// set resources of PID in memory (new or update)

	void *thread_update (void *arg); 		// thread that verifies status and allocates new threads

#endif
