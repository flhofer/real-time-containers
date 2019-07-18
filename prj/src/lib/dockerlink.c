#include "dockerlink.h"

static void docker_parse_events(struct json_object * js){

}

static void docker_read_pipe(){

	size_t in_length;
	char buf[JSON_FILE_BUF_SIZE];
	struct json_object *js;
	printDbg(PFX "Reading JSON output from pipe...\n");

	in_length = fread(buf, sizeof(char), JSON_FILE_BUF_SIZE, inpipe);
	buf[in_length] = '\0';
	js = json_tokener_parse(buf);
	docker_parse_events(js);

	json_object_put(js); // free object
}


/// thread_watch_docker(): checks for docker events and signals new containers
///
/// Arguments: - 
///
/// Return value: void (pid exits with error if needed)
void *thread_watch_docker(void *arg) {

	int pstate = 0;

	while (1) {
		switch (pstate) {

			case 0:
				break;

			case 1:
				pthread_exit(0); // exit the thread signalling normal return
				break;
		}
	}
}

