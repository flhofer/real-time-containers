// NOTE -> inspired by rt-app, may need licence header
// Will change a lot. let's do a first change and then an estimation
// http://json-c.github.io/json-c/json-c-0.13.1/doc/html/json__object_8h.html#a8c56dc58a02f92cd6789ba5dcb9fe7b1

#include "parse_config.h"

#include <stdio.h>			// STD IO print and file operations
#include <string.h>			// string operations
#include <stdbool.h>		// for bool defition and operation
#include <fcntl.h>			// file control 
#include <sched.h>			// scheduler functions
#include <errno.h>			// error numbers and strings
#include <json-c/json.h>	// libjson-c for parsing
#include <sys/sysinfo.h>	// system general information

// static library includes
#include "kernutil.h"		// kernel util data types and functions
#include "rt-utils.h"	// trace and other utils

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

/* redefine foreach as in <json/json_object.h> but to be ANSI
 * compatible */
#define foreach(obj, entry, key, val, idx)				\
	for ( ({ idx = 0; entry = json_object_get_object(obj)->head;});	\
		({ if (entry) { key = (char*)entry->k;			\
				val = (struct json_object*)entry->v;	\
			      };					\
		   entry;						\
		 }							\
		);							\
		({ entry = entry->next; idx++; })			\
	    )
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

/*
static void init_mutex_resource(rtapp_resource_t *data, const parm_t *parm)
{
	printDbg(PIN3 "Init: %s mutex\n", data->name);

	pthread_mutexattr_init(&data->res.mtx.attr);
	if (parm->pi_enabled) {
		pthread_mutexattr_setprotocol(
				&data->res.mtx.attr,
				PTHREAD_PRIO_INHERIT);
	}
	pthread_mutex_init(&data->res.mtx.obj,
			&data->res.mtx.attr);
}

static void init_timer_resource(rtapp_resource_t *data, const parm_t *parm)
{
	printDbg(PIN3 "Init: %s timer\n", data->name);
	data->res.timer.init = 0;
	data->res.timer.relative = 1;
}

static void init_cond_resource(rtapp_resource_t *data, const parm_t *parm)
{
	printDbg(PIN3 "Init: %s wait\n", data->name);

	pthread_condattr_init(&data->res.cond.attr);
	pthread_cond_init(&data->res.cond.obj,
			&data->res.cond.attr);
}

static void init_membuf_resource(rtapp_resource_t *data, const parm_t *parm)
{
	printDbg(PIN3 "Init: %s membuf\n", data->name);

	data->res.buf.ptr = malloc(parm->mem_buffer_size);
	data->res.buf.size = parm->mem_buffer_size;
}

static void init_iodev_resource(rtapp_resource_t *data, const parm_t *parm)
{
	printDbg(PIN3 "Init: %s io device\n", data->name);

	data->res.dev.fd = open(parm->io_device, O_CREAT | O_WRONLY, 0644);
}

static void init_barrier_resource(rtapp_resource_t *data, const parm_t *parm)
{
	printDbg(PIN3 "Init: %s barrier\n", data->name);

	/* each task waiting for this resource will increment this counter.
	 * start at -1 so that when we see this is zero we are the last man
	 * to enter the sync point and should wake everyone else.
	 *//*
	data->res.barrier.waiting = -1;
	pthread_mutexattr_init(&data->res.barrier.m_attr);
	if (parm->pi_enabled) {
		pthread_mutexattr_setprotocol(
				&data->res.barrier.m_attr,
				PTHREAD_PRIO_INHERIT);
	}
	pthread_mutex_init(&data->res.barrier.m_obj,
			&data->res.barrier.m_attr);

	pthread_cond_init(&data->res.barrier.c_obj, NULL);
}

static void
init_resource_data(const char *name, int type, rtapp_resources_t *resources_table,
		int idx, const parm_t *parm)
{
	rtapp_resource_t *data = &(resources_table->resources[idx]);

	/* common and defaults *//*
	data->index = idx;
	data->name = strdup(name);
	data->type = type;

	switch (data->type) {
		case rtapp_mutex:
			init_mutex_resource(data, parm);
			break;
		case rtapp_timer:
		case rtapp_timer_unique:
			init_timer_resource(data, parm);
			break;
		case rtapp_wait:
			init_cond_resource(data, parm);
			break;
		case rtapp_mem:
			init_membuf_resource(data, parm);
			break;
		case rtapp_iorun:
			init_iodev_resource(data, parm);
			break;
		case rtapp_barrier:
			init_barrier_resource(data, parm);
			break;
		default:
			break;
	}
}

static void
parse_resource_data(const char *name, struct json_object *obj, int idx,
		  rtapp_resource_t *data, const parm_t *parm)
{
	char *type;
	char def_type[RTAPP_RESOURCE_DESCR_LENGTH];

	printDbg(PFX "Parsing resources %s [%d]\n", name, idx);

	/* resource type *//*
	resource_to_string(0, def_type);
	type = get_string_value_from(obj, "type", TRUE, def_type);
	if (string_to_resource(type, &data->type) != 0) {
		log_critical(PIN2 "Invalid type of resource %s", type);
		exit(EXIT_INV_CONFIG);
	}

	/*
	 * get_string_value_from allocate the string so with have to free it
	 * once useless
	 *//*
	free(type);

	init_resource_data(name, data->type, parm->resources, idx, parm);
}

static int
add_resource_data(const char *name, int type, rtapp_resources_t **resources_table, parm_t *parm)
{
	int idx, size;
	rtapp_resources_t *table = *resources_table;

	idx = table->nresources;

	printDbg(PIN2 "Add new resource %s [%d] type %d\n", name, idx, type);

	table->nresources++;

	size = sizeof(rtapp_resources_t) + sizeof(rtapp_resource_t) * table->nresources;

	*resources_table = realloc(*resources_table, size);

	if (!*resources_table) {
		err_msg("Failed to allocate memory for resource %s", name);
		return -1;
	}

	/*
	 * We can't reuse table as *resources_table might have changed following
	 * realloc
	 *//*
	init_resource_data(name, type, *resources_table, idx, parm);

	return idx;
}

static void
parse_resources(struct json_object *resources, parm_t *parm)
{
	struct lh_entry *entry; char *key; struct json_object *val; int idx;
	int size;

	printDbg(PFX "Parsing resource section\n");

	/*
	 * Create at least an "empty" struct that will then be filled either will
	 * parsing resources table or when parsing task's event
	 *//*
	parm->resources = malloc(sizeof(rtapp_resources_t));
	if (!parm->resources) {
		err_msg("Failed to allocate memory for resources");
		exit(EXIT_FAILURE);
	}

	parm->resources->nresources = 0;

	if (!resources) {
		printDbg(PFX "No resource section Found\n");
		return;
	}

	if (json_object_is_type(resources, json_type_object)) {
		parm->resources->nresources = 0;
		foreach(resources, entry, key, val, idx) {
			parm->resources->nresources++;
		}

		printDbg(PFX "Found %d Resources\n", parm->resources->nresources);
		size = sizeof(rtapp_resources_t) + sizeof(rtapp_resource_t) * parm->resources->nresources;

		parm->resources = realloc(parm->resources, size);
		if (!parm->resources) {
				err_msg("Failed to allocate memory for resources");
				exit(EXIT_FAILURE);
		}

		foreach (resources, entry, key, val, idx) {
			parse_resource_data(key, val, idx, &parm->resources->resources[idx], parm);
		}
	}
}

static int get_resource_index(const char *name, int type, rtapp_resources_t **resources_table, parm_t *parm)
{
	int nresources = (*resources_table)->nresources;
	rtapp_resource_t *resources = (*resources_table)->resources;
	int i = 0;
	printDbg(PIN "get_resource_index %d events\n", nresources);

	while ((i < nresources) && ((strcmp(resources[i].name, name) != 0) || (resources[i].type != type)))
		i++;

	if (i >= nresources)
		i = add_resource_data(name, type, resources_table, parm);

	return i;
}

static char* create_unique_name(char *tmp, int size, const char* ref, long tag)
{
	snprintf(tmp, size, "%s%lx", ref, (long)(tag));
	return tmp;
}

static void
parse_task_event_data(char *name, struct json_object *obj,
		  event_data_t *data, thread_data_t *tdata, parm_t *parm)
{
	rtapp_resources_t **resources_table = tdata->global_resources;
	rtapp_resource_t *rdata, *ddata;
	char unique_name[22];
	const char *ref;
	char *tmp;
	long tag = (long)tdata;
	int i;

	if (!strncmp(name, "run", strlen("run")) ||
			!strncmp(name, "sleep", strlen("sleep"))) {

		if (!json_object_is_type(obj, json_type_int))
			goto unknown_event;

		data->duration = json_object_get_int(obj);

		if (!strncmp(name, "sleep", strlen("sleep")))
			data->type = rtapp_sleep;
		else if (!strncmp(name, "runtime", strlen("runtime")))
			data->type = rtapp_runtime;
		else
			data->type = rtapp_run;

		printDbg(PIN2 "type %d duration %d\n", data->type, data->duration);
		return;
	}

	if (!strncmp(name, "mem", strlen("mem")) ||
			!strncmp(name, "iorun", strlen("iorun"))) {
		if (!json_object_is_type(obj, json_type_int))
			goto unknown_event;

		/* create an unique name for per-thread buffer *//*
		ref = create_unique_name(unique_name, sizeof(unique_name), "mem", tag);
		i = get_resource_index(ref, rtapp_mem, resources_table, parm);
		data->res = i;
		data->count = json_object_get_int(obj);

		/* A single IO devices for all threads *//*
		if (strncmp(name, "iorun", strlen("iorun")) == 0) {
			i = get_resource_index("io_device", rtapp_iorun, resources_table, parm);
			data->dep = i;
		};

		if (!strncmp(name, "mem", strlen("mem")))
			data->type = rtapp_mem;
		else
			data->type = rtapp_iorun;

		printDbg(PIN2 "type %d count %d\n", data->type, data->count);
		return;
	}

	if (!strncmp(name, "lock", strlen("lock")) ||
			!strncmp(name, "unlock", strlen("unlock"))) {

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);
		i = get_resource_index(ref, rtapp_mutex, resources_table, parm);

		data->res = i;

		if (!strncmp(name, "lock", strlen("lock")))
			data->type = rtapp_lock;
		else
			data->type = rtapp_unlock;

		rdata = &((*resources_table)->resources[data->res]);

		printDbg(PIN2 "type %d target %s [%d]\n", data->type, rdata->name, rdata->index);
		return;
	}

	if (!strncmp(name, "signal", strlen("signal")) ||
			!strncmp(name, "broad", strlen("broad"))) {

		if (!strncmp(name, "signal", strlen("signal")))
			data->type = rtapp_signal;
		else
			data->type = rtapp_broadcast;

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);
		i = get_resource_index(ref, rtapp_wait, resources_table, parm);

		data->res = i;

		rdata = &((*resources_table)->resources[data->res]);

		printDbg(PIN2 "type %d target %s [%d]\n", data->type, rdata->name, rdata->index);
		return;
	}

	if (!strncmp(name, "wait", strlen("wait")) ||
			!strncmp(name, "sync", strlen("sync"))) {

		if (!strncmp(name, "wait", strlen("wait")))
			data->type = rtapp_wait;
		else
			data->type = rtapp_sig_and_wait;

		tmp = get_string_value_from(obj, "ref", TRUE, "unknown");
		i = get_resource_index(tmp, rtapp_wait, resources_table, parm);
		/*
		 * get_string_value_from allocate the string so with have to free it
		 * once useless
		 *//*
		free(tmp);

		data->res = i;

		tmp = get_string_value_from(obj, "mutex", TRUE, "unknown");
		i = get_resource_index(tmp, rtapp_mutex, resources_table, parm);
		/*
		 * get_string_value_from allocate the string so with have to free it
		 * once useless
		 *//*
		free(tmp);

		data->dep = i;

		rdata = &((*resources_table)->resources[data->res]);
		ddata = &((*resources_table)->resources[data->dep]);

		printDbg(PIN2 "type %d target %s [%d] mutex %s [%d]\n", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
		return;
	}

    if (!strncmp(name, "barrier", strlen("barrier"))) {

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		data->type = rtapp_barrier;

		ref = json_object_get_string(obj);
		i = get_resource_index(ref, rtapp_barrier, resources_table, parm);

		data->res = i;
		rdata = &((*resources_table)->resources[data->res]);
		rdata->res.barrier.waiting += 1;

		printDbg(PIN2 "type %d target %s [%d] %d users so far\n", data->type, rdata->name, rdata->index, rdata->res.barrier.waiting);
		return;
	}

	if (!strncmp(name, "timer", strlen("timer"))) {

		tmp = get_string_value_from(obj, "ref", TRUE, "unknown");

		if (!strncmp(tmp, "unique", strlen("unique"))) {
			ref = create_unique_name(unique_name, sizeof(unique_name), tmp, tag);
			resources_table = &tdata->local_resources;
			data->type = rtapp_timer_unique;
		} else {
			ref = tmp;
			data->type = rtapp_timer;
		}

		data->res = get_resource_index(ref, data->type, resources_table, parm);

		/*
		 * get_string_value_from allocate the string so with have to free it
		 * once useless
		 *//*
		free(tmp);

		data->duration = get_int_value_from(obj, "period", TRUE, 0);

		rdata = &((*resources_table)->resources[data->res]);

		tmp = get_string_value_from(obj, "mode", TRUE, "relative");
		if (!strncmp(tmp, "absolute", strlen("absolute")))
			rdata->res.timer.relative = 0;
		free(tmp);

		printDbg(PIN2 "type %d target %s [%d] period %d\n", data->type, rdata->name, rdata->index, data->duration);
		return;
	}

	if (!strncmp(name, "resume", strlen("resume"))) {

		data->type = rtapp_resume;

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);

		i = get_resource_index(ref, rtapp_wait, resources_table, parm);

		data->res = i;

		i = get_resource_index(ref, rtapp_mutex, resources_table, parm);

		data->dep = i;

		rdata = &((*resources_table)->resources[data->res]);
		ddata = &((*resources_table)->resources[data->dep]);

		printDbg(PIN2 "type %d target %s [%d] mutex %s [%d]\n", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
		return;
	}

	if (!strncmp(name, "suspend", strlen("suspend"))) {

		data->type = rtapp_suspend;

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);

		i = get_resource_index(ref, rtapp_wait, resources_table, parm);

		data->res = i;

		i = get_resource_index(ref, rtapp_mutex, resources_table, parm);

		data->dep = i;

		rdata = &((*resources_table)->resources[data->res]);
		ddata = &((*resources_table)->resources[data->dep]);

		printDbg(PIN2 "type %d target %s [%d] mutex %s [%d]\n", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
		return;
	}

	if (!strncmp(name, "yield", strlen("yield"))) {
		data->type = rtapp_yield;
		printDbg(PIN2 "type %d\n", data->type);
		return;
	}

	if (!strncmp(name, "fork", strlen("fork"))) {

		data->type = rtapp_fork;

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);

		i = get_resource_index(ref, rtapp_fork, resources_table, parm);

		data->res = i;

		rdata = &((*resources_table)->resources[data->res]);

		rdata->res.fork.ref = strdup(ref);
		rdata->res.fork.tdata = NULL;
		rdata->res.fork.nforks = 0;

		if (!rdata->res.fork.ref) {
			err_msg("Failed to duplicate ref");
			exit(EXIT_FAILURE);
		}

		printDbg(PIN2 "type %d target %s [%d]\n", data->type, rdata->name, rdata->index);
		return;
	}

	err_msg(PIN2 "Resource %s not found in the resource section !!!", ref);
	err_msg(PIN2 "Please check the resource name or the resource section");

unknown_event:
	data->duration = 0;
	data->type = rtapp_run;
	err_msg(PIN2 "Unknown or mismatch %s event type !!!", name);

}

static char *events[] = {
	"lock",
	"unlock",
	"wait",
	"signal",
	"broad",
	"sync",
	"sleep",
	"runtime",
	"run",
	"timer",
	"suspend",
	"resume",
	"mem",
	"iorun",
	"yield",
	"barrier",
	"fork",
	NULL
};

static int
obj_is_event(char *name)
{
    char **pos;

    for (pos = events; *pos; pos++) {
	    char *event = *pos;
	    if (!strncmp(name, event, strlen(event)))
		    return 1;
    }

    return 0;
}

static void parse_cpuset_data(struct json_object *obj, cpuset_data_t *data)
{
	struct json_object *cpuset_obj, *cpu;
	unsigned int max_cpu = sysconf(_SC_NPROCESSORS_CONF) - 1;

	/* cpuset *//*
	cpuset_obj = get_in_object(obj, "cpus", TRUE);
	if (cpuset_obj) {
		unsigned int i;
		unsigned int cpu_idx;

		assure_type_is(cpuset_obj, obj, "cpus", json_type_array);
		data->cpuset_str = strdup(json_object_to_json_string(cpuset_obj));
		data->cpusetsize = sizeof(cpu_set_t);
		data->cpuset = malloc(data->cpusetsize);
		CPU_ZERO(data->cpuset);
		for (i = 0; i < json_object_array_length(cpuset_obj); i++) {
			cpu = json_object_array_get_idx(cpuset_obj, i);
			cpu_idx = json_object_get_int(cpu);
			if (cpu_idx > max_cpu) {
				log_critical(PIN2 "Invalid cpu %u in cpuset %s", cpu_idx, data->cpuset_str);
				free(data->cpuset);
				free(data->cpuset_str);
				exit(EXIT_INV_CONFIG);
			}
			CPU_SET(cpu_idx, data->cpuset);
		}
	} else {
		data->cpuset_str = strdup("-");
		data->cpuset = NULL;
		data->cpusetsize = 0;
	}
	printDbg(PIN "key: cpus %s\n", data->cpuset_str);
}

static sched_data_t *parse_sched_data(struct json_object *obj, int def_policy)
{
	sched_data_t tmp_data;
	char *def_str_policy;
	char *policy;
	int prior_def = -1;

	/* Get default policy *//*
	def_str_policy = policy_to_string(def_policy);

	/* Get Policy *//*
	policy = get_string_value_from(obj, "policy", TRUE, def_str_policy);
	if (policy ){
		if (string_to_policy(policy, &tmp_data.policy) != 0) {
			log_critical(PIN2 "Invalid policy %s", policy);
			exit(EXIT_INV_CONFIG);
		}
	} else {
		tmp_data.policy = -1;
	}

	/* Get priority *//*
	if (tmp_data.policy == -1)
		prior_def = -1;
	else if (tmp_data.policy == other || tmp_data.policy == idle)
		prior_def = DEFAULT_THREAD_NICE;
	else
		prior_def = DEFAULT_THREAD_PRIORITY;

	tmp_data.prio = get_int_value_from(obj, "priority", TRUE, prior_def);

	/* deadline params *//*
	tmp_data.runtime = get_int_value_from(obj, "dl-runtime", TRUE, 0);
	tmp_data.period = get_int_value_from(obj, "dl-period", TRUE, tmp_data.runtime);
	tmp_data.deadline = get_int_value_from(obj, "dl-deadline", TRUE, tmp_data.period);


	if (def_policy != -1) {
		/* Support legacy grammar for thread object *//*
		if (!tmp_data.runtime)
			tmp_data.runtime = get_int_value_from(obj, "runtime", TRUE, 0);
		if (!tmp_data.period)
			tmp_data.period = get_int_value_from(obj, "period", TRUE, tmp_data.runtime);
		if (!tmp_data.deadline)
			tmp_data.deadline = get_int_value_from(obj, "deadline", TRUE, tmp_data.period);
	}

	/* Move from usec to nanosec *//*
	tmp_data.runtime *= 1000;
	tmp_data.period *= 1000;
	tmp_data.deadline *= 1000;

	/* Check if we found at least one meaningful scheduler parameter *//*
	if (tmp_data.prio != -1 || tmp_data.runtime || tmp_data.period || tmp_data.period) {
		sched_data_t *new_data;

		/* At least 1 parameters has been set in the object *//*
		new_data = malloc(sizeof(sched_data_t));
		memcpy( new_data, &tmp_data,sizeof(sched_data_t));

		log_debug(PIN "key: set scheduler %d with priority %d", new_data->policy, new_data->prio);

		return new_data;
	}

	return NULL;
}

static taskgroup_data_t *parse_taskgroup_data(struct json_object *obj)
{
	char *name = get_string_value_from(obj, "taskgroup", TRUE, "");
	taskgroup_data_t *tg;

	if (!strlen(name))
		return NULL;

	if (*name != '/') {
		log_critical(PIN2 "Taskgroup [%s] has to start with [/]", name);
		exit(EXIT_INV_CONFIG);
	}

	tg = find_taskgroup(name);
	if (tg)
		return tg;
	tg = alloc_taskgroup(strlen(name) + 1);
	if (!tg) {
		log_critical(PIN2 "Cannot allocate taskgroup");
		exit(EXIT_FAILURE);
	}

	strcpy(tg->name, name);

	return tg;
}

static void check_taskgroup_policy_dep(phase_data_t *pdata, thread_data_t *tdata)
{
	/* Save sched_data as thread's current sched_data. *//*
	if (pdata->sched_data)
		tdata->curr_sched_data = pdata->sched_data;

	/* Save taskgroup_data as thread's current taskgroup_data. *//*
	if (pdata->taskgroup_data)
		tdata->curr_taskgroup_data = pdata->taskgroup_data;

	/*
	 * Detect policy/taskgroup misconfiguration: a task which specifies a
	 * taskgroup should not run in a policy other than SCHED_OTHER.
	 *//*
	if (tdata->curr_sched_data && tdata->curr_sched_data->policy != other &&
	    tdata->curr_taskgroup_data) {
		log_critical(PIN2 "No taskgroup support for policy %s",
			     policy_to_string(tdata->curr_sched_data->policy));
		exit(EXIT_INV_CONFIG);
	}
}

static void
parse_task_phase_data(struct json_object *obj,
		  phase_data_t *data, thread_data_t *tdata, parm_t *parm)
{
	/* used in the foreach macro *//*
	struct lh_entry *entry; char *key; struct json_object *val; int idx;
	int i;

	printDbg(PFX "Parsing phase\n");

	/* loop *//*
	data->loop = get_int_value_from(obj, "loop", TRUE, 1);

	/* Count number of events *//*
	data->nbevents = 0;
	foreach(obj, entry, key, val, idx) {
		if (obj_is_event(key))
				data->nbevents++;
	}

	if (data->nbevents == 0) {
		log_critical(PIN "No events found. Task must have events or it's useless");
		exit(EXIT_INV_CONFIG);

	}

	printDbg(PIN "Found %d events\n", data->nbevents);

	data->events = malloc(data->nbevents * sizeof(event_data_t));

	/* Parse events *//*
	i = 0;
	foreach(obj, entry, key, val, idx) {
		if (obj_is_event(key)) {
			printDbg(PIN "Parsing event %s", key);
			parse_task_event_data(key, val, &data->events[i], tdata, parm);
			i++;
		}
	}
	parse_cpuset_data(obj, &data->cpu_data);
	data->sched_data = parse_sched_data(obj, -1);
	data->taskgroup_data = parse_taskgroup_data(obj);
}
*/

static void parse_container_data(struct json_object *obj, int index, 
		parm_t *data, contparm_t *parm)
{
	struct json_object *phases_obj, *resources, *nobj;

	char *name;

	printDbg(PFX "container    : %s\n", json_object_to_json_string(obj));

	nobj = get_in_object(obj, "contid", TRUE);
	name = get_string_value_from(nobj, "contid", TRUE, "--");

	printDbg(PFX "Parsing container %.12s [%d]\n", name, index);

	nobj = json_object_put(nobj);

	/* common and defaults */
	/*
	 * Set a pointer to opt entry as we might end up to modify the localtion of
	 * global resources during realloc
	 *//*
	data->global_resources = &(parm->resources);

	/*
	 * Create at least an "empty" struct that will then be filled either will
	 * parsing resources table or when parsing task's event
	 *//*
	data->local_resources = malloc(sizeof(rtapp_resources_t));
	if (!data->local_resources) {
		err_msg("Failed to allocate memory for local resources");
		exit(EXIT_FAILURE);
	}

	data->local_resources->nresources = 0;
	data->ind = index;
	data->name = strdup(name);
	data->lock_pages = parm->lock_pages;

	data->cpu_data.cpuset = NULL;
	data->cpu_data.cpuset_str = NULL;
	data->curr_cpu_data = NULL;
	data->def_cpu_data.cpuset = NULL;
	data->def_cpu_data.cpuset_str = NULL;

	/* cpuset *//*
	parse_cpuset_data(obj, &data->cpu_data);
	/* Scheduling policy *//*
	data->sched_data = parse_sched_data(obj, parm->policy);
	/* Taskgroup *//*
	data->taskgroup_data = parse_taskgroup_data(obj);

	/*
	 * Thread's current sched_data and taskgroup_data are used to detect
	 * policy/taskgroup misconfiguration.
	 *//*
	data->curr_sched_data = data->sched_data;
	data->curr_taskgroup_data = data->taskgroup_data;

	/* initial delay *//*
	data->delay = get_int_value_from(obj, "delay", TRUE, 0);

	/* It's the responsibility of the caller to set this if we were forked *//*
	data->forked = 0;
	data->num_instances = get_int_value_from(obj, "instance", TRUE, 1);

	/* Get phases *//*
	phases_obj = get_in_object(obj, "phases", TRUE);
	if (phases_obj) {
		/* used in the foreach macro *//*
		struct lh_entry *entry; char *key; struct json_object *val; int idx;

		assure_type_is(phases_obj, obj, "phases",
					json_type_object);

		printDbg(PIN "Parsing phases section\n");
		data->nphases = 0;
		foreach(phases_obj, entry, key, val, idx) {
			data->nphases++;
		}

		printDbg(PIN "Found %d phases\n", data->nphases);
		data->phases = malloc(sizeof(phase_data_t) * data->nphases);
		foreach(phases_obj, entry, key, val, idx) {
			printDbg(PIN "Parsing phase %s\n", key);
			assure_type_is(val, phases_obj, key, json_type_object);
			parse_task_phase_data(val, &data->phases[idx], data, parm);
			/*
			 * Uses thread's current sched_data and taskgroup_data
			 * to detect policy/taskgroup misconfiguration.
			 *//*
			check_taskgroup_policy_dep(&data->phases[idx], data);
		}

		/* Get loop number *//*
		data->loop = get_int_value_from(obj, "loop", TRUE, -1);

	} else {
		data->nphases = 1;
		data->phases = malloc(sizeof(phase_data_t) * data->nphases);
		parse_task_phase_data(obj,  &data->phases[0], data, parm);

		/* There is no "phases" object which means that thread and phase will
		 * use same scheduling parameters. But thread object looks for default
		 * value when parameters are not defined whereas phase doesn't.
		 * We remove phase's scheduling policy which is a subset of thread's one
		 *//*
		free(data->phases[0].sched_data);
		data->phases[0].sched_data = NULL;

		/*
		 * Uses thread's current sched_data and taskgroup_data
		 * to detect policy/taskgroup misconfiguration.
		 *//*
		check_taskgroup_policy_dep(&data->phases[0], data);
		/*
		 * Get loop number:
		 *
		 * If loop is specified, we already parsed it in
		 * parse_task_phase_data() above, so we just need to remember
		 * that we don't want to loop forever.
		 *
		 * If not specified we want to loop forever.
		 *
		 *//*
		if (get_in_object(obj, "loop", TRUE))
			data->loop = 1;
		else
			data->loop = -1;
	}

	/* Reset thread's current sched_data and taskgroup_data after parsing. *//*
	data->curr_sched_data = NULL;
	data->curr_taskgroup_data = NULL;*/
} 

static void parse_containers(struct json_object *containers, contparm_t *parm)
{
	
	printDbg(PFX "Parsing containers section\n");

	{ // block, container and pid count

		json_object *contlist;
		json_object *contobj;
		json_object *pidobj;
		parm->nthreads = 0;
		parm->num_cont = 0;
		int idx = 0;

		if ((contlist = (json_object *)json_object_get_array(containers))) {
			// scan trough array_list

			while ((contobj = json_object_array_get_idx (containers, idx))) {

				// for each container check pid entries count							
				if ((pidobj = get_in_object (contobj, "pids", TRUE))){
					parm->nthreads += json_object_array_length(pidobj);
					// free pidlist object
					json_object_put(pidobj);
				}

				// update conuters
				parm->num_cont++;
				idx++;

				// free container object
				json_object_put(contobj);
			}
			// free container list
			json_object_put(contlist);
		}
		printDbg(PFX "Found %d thread configurations in %d containers\n", parm->nthreads, parm->num_cont);
	} // END container and pid count block

	{ // container parse block
		/*
		 * Parse thread data of defined containers so that we can use them later
		 * when creating the containers at main() and fork event.
		 */

		json_object *contlist;
		json_object *contobj;
		int idx = 0;

		if ((contlist = (json_object *)json_object_get_array(containers))) {
			// scan trough array_list

			while ((contobj = json_object_array_get_idx (containers, idx))) {
				ppush(&parm->cont); // add new element to the head
				parse_container_data(contobj, idx, parm->cont, parm); 

				// update counters
				idx++;

				// free container object
				json_object_put(contobj);
			}
			// free container list
			json_object_put(contlist);
		}
	} // END container parse block
}


/// parse_global(): extract parameter values from JSON tokens
///
/// Arguments: - json object of tree containing global configuration
/// 		   - structure to store values in
///
/// Return value: no return value, exits on error
static void parse_global(struct json_object *global, prgset_t *set)
{

	printDbg(PFX "Parsing global section\n");
	if (!global) {
		printDbg(PFX " No global section Found: Use default value\n");

		// TODO set only if NULL

		// logging TODO:
		if (!(set->logdir = strdup("./")) || 
			!(set->logbasename = strdup("orchestrator.txt")))
			err_exit_n(errno, "Can not set parameter");
		set->logsize = 0;

		// signatures and folders
		if (!(set->cont_ppidc = strdup(CONT_PPID)) ||
			!(set->cont_pidc = strdup(CONT_PID)) ||
			!(set->cont_cgrp = strdup(CONT_DCKR)))
			err_exit_n(errno, "Can not set parameter");

		// filepaths virtual file system
		if (!(set->procfileprefix = strdup("/proc/sys/kernel/")) ||
			!(set->cpusetfileprefix = strdup("/sys/fs/cgroup/cpuset/")) ||
			!(set->cpusystemfileprefix = strdup("/sys/devices/system/cpu/")))
			err_exit_n(errno, "Can not set parameter");

		set->cpusetdfileprefix = malloc(strlen(set->cpusetfileprefix) + strlen(set->cont_cgrp)+1);
		if (!set->cpusetdfileprefix)
			err_exit_n(errno, "Could not allocate memory");

		*set->cpusetdfileprefix = '\0'; // set first chat to null
		set->cpusetdfileprefix = strcat(strcat(set->cpusetdfileprefix, set->cpusetfileprefix), set->cont_cgrp);		

		return;
	}


	/* Will use default value as set by command line parameters, so to
		be able to have an override switch */

	// filepaths
	if (!set->logdir)
		set->logdir = get_string_value_from(global, "logdir", TRUE, "./");
	if (!set->logbasename)
		set->logbasename = get_string_value_from(global, "log_basename", TRUE,
			"orchestrator.txt");

	// filepaths virtual file system
	if (!set->procfileprefix)
		set->procfileprefix = get_string_value_from(global, "prc_kernel", TRUE,
			"/proc/sys/kernel/");
	if (!set->cpusetfileprefix)
		set->cpusetfileprefix = get_string_value_from(global, "sys_cpuset", TRUE,
		"/sys/fs/cgroup/cpuset/");
	if (!set->cpusystemfileprefix)
		set->cpusystemfileprefix = get_string_value_from(global, "sys_cpu", TRUE,
		"/sys/devices/system/cpu/");
	// one comes later

	// signatures and folders
	if (!set->cont_ppidc)
		set->cont_ppidc = get_string_value_from(global, "cont_ppidc", TRUE, CONT_PPID);
	if (!set->cont_pidc)
		set->cont_pidc = get_string_value_from(global, "cont_pidc", TRUE, CONT_PID);
	if (!set->cont_cgrp){
		set->cont_cgrp = get_string_value_from(global, "cont_cgrp", TRUE, CONT_DCKR);
	
		// create depending virtual file system entry
		set->cpusetdfileprefix = malloc(strlen(set->cpusetfileprefix) + strlen(set->cont_cgrp)+1);
		if (!set->cpusetdfileprefix)
			err_exit_n(errno, "Could not allocate memory");

		*set->cpusetdfileprefix = '\0'; // set first chat to null
		set->cpusetdfileprefix = strcat(strcat(set->cpusetdfileprefix, set->cpusetfileprefix), set->cont_cgrp);		
	}

	set->priority = get_int_value_from(global, "priority", TRUE, set->priority);
	set->clocksel = get_int_value_from(global, "clock", TRUE, set->clocksel);

	{  // char policy block

		char *policy;
		policy = get_string_value_from(global, "default_policy",
						   TRUE, "SCHED_OTHER");
		if (string_to_policy(policy, &set->policy)) {
			err_msg(PFX "Invalid policy %s", policy);
			exit(EXIT_INV_CONFIG);
		}
		free(policy);

	} // END policy block

	set->quiet = get_bool_value_from(global, "quiet", TRUE, set->quiet);
	set->affother = get_bool_value_from(global, "affother", TRUE, set->affother);
	set->setdflag = get_bool_value_from(global, "setdflag", TRUE, set->setdflag);
	set->interval = get_int_value_from(global, "interval", TRUE, set->interval);
	set->update_wcet = get_int_value_from(global, "dl_wcet", TRUE, set->update_wcet);
	set->loops = get_int_value_from(global, "loops", TRUE, set->loops);
	set->runtime = get_int_value_from(global, "runtime", TRUE, set->runtime);
	set->psigscan = get_bool_value_from(global, "psigscan", TRUE, set->psigscan);
	set->trackpids = get_bool_value_from(global, "trackpids", TRUE, set->trackpids);
	// dryrun, cli only
	set->lock_pages = get_bool_value_from(global, "lock_pages", TRUE, set->lock_pages);
	// force, cli only
	set->smi = get_bool_value_from(global, "smi", TRUE, set->smi);
	set->rrtime = get_int_value_from(global, "rrtime", TRUE, set->rrtime);
	set->use_fifo = get_bool_value_from(global, "use_fifo", TRUE, set->use_fifo);
	//kernelversion -> runtime parameter

	{ // affinity selection switch block
		char *setaffinity;

		setaffinity = get_string_value_from(global, "setaffinity",
					       TRUE, "AFFINITY_UNSPECIFIED");

		// function to evaluate string value!
		set->setaffinity = string_to_affinity(setaffinity);
		free(setaffinity);

	} // END affinity selection switch block

	{  // default affinity mask and selection block

		char *defafin;
		if (!(defafin = malloc(10))) // has never been set
			err_exit("could not allocate memory!");

		(void)sprintf(defafin, "%d-%d", SYSCPUS+1, get_nprocs()-1);

		// no mask specified, use default
		if (AFFINITY_UNSPECIFIED == set->setaffinity
			&& !set->affinity){
			printDbg(PIN2 "Using default setting, affinity '%d' and '%s'.\n", SYSCPUS, defafin);

			set->affinity = strdup(defafin);
		}
		else
			// read from file
			set->cpusystemfileprefix = get_string_value_from(global, "affinity", TRUE, defafin);

		free(defafin);
	} // END default affinity block 


	set->gnuplot = get_bool_value_from(global, "gnuplot", TRUE, set->gnuplot);
	set->logsize = get_bool_value_from(global, "logsize", TRUE, set->logsize);
	set->ftrace = get_bool_value_from(global, "ftrace", TRUE, set->ftrace);

}

/// config_set_default(): set default program parameters
///
/// Arguments: - structure to store values in
///
/// Return value: no return value, exits on error
void config_set_default(prgset_t *set) {

	// logging TODO:
	set->logdir = NULL; 
	set->logbasename = NULL;
	set->logsize = 0;

	set->cont_ppidc = NULL;
	set->cont_pidc = NULL;
	set->cont_cgrp = NULL;

	// filepaths virtual file system
	set->procfileprefix = NULL;
	set->cpusetfileprefix = NULL;
	set->cpusystemfileprefix = NULL;

	set->cpusetdfileprefix = NULL;

	// generic parameters
	set->priority=0;
	set->clocksel = 0;
	set->policy = SCHED_OTHER;
	set->quiet = 0;
	set->affother = 0;
	set->setdflag = 0;
	set->interval = TSCAN;
	set->update_wcet = TWCET;
	set->loops = TDETM;
	set->runtime = 0;
	set->psigscan = 0;
	set->trackpids = 0;
	//set->negiszero = 0;
	set->dryrun = 0;
	set->lock_pages = 0;
	set->force = 0;
	set->smi = 0;
	set->rrtime = 0;
	set->use_fifo=0; // TODO

	// runtime values
	set->kernelversion = KV_NOT_SUPPORTED;
	// affinity specification for system vs RT
	set->setaffinity = AFFINITY_UNSPECIFIED;
	set->affinity = NULL;
	set->affinity_mask = NULL;

	// TODO:
	set->gnuplot = 0;
	set->ftrace = 0;

	set->use_cgroup = DM_CGRP;
}

/// parse_config(): parse the json configuration file and push back results
///
/// Arguments: - filename of the configuration file
/// 		   - struct to store the read parameters in
///
/// Return value: void (exits with error if needed)
void parse_config(const char *filename, prgset_t *set, contparm_t *parm)
{

	if (!set) {
		// empty pointer, create and init structure
		if ((set=malloc(sizeof(prgset_t))))
			err_msg("Error allocatinging memory!"); 
		config_set_default(set);	
	}

	char *fn = strdup(filename); // TODO: why?
	struct json_object *root;
	printDbg(PFX "Reading JSON config from %s\n", fn);
	root = json_object_from_file(fn);
	free(fn);

	// root read successfully?
	if (root == NULL) {
		err_msg(PFX "Error while parsing input JSON");
		exit(EXIT_INV_CONFIG);
	}
	
	printDbg(PFX "Successfully parsed input JSON\n");
	printDbg(PFX "root     : %s\n", json_object_to_json_string(root));

	// begin parsing JSON

	// Sections of settings
	{	// program settings block

		struct json_object *global;
		global = get_in_object(root, "global", TRUE);
		if (global)
			printDbg(PFX "global   : %s\n", json_object_to_json_string(global));
		printDbg(PFX "Parsing global\n");
		parse_global(global, set);
		if (global && !json_object_put(global))
			err_msg(PFX "Could not free object!");

	} // END program settings block


	{ // container settings block

		struct json_object *containers;
		containers = get_in_object(root, "containers", FALSE);
		printDbg(PFX "containers    : %s\n", json_object_to_json_string(containers));
		printDbg(PFX "Parsing containers\n");
		parse_containers(containers, parm);
		if (!json_object_put(containers))
			err_msg(PFX "Could not free object!");

	} // END container settings block

	/*
	{ // global resource limits block

		struct json_object *resources;
		resources = get_in_object(root, "resources", TRUE);
		if (resources)
			printDbg(PFX "resources: %s\n", json_object_to_json_string(resources));
		printDbg(PFX "Parsing resources\n");
		parse_resources(resources, parm);
		if (resources && !json_object_put(resources))
			err_msg(PFX "Could not free object!");

	} // END resource limits block
	*/

	// end parsing JSON
	if (!json_object_put(root))
		err_exit(PFX "Could not free objects!");

}
