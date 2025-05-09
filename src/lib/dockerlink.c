#include "dockerlink.h"

#include <stdbool.h>		// for bool definition and operation
#include <json-c/json.h>	// libjson-c for parsing
#include <errno.h>			// error numbers and strings
#include <signal.h> 		// for SIGs, handling in main, raise in update

#include "kernutil.h"	// used for custom pipes
#include "error.h"		// error and stderr print functions
#include "cmnutil.h"	// common definitions and functions
#include "parse_func.h"


#define INTERV_RFSH			1000

#undef PFX
#define PFX "[dockerlink] "
#define JSON_FILE_BUF_SIZE 4096
#define DEFAULT_MEM_BUF_SIZE (4 * 1024 * 1024)

#define DOCKER_CMD_LINE "docker events --format '{{json .}}'"

int th_return = EXIT_SUCCESS;

// signal to keep status of triggers ext SIG
static volatile sig_atomic_t dlink_stop;

/// dlink_inthand(): interrupt handler for infinite while loop, help
/// this function is called from outside, interrupt handling routine
/// Arguments: - signal number of interrupt calling
///
/// Return value: -
static void dlink_inthand (int sig, siginfo_t *siginfo, void *context){
	dlink_stop = 1;
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

/// read_pipe(): read from pipe and parse JSON
///
/// Arguments: event structure to fill with data from JSON
///
/// Return value: (void)
static int read_pipe(struct eventData * evnt){

	char buf[JSON_FILE_BUF_SIZE];
	struct json_object *root;

	buf[0] = '\0';

	// repeat loop for reading until process ends or interrupt
	// is called
	while (!(dlink_stop) && !feof(inpipe)){

		// read buffer until timeout
		char * got = fgets(buf, JSON_FILE_BUF_SIZE, inpipe);

		// buf read successfully?
		if ((!got) || '\0' == buf[0]) {
			printDbg(PFX "Empty JSON buffer\n");
			continue;
		}

		root = json_tokener_parse(buf);

		// root read successfully?
		if (NULL == root) {
			err_msg("Empty JSON");
			th_return = EXIT_INV_CONFIG;
			pthread_exit(&th_return);
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
			}
		}
		evnt->scope = get_string_value_from(root, "scope", FALSE, NULL);
		evnt->timenano = get_int64_value_from(root, "timeNano", FALSE, 0);
		if (!json_object_put(root)){ // free object
			printDbg(PFX "Could not free objects!\n");
			th_return = EXIT_FAILURE;
			pthread_exit(&th_return);
		}
		return 1;
	}

	// reading stopped. no new element
	return 0;
}

/// check_event(): call pipe read and parse response
///
/// Arguments: - 
///
/// Return value: pointer to valid container event
static contevent_t * check_event() {

	struct eventData evnt;
	memset(&evnt, 0, sizeof(struct eventData)); // set all pointers to NULL -> init

	// read next element from pipe
	if (!read_pipe(&evnt))
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
		}
		else if (!strcmp(evnt.status, "start"))
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
	free(evnt.name);
	free(evnt.id);
	free(evnt.from);
	free(evnt.scope);
	// free main

	return cntevent;
}

/// dlink_thread_watch(): checks for docker events and signals new containers
///
/// Arguments: - 
///
/// Return value: pointer to void (PID exits with error if needed)
void *dlink_thread_watch(void *arg) {

	int pstate = 0;
	pid_t pid;
	char * pcmd;
	contevent_t * cntevent;

	{ // setup interrupt handler block
		struct sigaction act;

		/* Use the sa_sigaction field because the handles has two additional parameters */
		/* The SA_SIGINFO flag tells sigaction() to use the sa_sigaction field, not sa_handler. */
		act.sa_handler = NULL; // On some architectures ---
		act.sa_sigaction = &dlink_inthand; // these are a union, do not assign both, -> first set null, then value
		act.sa_flags = SA_SIGINFO;
		act.sa_restorer = NULL;

		/* blocking signal set */
		if (((sigemptyset(&act.sa_mask)))
			|| (sigaction(SIGHUP, &act, NULL) < 0))	{ // quit from caller
			perror ("Setup of sigaction failed");
			th_return = EXIT_FAILURE;
			pthread_exit(&th_return);
		}
	} // END interrupt handler block

	{
		sigset_t set;
		/* Block all signals except SIGHUP */

		if ( ((sigfillset(&set)))
			|| ((sigdelset(&set, SIGHUP)))
			|| (0 != pthread_sigmask(SIG_BLOCK, &set, NULL))){
			perror ("Setup of sigmask failed");
			th_return = EXIT_FAILURE;
			pthread_exit(&th_return);
		}
	}
	
	if (NULL != arg)
		pcmd = (char *)arg;
	else
		pcmd = DOCKER_CMD_LINE;

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
				if (!(inpipe = popen2 (pcmd, "r", &pid))){
					err_msg_n(errno, "Pipe process open failed!");
					th_return = EXIT_FAILURE;
					pthread_exit(&th_return);
				}
				pstate = 1;
				printDbg(PFX "Reading JSON output from pipe...\n");
				// no break

			case 1:
				if (feof(inpipe))
					pstate = 4;
				else if ((cntevent = check_event()))  // new event?
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
				printDbg(PFX "Stopped\n");
				pthread_exit(&th_return);
		}

		if (3 > pstate && dlink_stop){ // if 3 wait for change, lock is hold
			pstate=4;
		}
	}
}

