#include "dockerlink.h"

#include <stdbool.h>		// for bool definition and operation
#include <json-c/json.h>	// libjson-c for parsing
#include <errno.h>			// error numbers and strings
#include <signal.h> 		// for SIGs, handling in main, raise in update

#include "kernutil.h"	// used for custom pipes
#include "error.h"		// error and stderr print functions

#define USEC_PER_SEC		1000000
#define NSEC_PER_SEC		1000000000
#define TIMER_RELTIME		0
#define INTERV_RFSH			1000

// TODO: implement rt signal interface
// TODO: /proc/sys/kernel/rtsig-max /proc/sys/kernel/rtsig-nr
// RLIMIT_SIGPENDING in 2.6.8
// implemented since 2.2
// http://man7.org/linux/man-pages/man7/signal.7.html 
// waring -> interrupt with EINTR

#define PFX "[dockerlink] "
#define JSON_FILE_BUF_SIZE 4096
#define DEFAULT_MEM_BUF_SIZE (4 * 1024 * 1024)

#ifndef TRUE
	#define TRUE true
	#define FALSE false
#endif

#include "parse_func.h"

// signal to keep status of triggers ext SIG
volatile sig_atomic_t stop;

/// stphand(): interrupt handler for infinite while loop, help 
/// this function is called from outside, interrupt handling routine
/// Arguments: - signal number of interrupt calling
///
/// Return value: -
//TODO: check function with static
static void stphand (int sig, siginfo_t *siginfo, void *context){
	stop = 1;
}

/// tsnorm(): verifies timespec for boundaries + fixes it
///
/// Arguments: pointer to timespec to check
///
/// Return value: -
static inline void tsnorm(struct timespec *ts)
{
	while (ts->tv_nsec >= NSEC_PER_SEC) {
		ts->tv_nsec -= NSEC_PER_SEC;
		ts->tv_sec++;
	}
}

FILE * inpipe;
pthread_mutex_t containerMutex; // data access mutex
contevent_t * containerEvent; // data
	
struct eventData {
	char * type;
	char * status;
	char * name;
	char * id;
	char * from;
	char * scope;
	uint64_t timenano;
};

// possible docker events, v 1.18
enum dockerEvents {
    dkrevnt_attach,
    dkrevnt_commit,
    dkrevnt_copy,
    dkrevnt_create,
    dkrevnt_destroy,
    dkrevnt_detach,
    dkrevnt_die,
    dkrevnt_exec_create,
    dkrevnt_exec_detach,
    dkrevnt_exec_die,
    dkrevnt_exec_start,
    dkrevnt_export,
    dkrevnt_health_status,
    dkrevnt_kill,
    dkrevnt_oom,
    dkrevnt_pause,
    dkrevnt_rename,
    dkrevnt_resize,
    dkrevnt_restart,
    dkrevnt_start,
    dkrevnt_stop,
    dkrevnt_top,
    dkrevnt_unpause,
    dkrevnt_update
	};

/// docker_read_pipe(): read from pipe and parse JSON
///
/// Arguments: event structure to fill with data from JSON
///
/// Return value: (void)
static int docker_read_pipe(struct eventData * evnt){

	char buf[JSON_FILE_BUF_SIZE];
	struct json_object *root;

	buf[0] = '\0';

	// repeat loop for reading until process ends or interrupt
	// is called
	while (!(stop) && !feof(inpipe)){

		// read buffer until timeout
		char * got = fgets(buf, JSON_FILE_BUF_SIZE, inpipe);

		// buf read successfully? // TODO: fix check. seems odd
		if ((!got) || '\0' == buf[0]) {
			warn(PFX "Empty JSON buffer");
			continue;
		}

		root = json_tokener_parse(buf);

		// root read successfully?
		if (NULL == root) {
			warn(PFX "Empty JSON");
//			pthread_exit(0);
			exit(EXIT_INV_CONFIG);
		}

		evnt->type = get_string_value_from(root, "Type", FALSE, NULL);
		if (!strcmp(evnt->type, "container")) {
			evnt->status = get_string_value_from(root, "status", FALSE, NULL);
			evnt->id = get_string_value_from(root, "id", FALSE, NULL);
			evnt->from = get_string_value_from(root, "from", FALSE, NULL);
			struct json_object *actor, *attrib;

			actor = get_in_object(root, "Actor", TRUE);
			if (actor) {
				// not status type, ID is here
	//			if (!evnt->id)
	//				evnt->id = get_string_value_from(actor, "ID", FALSE, NULL);
				attrib = get_in_object(actor, "Attributes", TRUE);
				if (attrib)
					evnt->name = get_string_value_from(attrib, "name", FALSE, NULL);

				json_object_put(attrib);
				json_object_put(actor);
			}
		}
		evnt->scope = get_string_value_from(root, "scope", FALSE, NULL);
		evnt->timenano = get_int64_value_from(root, "timeNano", FALSE, 0);
		json_object_put(root); // free object
		return 1;
	}

	// reading stopped. no new element
	return 0;
}

/// docker_check_event(): call pipe read and parse response
///
/// Arguments: - 
///
/// Return value: pointer to valid container event
static contevent_t * docker_check_event() {

	struct eventData evnt;
	memset(&evnt, 0, sizeof(struct eventData)); // set all pointers to NULL -> init

	// read next element from pipe
	if (!docker_read_pipe(&evnt))
		return NULL; // return if empty

	// parse element
	contevent_t * cntevent = NULL; // return element, default - null

	if (evnt.type && !strcmp(evnt.type, "container")){
		// kill	
		if ((!strcmp(evnt.status, "kill")))
		{
			cntevent = malloc(sizeof(contevent_t));
			
			cntevent->event = cnt_remove;
			cntevent->name = strdup(evnt.name);
			cntevent->id = strdup(evnt.id);
			cntevent->image = strdup(evnt.from);
			cntevent->timenano = evnt.timenano;
			return cntevent;
		}
		if ((!strcmp(evnt.status, "create")) ||
			(!strcmp(evnt.status, "start")))
		{
			cntevent = malloc(sizeof(contevent_t));
			
			cntevent->event = cnt_add;
			cntevent->name = strdup(evnt.name);
			cntevent->id = strdup(evnt.id);
			cntevent->image = strdup(evnt.from);
			cntevent->timenano = evnt.timenano;
		}
	}

	// free elements
	free(evnt.type);
	free(evnt.status);
	free(evnt.id);
	free(evnt.from);
	free(evnt.scope);
	// free main

	return cntevent;
}

/// thread_watch_docker(): checks for docker events and signals new containers
///
/// Arguments: - 
///
/// Return value: pointer to void (PID exits with error if needed)
void *thread_watch_docker(void *arg) {

	int pstate = 0;
	pid_t pid;
	char * pcmd;
	contevent_t * cntevent;

	// TODO: block not used signals
	{ // setup interrupt handler block
		struct sigaction act;
		memset (&act, '\0', sizeof(act));
	 
		/* Use the sa_sigaction field because the handles has two additional parameters */
		act.sa_sigaction = &stphand;
	 
		/* The SA_SIGINFO flag tells sigaction() to use the sa_sigaction field, not sa_handler. */
		act.sa_flags = SA_SIGINFO;
	 
		if (sigaction(SIGHUP, &act, NULL) < 0) { // INT signal, stop from main prg
			perror ("Setup of sigaction failed");  
			exit(EXIT_FAILURE); // exit the software, not working
		}
	} // END interrupt handler block
	
	if (NULL != arg)
		pcmd = (char *)arg;
	else
		pcmd = "docker events --format '{{json .}}'"; // CONSTANT!

	int ret;
	struct timespec intervaltv;

	// get clock, use it as a future reference for update time TIMER_ABS*
	ret = clock_gettime(CLOCK_MONOTONIC, &intervaltv);
	if (0 != ret) {
		if (EINTR != ret)
			warn("clock_gettime() failed: %s", strerror(errno));
		pstate=4;
	}

	while (1) {
		switch (pstate) {

			case 0: 
				if (!(inpipe = popen2 (pcmd, "r", &pid)))
					err_exit_n(errno, "Pipe process open failed!");
				pstate = 1;
				printDbg(PFX "Reading JSON output from pipe...\n");
				// no break

			case 1:
				if (feof(inpipe))
					pstate = 4;
				else if ((cntevent = docker_check_event()))  // new event?
					pstate = 2;
				break;

			case 2:
				// put new event to 
				(void)pthread_mutex_lock(&containerMutex);
				
				if (!containerEvent)
					pstate = 3;
				else
					(void)pthread_mutex_unlock(&containerMutex);
				break;

			case 3:
				containerEvent = cntevent;
				(void)pthread_mutex_unlock(&containerMutex);
				cntevent = NULL;
				pstate = 1;
				break;

			case 4:
				if (inpipe)
					pclose2(inpipe, pid, SIGHUP);
				pthread_exit(0); // exit the thread signaling normal return
				break;
		}

		if (3 > pstate && stop){ // if 3 wait for change, lock is hold
			pstate=4;
		}
/*		else if (1 == pstate) {
	        // abs-time relative interval shift

	        // calculate next execution intervall
	        intervaltv.tv_sec += INTERV_RFSH / USEC_PER_SEC;
	        intervaltv.tv_nsec+= (INTERV_RFSH % USEC_PER_SEC) * 1000;
	        tsnorm(&intervaltv);

	        // sleep for interval nanoseconds
	        ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &intervaltv, NULL);
	        if (0 != ret) {
	                // Set warning only.. shouldn't stop working
	                // probably overrun, restarts immediately in attempt to catch up
	                if (EINTR != ret) {
	                        warn("clock_nanosleep() failed. errno: %s",strerror (ret));
	                }
	        }
        }	*/	
	}
}

