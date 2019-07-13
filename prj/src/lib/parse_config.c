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

// static library includes
#include "kernutil.h"		// kernel util data types and functions

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
			info(PIN "key: %s <default> %d", key, def_value);\
			return def_value;				\
		} else 						\
			err_exit_n(EXIT_INV_CONFIG, PFX "Key %s not found", key);	\
	}								\
} while(0)

/* same as before, but for string, for which we need to strdup in the
 * default value so it can be a literal */
#define set_default_if_needed_str(key, value, have_def, def_value) do {	\
	if (!value) {							\
		if (have_def) {						\
			if (!def_value) {				\
				info(PIN "key: %s <default> NULL", key);\
				return NULL;				\
			}						\
			info(PIN "key: %s <default> %s",		\
				  key, def_value);			\
			return strdup(def_value);			\
		} else 						\
			err_exit_n(EXIT_INV_CONFIG, PFX "Key %s not found", key);	\
	}								\
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
		err_exit_n(EXIT_INV_CONFIG, "%s", json_object_to_json_string(parent));
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
	if (!nullable && !ret)
		err_exit_n(EXIT_INV_CONFIG, PFX "Error while parsing config\n" PFL);

	if (!nullable && strcmp(json_object_to_json_string(to), "null") == 0) 
		err_exit_n(EXIT_INV_CONFIG, PFX "Cannot find key %s", what);
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
	info(PIN "key: %s, value: %d, type <int>", key, i_value);
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
	info(PIN "key: %s, value: %d, type <bool>", key, b_value);
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
		info(PIN "key: %s, value: NULL, type <string>", key);
		return NULL;
	}
	assure_type_is(value, where, key, json_type_string);
	s_value = strdup(json_object_get_string(value));
	info(PIN "key: %s, value: %s, type <string>", key, s_value);
	return s_value;
}

/*
static void init_mutex_resource(rtapp_resource_t *data, const parm_t *parm)
{
	info(PIN3 "Init: %s mutex", data->name);

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
	info(PIN3 "Init: %s timer", data->name);
	data->res.timer.init = 0;
	data->res.timer.relative = 1;
}

static void init_cond_resource(rtapp_resource_t *data, const parm_t *parm)
{
	info(PIN3 "Init: %s wait", data->name);

	pthread_condattr_init(&data->res.cond.attr);
	pthread_cond_init(&data->res.cond.obj,
			&data->res.cond.attr);
}

static void init_membuf_resource(rtapp_resource_t *data, const parm_t *parm)
{
	info(PIN3 "Init: %s membuf", data->name);

	data->res.buf.ptr = malloc(parm->mem_buffer_size);
	data->res.buf.size = parm->mem_buffer_size;
}

static void init_iodev_resource(rtapp_resource_t *data, const parm_t *parm)
{
	info(PIN3 "Init: %s io device", data->name);

	data->res.dev.fd = open(parm->io_device, O_CREAT | O_WRONLY, 0644);
}

static void init_barrier_resource(rtapp_resource_t *data, const parm_t *parm)
{
	info(PIN3 "Init: %s barrier", data->name);

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

	info(PFX "Parsing resources %s [%d]", name, idx);

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

	info(PIN2 "Add new resource %s [%d] type %d", name, idx, type);

	table->nresources++;

	size = sizeof(rtapp_resources_t) + sizeof(rtapp_resource_t) * table->nresources;

	*resources_table = realloc(*resources_table, size);

	if (!*resources_table) {
		log_error("Failed to allocate memory for resource %s", name);
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

	info(PFX "Parsing resource section");

	/*
	 * Create at least an "empty" struct that will then be filled either will
	 * parsing resources table or when parsing task's event
	 *//*
	parm->resources = malloc(sizeof(rtapp_resources_t));
	if (!parm->resources) {
		log_error("Failed to allocate memory for resources");
		exit(EXIT_FAILURE);
	}

	parm->resources->nresources = 0;

	if (!resources) {
		info(PFX "No resource section Found");
		return;
	}

	if (json_object_is_type(resources, json_type_object)) {
		parm->resources->nresources = 0;
		foreach(resources, entry, key, val, idx) {
			parm->resources->nresources++;
		}

		info(PFX "Found %d Resources", parm->resources->nresources);
		size = sizeof(rtapp_resources_t) + sizeof(rtapp_resource_t) * parm->resources->nresources;

		parm->resources = realloc(parm->resources, size);
		if (!parm->resources) {
				log_error("Failed to allocate memory for resources");
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
	info(PIN "get_resource_index %d events", nresources);

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

		info(PIN2 "type %d duration %d", data->type, data->duration);
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

		info(PIN2 "type %d count %d", data->type, data->count);
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

		info(PIN2 "type %d target %s [%d]", data->type, rdata->name, rdata->index);
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

		info(PIN2 "type %d target %s [%d]", data->type, rdata->name, rdata->index);
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

		info(PIN2 "type %d target %s [%d] mutex %s [%d]", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
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

		info(PIN2 "type %d target %s [%d] %d users so far", data->type, rdata->name, rdata->index, rdata->res.barrier.waiting);
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

		info(PIN2 "type %d target %s [%d] period %d", data->type, rdata->name, rdata->index, data->duration);
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

		info(PIN2 "type %d target %s [%d] mutex %s [%d]", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
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

		info(PIN2 "type %d target %s [%d] mutex %s [%d]", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
		return;
	}

	if (!strncmp(name, "yield", strlen("yield"))) {
		data->type = rtapp_yield;
		info(PIN2 "type %d", data->type);
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
			log_error("Failed to duplicate ref");
			exit(EXIT_FAILURE);
		}

		info(PIN2 "type %d target %s [%d]", data->type, rdata->name, rdata->index);
		return;
	}

	log_error(PIN2 "Resource %s not found in the resource section !!!", ref);
	log_error(PIN2 "Please check the resource name or the resource section");

unknown_event:
	data->duration = 0;
	data->type = rtapp_run;
	log_error(PIN2 "Unknown or mismatch %s event type !!!", name);

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
	info(PIN "key: cpus %s", data->cpuset_str);
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

	info(PFX "Parsing phase");

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

	info(PIN "Found %d events", data->nbevents);

	data->events = malloc(data->nbevents * sizeof(event_data_t));

	/* Parse events *//*
	i = 0;
	foreach(obj, entry, key, val, idx) {
		if (obj_is_event(key)) {
			info(PIN "Parsing event %s", key);
			parse_task_event_data(key, val, &data->events[i], tdata, parm);
			i++;
		}
	}
	parse_cpuset_data(obj, &data->cpu_data);
	data->sched_data = parse_sched_data(obj, -1);
	data->taskgroup_data = parse_taskgroup_data(obj);
}

static void
parse_task_data(char *name, struct json_object *obj, int index,
		  thread_data_t *data, parm_t *parm)
{
	struct json_object *phases_obj, *resources;

	info(PFX "Parsing task %s [%d]", name, index);

	/* common and defaults *//*
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
		log_error("Failed to allocate memory for local resources");
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

		info(PIN "Parsing phases section");
		data->nphases = 0;
		foreach(phases_obj, entry, key, val, idx) {
			data->nphases++;
		}

		info(PIN "Found %d phases", data->nphases);
		data->phases = malloc(sizeof(phase_data_t) * data->nphases);
		foreach(phases_obj, entry, key, val, idx) {
			info(PIN "Parsing phase %s", key);
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
	data->curr_taskgroup_data = NULL;
}

static void
parse_containers(struct json_object *containers, parm_t *parm)
{
	/* used in the foreach macro *//*
	struct lh_entry *entry; char *key; struct json_object *val; int idx;

	int i = 0;
	int instance;

	info(PFX "Parsing containers section");
	parm->nthreads = 0;
	parm->num_tasks = 0;
	foreach(containers, entry, key, val, idx) {
		instance = get_int_value_from(val, "instance", TRUE, 1);
		parm->nthreads += instance;

		parm->num_tasks++;
	}

	info(PFX "Found %d threads of %d containers", parm->nthreads, parm->num_tasks);

	/*
	 * Parse thread data of defined containers so that we can use them later
	 * when creating the containers at main() and fork event.
	 *//*
	parm->threads_data = malloc(sizeof(thread_data_t) * parm->num_tasks);
	foreach (containers, entry, key, val, idx)
		parse_task_data(key, val, -1, &parm->threads_data[i++], parm);
}
*/

/// parse_global(): extract parameter values from JSON tokens
///
/// Arguments: - json object of tree containing global configuration
/// 		   - structure to store values in
///
/// Return value: no return value, exits on error
static void parse_global(struct json_object *global, prgset_t *set)
{
	char *policy, *tmp_str;
	struct json_object *tmp_obj;
	int scan_cnt;

	info(PFX "Parsing global section");
	if (!global) {
		info(PFX " No global section Found: Use default value");

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
	set->logdir = get_string_value_from(global, "logdir", TRUE, 
		set->logdir ? set->logdir : "./");
	set->logbasename = get_string_value_from(global, "log_basename", TRUE,
		set->logbasename ? set->logbasename : "orchestrator.txt");

	// signatures and folders
	set->cont_ppidc = get_string_value_from(global, "cont_ppidc", TRUE,
		set->cont_ppidc ? set->cont_ppidc : CONT_PPID);
	set->cont_pidc = get_string_value_from(global, "cont_pidc", TRUE,
		set->cont_pidc ? set->cont_pidc : CONT_PID);

	if (!set->cont_cgrp){
		set->cont_cgrp = get_string_value_from(global, "cont_cgrp", TRUE, CONT_DCKR);

		set->cpusetdfileprefix = malloc(strlen(set->cpusetfileprefix) + strlen(set->cont_cgrp)+1);
		if (!set->cpusetdfileprefix)
			err_exit_n(errno, "Could not allocate memory");

		*set->cpusetdfileprefix = '\0'; // set first chat to null
		set->cpusetdfileprefix = strcat(strcat(set->cpusetdfileprefix, set->cpusetfileprefix), set->cont_cgrp);		
	}


	// filepaths virtual file system
	set->procfileprefix = get_string_value_from(global, "prc_kernel", TRUE,
		"/proc/sys/kernel/");
	set->cpusetfileprefix = get_string_value_from(global, "sys_cpuset", TRUE,
		"/sys/fs/cgroup/cpuset/");
	set->cpusystemfileprefix = get_string_value_from(global, "sys_cpu", TRUE,
		"/sys/devices/system/cpu/";

	set->priority = get_int_value_from(global, "duration", TRUE, set->priority);

	set->interval = get_int_value_from(global, "interval", TRUE, -1);
	set->gnuplot = get_bool_value_from(global, "gnuplot", TRUE, 0);
	policy = get_string_value_from(global, "default_policy",
				       TRUE, "SCHED_OTHER");

	set->logsize = 0;

/*	if (string_to_policy(policy, &set->policy) == 0) {
		log_critical(PFX "Invalid policy %s", policy);
		exit(EXIT_INV_CONFIG);
	}
	/*
	 * get_string_value_from allocate the string so with have to free it
	 * once useless
	 *//*
	free(policy);

	tmp_obj = get_in_object(global, "calibration", TRUE);
	if (tmp_obj == NULL) {
		/* no setting ? Calibrate CPU0 *//*
		set->calib_cpu = 0;
		set->calib_ns_per_loop = 0;
		log_error("missing calibration setting force CPU0");
	} else {
		if (json_object_is_type(tmp_obj, json_type_int)) {
			/* integer (no " ") detected. *//*
			set->calib_ns_per_loop = json_object_get_int(tmp_obj);
			log_debug("ns_per_loop %d", set->calib_ns_per_loop);
		} else {
			/* Get CPU number *//*
			tmp_str = get_string_value_from(global, "calibration",
					 TRUE, "CPU0");
			scan_cnt = sscanf(tmp_str, "CPU%d", &set->calib_cpu);
			/*
			 * get_string_value_from allocate the string so with have to free it
			 * once useless
			 *//*
			free(tmp_str);
			if (!scan_cnt) {
				log_critical(PFX "Invalid calibration CPU%d", set->calib_cpu);
				exit(EXIT_INV_CONFIG);
			}
			log_debug("calibrating CPU%d", set->calib_cpu);
		}
	}

	tmp_obj = get_in_object(global, "log_size", TRUE);
	if (tmp_obj == NULL) {
		/* no size ? use file system *//*
		set->logsize = -2;
	} else {
		if (json_object_is_type(tmp_obj, json_type_int)) {
			/* integer (no " ") detected. *//*
			/* buffer size is set in MB *//*
			set->logsize = json_object_get_int(tmp_obj) << 20;
			log_notice("Log buffer size fixed to %dMB per threads", (set->logsize >> 20));
		} else {
			/* Get CPU number *//*
			tmp_str = get_string_value_from(global, "log_size",
					 TRUE, "disable");

			if (!strcmp(tmp_str, "disable"))
				set->logsize = 0;
			else if (!strcmp(tmp_str, "file"))
				set->logsize = -2;
			else if (!strcmp(tmp_str, "auto"))
				set->logsize = -2; /* Automatic buffer size computation is not supported yet so we fall back on file system mode *//*
			log_debug("Log buffer set to %s mode", tmp_str);

			/*
			 * get_string_value_from allocate the string so with have to free it
			 * once useless
			 *//*
			free(tmp_str);
		}
	}

	set->ftrace = get_bool_value_from(global, "ftrace", TRUE, 0);
	set->lock_pages = get_bool_value_from(global, "lock_pages", TRUE, 1);
	set->pi_enabled = get_bool_value_from(global, "pi_enabled", TRUE, 0);
	set->io_device = get_string_value_from(global, "io_device", TRUE,
						"/dev/null");
	set->mem_buffer_size = get_int_value_from(global, "mem_buffer_size",
							TRUE, DEFAULT_MEM_BUF_SIZE);
	set->cumulative_slack = get_bool_value_from(global, "cumulative_slack", TRUE, 0);
	*/
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
void parse_config(const char *filename, prgset_t *set, parm_t *parm)
{

	if (!set) {
		// empty pointer, create and init structure
		if ((set=malloc(sizeof(prgset_t))))
			err_msg("Error allocatinging memory!"); 
		config_set_default(set);	
	}

	char *fn = strdup(filename); // TODO: why?
	struct json_object *root;
	info(PFX "Reading JSON config from %s", fn);
	root = json_object_from_file(fn);
	free(fn);

	// root read successfully?
	if (root == NULL) 
		err_exit_n(EXIT_INV_CONFIG, PFX "Error while parsing input JSON");
	
	cont(PFX "Successfully parsed input JSON");
	cont(PFX "root     : %s", json_object_to_json_string(root));

	{ // begin parsing JSON

		// Sections of settings
		struct json_object *global, *containers, *resources;

		// get program settings
		global = get_in_object(root, "global", TRUE);
		if (global)
			cont(PFX "global   : %s", json_object_to_json_string(global));

/*		// get container settings
		containers = get_in_object(root, "containers", FALSE);
		cont(PFX "containers    : %s", json_object_to_json_string(containers));

		// get global resource limits
		resources = get_in_object(root, "resources", TRUE);
		if (resources)
			info(PFX "resources: %s", json_object_to_json_string(resources));
*/
		info(PFX "Parsing global");
		parse_global(global, set);
		json_object_put(global);
	/*	info(PFX "Parsing resources");
		parse_resources(resources, parm);
		json_object_put(resources);
		info(PFX "Parsing containers");
		parse_containers(containers, parm);
		json_object_put(containers);*/
		info(PFX "Free json objects");
	} // end parsing JSON
}
