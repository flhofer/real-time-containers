#include "dockerlink.h"

#include <stdbool.h>		// for bool defition and operation
#include <json-c/json.h>	// libjson-c for parsing
#include <errno.h>			// error numbers and strings

#include "error.h"		// error and strerr print functions


#define USEC_PER_SEC		1000000
#define NSEC_PER_SEC		1000000000
#define TIMER_RELTIME		0
#define INTERV_RFSH			100000

//// -------------------------------- FROM RT-APP, BEGIN ---------------------------------

#define PFX "[json] "
#define PFL "         "PFX
#define PIN PFX"    "
#define PIN2 PIN"    "
#define PIN3 PIN2"    "
#define JSON_FILE_BUF_SIZE 4096
#define DEFAULT_MEM_BUF_SIZE (4 * 1024 * 1024)

#ifndef TRUE
	#define TRUE true
	#define FALSE false
#endif

/* this macro set a default if key not present, or give an error and exit
 * if key is present but does not have a default */
#define set_default_if_needed(key, value, have_def, def_value) do {	\
	if (!value) {							\
		if (have_def) {						\
			printDbg(PIN "key: %s <default> %d\n", key, def_value);\
			return def_value;				\
		} else {						\
			err_msg(PFX "Key %s not found", key);	\
			exit(EXIT_INV_CONFIG);	\
		}							\
	}								\
} while(0)

/* same as before, but for string, for which we need to strdup in the
 * default value so it can be a literal */
#define set_default_if_needed_str(key, value, have_def, def_value) do {	\
	if (!value) {							\
		if (have_def) {						\
			if (!def_value) {				\
				printDbg(PIN "key: %s <default> NULL\n", key);\
				return NULL;						\
			}										\
			printDbg(PIN "key: %s <default> %s\n",		\
				  key, def_value);					\
			return strdup(def_value);				\
		} else {									\
			err_msg(PFX "Key %s not found", key);	\
			exit(EXIT_INV_CONFIG);					\
		}											\
	}												\
}while (0)

/* get an object obj and check if its type is <type>. If not, print a message
 * (this is what parent and key are used for) and exit
 */
static inline void
assure_type_is(struct json_object *obj,
	       struct json_object *parent,
	       const char *key,
	       enum json_type type)
{
	if (!json_object_is_type(obj, type)) {
		err_msg("Invalid type for key %s", key);
		err_msg("%s", json_object_to_json_string(parent));
		exit(EXIT_INV_CONFIG);
	}
}

/* search a key (what) in object "where", and return a pointer to its
 * associated object. If nullable is false, exit if key is not found */
static inline struct json_object*
get_in_object(struct json_object *where,
	      const char *what,
	      int nullable)
{
	struct json_object *to;
	json_bool ret;
	ret = json_object_object_get_ex(where, what, &to);
	if (!nullable && !ret){
		err_msg(PFX "Error while parsing config\n" PFL);
		exit(EXIT_INV_CONFIG);
	}


	if (!nullable && strcmp(json_object_to_json_string(to), "null") == 0) {
		err_msg(PFX "Cannot find key %s", what);
		exit(EXIT_INV_CONFIG);
	}

	return to;
}

static inline int
get_int_value_from(struct json_object *where,
		   const char *key,
		   int have_def,
		   int def_value)
{
	struct json_object *value;
	int i_value;
	value = get_in_object(where, key, have_def);
	set_default_if_needed(key, value, have_def, def_value);
	assure_type_is(value, where, key, json_type_int);
	i_value = json_object_get_int(value);
	printDbg(PIN "key: %s, value: %d, type <int>\n", key, i_value);
	return i_value;
}

static inline int64_t
get_int64_value_from(struct json_object *where,
		   const char *key,
		   int have_def,
		   int def_value)
{
	struct json_object *value;
	int64_t i_value;
	value = get_in_object(where, key, have_def);
	set_default_if_needed(key, value, have_def, def_value);
	assure_type_is(value, where, key, json_type_int);
	i_value = json_object_get_int64(value);
	printDbg(PIN "key: %s, value: %ld, type <int64>\n", key, i_value);
	return i_value;
}

static inline int
get_bool_value_from(struct json_object *where,
		    const char *key,
		    int have_def,
		    int def_value)
{
	struct json_object *value;
	int b_value;
	value = get_in_object(where, key, have_def);
	set_default_if_needed(key, value, have_def, def_value);
	assure_type_is(value, where, key, json_type_boolean);
	b_value = json_object_get_boolean(value);
	printDbg(PIN "key: %s, value: %d, type <bool>\n", key, b_value);
	return b_value;
}

static inline char*
get_string_value_from(struct json_object *where,
		      const char *key,
		      int have_def,
		      const char *def_value)
{
	struct json_object *value;
	char *s_value;
	value = get_in_object(where, key, have_def);
	set_default_if_needed_str(key, value, have_def, def_value);
	if (json_object_is_type(value, json_type_null)) {
		printDbg(PIN "key: %s, value: NULL, type <string>\n", key);
		return NULL;
	}
	assure_type_is(value, where, key, json_type_string);
	s_value = strdup(json_object_get_string(value));
	printDbg(PIN "key: %s, value: %s, type <string>\n", key, s_value);
	return s_value;
}

//// -------------------------------- FROM RT-APP, END ---------------------------------


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
	char * id;
	char * from;
	char * scope;
	uint64_t timenano;
};

struct eventData * evnt;

/*
enum dockerEvents {
    attach,
    commit,
    copy,
    create,
    destroy,
    detach,
    die,
    exec_create,
    exec_detach,
    exec_die,
    exec_start,
    export,
    health_status,
    kill,
    oom,
    pause,
    rename,
    resize,
    restart,
    start,
    stop,
    top,
    unpause,
    update};
*/

static void docker_read_pipe(){

	char buf[JSON_FILE_BUF_SIZE];
	struct json_object *root;

	printDbg(PFX "Reading JSON output from pipe...\n");

	if (!(fgets(buf, JSON_FILE_BUF_SIZE, inpipe)))
		return;
	buf[JSON_FILE_BUF_SIZE-1] = '\0';
	
	root = json_tokener_parse(buf);

	// root read successfully?
	if (root == NULL) {
		err_msg(PFX "Error while parsing input JSON");
		exit(EXIT_INV_CONFIG);
	}

	free(evnt->type);
	free(evnt->scope);

	evnt->type = get_string_value_from(root, "Type", FALSE, NULL);
	if (!strcmp(evnt->type, "container")) {
		free(evnt->status);
		free(evnt->id);
		free(evnt->from);
		evnt->status = get_string_value_from(root, "status", FALSE, NULL);
		evnt->id = get_string_value_from(root, "id", FALSE, NULL);
		evnt->from = get_string_value_from(root, "from", FALSE, NULL);
	}
	evnt->scope = get_string_value_from(root, "scope", FALSE, NULL);
	evnt->timenano = get_int64_value_from(root, "timeNano", FALSE, 0);
	json_object_put(root); // free object
}

static contevent_t * docker_check_event() {

	docker_read_pipe();
	contevent_t * cntevent;

	if (!strcmp(evnt->type, "container")){
		// kill	
		if ((!strcmp(evnt->status, "kill")))
		{
			cntevent = malloc(sizeof(contevent_t));
			
			cntevent->event = cnt_remove;
			cntevent->id = strdup(evnt->id);
			cntevent->image = strdup(evnt->from);
			cntevent->timenano = evnt->timenano;
			return cntevent;
		}
		if ((!strcmp(evnt->status, "create")) ||
			(!strcmp(evnt->status, "start")))
		{
			cntevent = malloc(sizeof(contevent_t));
			
			cntevent->event = cnt_add;
			cntevent->id = strdup(evnt->id);
			cntevent->image = strdup(evnt->from);
			cntevent->timenano = evnt->timenano;
			return cntevent;
		}
		return NULL;
	}
	else
		return NULL;
}

/// thread_watch_docker(): checks for docker events and signals new containers
///
/// Arguments: - 
///
/// Return value: void (pid exits with error if needed)
void *thread_watch_docker(void *arg) {

	int pstate = 0;
	char * cmd;
	contevent_t * cntevent;
	
	if (NULL != arg)
		cmd = (char *)arg;
	else
		cmd = "docker events --format '{{json .}}'";

	int ret;
	struct timespec intervaltv, now, old;

	// get clock, use it as a future reference for update time TIMER_ABS*
	ret = clock_gettime(CLOCK_MONOTONIC, &intervaltv);
	if (0 != ret) {
		if (EINTR != ret)
			warn("clock_gettime() failed: %s", strerror(errno));
		pstate=5;
	}
	old = intervaltv;

	while (1) {
		switch (pstate) {

			case 0: 
				inpipe = popen (cmd, "r");
				evnt = malloc (sizeof(struct eventData));
				pstate = 1;
				break;

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
				// free elements
				free(evnt->type);
				free(evnt->status);
				free(evnt->id);
				free(evnt->from);
				free(evnt->scope);
				// free main
				free(evnt);
				pclose(inpipe);

			case 5:
				pthread_exit(0); // exit the thread signalling normal return
				break;
		}

		if ((1 == pstate) || (2==pstate)) {
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
		}

	}
}

