// NOTE -> inspired by rt-app, may need licence header
// Will change a lot. let's do a first change and then an estimation
// http://json-c.github.io/json-c/json-c-0.13.1/doc/html/json__object_8h.html#a8c56dc58a02f92cd6789ba5dcb9fe7b1

#include "parse_config.h"

#include <stdio.h>			// STD IO print and file operations
#include <string.h>			// string operations
#include <stdbool.h>		// for BOOL definition and operation
#include <fcntl.h>			// file control 
#include <sched.h>			// scheduler functions
#include <errno.h>			// error numbers and strings
#include <json-c/json.h>	// libjson-c for parsing
#include <sys/sysinfo.h>	// system general information

// static library includes
#include "kernutil.h"		// kernel util data types and functions
#include "parse_func.h"		// macros for json parsing
#include "cmnutil.h"		// common definitions and functions

#undef PFX
#define PFX "[json] "
#define JSON_FILE_BUF_SIZE 4096
#define DEFAULT_MEM_BUF_SIZE (4 * 1024 * 1024)


/// parse_resource_data(): extract parameter values from JSON tokens for resource limits
///
/// Arguments: - JSON object of tree containing resource information
/// 		   - structure to store values in, resources of section (pid,container,global)
///
/// Return value: no return value, exits on error
static void parse_resource_data(struct json_object *obj,
		  struct sched_rscs **data){

	*data = malloc(sizeof(struct sched_rscs));
	(*data)->affinity = get_int_value_from(obj, "affinity", TRUE, INT_MIN);
	(*data)->affinity_mask = NULL;
	(*data)->rt_timew = get_int_value_from(obj, "rt-soft", TRUE, -1);
	(*data)->rt_time = get_int_value_from(obj, "rt-hard", TRUE, -1);
	(*data)->mem_dataw = get_int_value_from(obj, "data-soft", TRUE, -1);
	(*data)->mem_data = get_int_value_from(obj, "data-hard", TRUE, -1);
}

/// parse_scheduling_data(): extract parameter values from JSON tokens for resource limits
///
/// Arguments: - JSON object of tree containing scheduling information
/// 		   - structure to store values in, scheduling of section (pid,container,global)
///
/// Return value: no return value, exits on error
static void parse_scheduling_data(struct json_object *obj,
		  struct sched_attr **data){

	*data = malloc(sizeof(struct sched_attr));

	(*data)->size = 48;
	{  // char policy block

		char *policy;
		policy = get_string_value_from(obj, "policy",
						   TRUE, "default");
		if (string_to_policy(policy, &(*data)->sched_policy)) {
			err_msg(PFX "Invalid policy %s", policy);
			exit(EXIT_INV_CONFIG);
		}
		free(policy);

	} // END policy block
	
	(*data)->sched_flags = get_int_value_from(obj, "flags", TRUE, 0);
	(*data)->sched_nice = get_int_value_from(obj, "nice", TRUE, 0);
	(*data)->sched_priority = get_int_value_from(obj, "prio", TRUE, 0);
	(*data)->sched_runtime = get_int64_value_from(obj, "runtime", TRUE, 0);
	(*data)->sched_deadline = get_int64_value_from(obj, "deadline", TRUE, (*data)->sched_runtime);
	(*data)->sched_period = get_int64_value_from(obj, "period", TRUE, (*data)->sched_deadline);
}

/// parse_pid_data(): extract parameter values from JSON tokens for pid
///
/// Arguments: - JSON object of tree containing pid information
///			   - index in array list
/// 		   - structure to store values in, for this pid
///			   - structure containing container configuration, parent
///			   - structure containing image configuration, parent
///
/// Return value: no return value, exits on error
static void parse_pid_data(struct json_object *obj, int index, 
		pidc_t *data, cont_t *cont, img_t * img, containers_t *conts)
{

	printDbg(PFX "Parsing PID [%d]\n", index);

	if (!cont) 
		cont = (cont_t *) img; // overwrite default reference if no cont set
								// have (almost) the same structure

	data->psig = get_string_value_from(obj, "cmd", FALSE, NULL);

	{
		struct json_object *attr;
		attr = get_in_object(obj, "params", TRUE);
		if (attr)
			parse_scheduling_data(attr,	&data->attr);
		else if (cont) {
			// set to container default
			data->attr = cont->attr;
			data->status |= MSK_STATSHAT;
			printDbg(PIN "defaulting to container scheduling settings\n");
		}
		else if (img) {
			data->attr = img->attr;
			data->status |= MSK_STATSHAT;
			printDbg(PIN "defaulting to image scheduling settings\n");
		}
		else {
			data->attr = conts->attr;
			data->status |= MSK_STATSHAT;
			printDbg(PIN "defaulting to global scheduling settings\n");
		}
	}

	{
		struct json_object *rscs;
		rscs = get_in_object(obj, "res", TRUE);
		if (rscs)
			parse_resource_data(rscs, &data->rscs);
		else if (cont) { 
			// set to container default
			data->rscs = cont->rscs;
			data->status |= MSK_STATSHRC;
			printDbg(PIN "defaulting to container resource settings\n");
		}
		else if (img) {
			data->rscs = img->rscs;
			data->status |= MSK_STATSHRC;
			printDbg(PIN "defaulting to image scheduling settings\n");
		}
		else {
			data->rscs = conts->rscs;
			data->status |= MSK_STATSHRC;
			printDbg(PIN "defaulting to global scheduling settings\n");
		}
	}

	data->cont = cont;
	data->img = img;
}

/// parse_container_data(): extract parameter values from JSON tokens for container
///
/// Arguments: - JSON object of tree containing container data
///			   - index in array list
/// 		   - structure to store values in, for this container
///			   - structure containig containers (all) information, parent
///			   - image of container, NULL if unknown
///
/// Return value: no return value, exits on error
static void parse_container_data(struct json_object *obj, int index, 
		cont_t *data, containers_t *conts, img_t * img)
{

	printDbg(PFX "Parsing container [%d]\n", index);

	data->contid = get_string_value_from(obj, "contid", TRUE, "--");

	{
		struct json_object *attr;
		attr = get_in_object(obj, "params", TRUE);
		if (attr)
			parse_scheduling_data(attr,	&data->attr);
		else if (img) {
			data->attr = img->attr;
			data->status |= MSK_STATSHAT;
			printDbg(PIN "defaulting to image scheduling settings\n");
		}
		else {
			data->attr = conts->attr;
			data->status |= MSK_STATSHAT;
			printDbg(PIN "defaulting to global scheduling settings\n");
		}
	}

	{
		struct json_object *rscs;
		rscs = get_in_object(obj, "res", TRUE);
		if (rscs)
			parse_resource_data(rscs, &data->rscs);
		else if (img) { 
			data->rscs = img->rscs;
			data->status |= MSK_STATSHRC;
			printDbg(PIN "defaulting to image scheduling settings\n");
		}
		else {
			data->rscs = conts->rscs;
			data->status |= MSK_STATSHRC;
			printDbg(PIN "defaulting to global scheduling settings\n");
		}
	}

	{// now parse PIDs before you exit :)
		struct json_object *pidslist;
		struct json_object *pidobj;
		int idx = 0;

		if ((pidslist = get_in_object(obj, "pids", TRUE)))
			while ((pidobj = json_object_array_get_idx(pidslist, idx))){

				// chain new element to container pids list, pointing to this pid
				push((void**)&conts->pids, sizeof(pidc_t));
				push((void**)&data->pids, sizeof(pids_t));
				data->pids->pid = conts->pids;

				parse_pid_data(pidobj, idx, conts->pids, data, img, conts);
				
				idx++;
			}
	}

	data->img = img;
} 

/// parse_containers(): extract parameter values from JSON tokens for containers
///
/// Arguments: - JSON object of tree containing container array
/// 		   - structure to store values in
///			   - structure containig containers (all) information, parent
///			   - image of container, NULL if unknown
///
/// Return value: no return value, exits on error
static void parse_containers(struct json_object *containers, containers_t *conts,
		img_t * img)
{
	
	printDbg(PFX "Parsing containers section\n");
	if (json_type_array == json_object_get_type(containers)) {
		// scan trough array_list

		{ // block, container and pid count

			struct json_object *contobj;
			struct json_object *pidobj;
			int idx = 0;

			while ((contobj = json_object_array_get_idx (containers, idx))) {

				// for each container check pid entries count							
				if ((pidobj = get_in_object (contobj, "pids", TRUE))){
					conts->nthreads += json_object_array_length(pidobj);
				}

				// update conuters
				conts->num_cont++;
				idx++;
			}
			printDbg(PFX "Found %d thread configurations in %d containers\n",
				conts->nthreads, conts->num_cont);
		} // END container and pid count block

		{ // container parse block
			/*
			 * Parse thread data of defined containers so that we can use them later
			 * when creating the containers at main() and fork event.
			 */

			struct json_object *contobj;
			int idx = 0;

			while ((contobj = json_object_array_get_idx (containers, idx))) {
				push((void**)&conts->cont, sizeof(cont_t)); // add new element to the head
				parse_container_data(contobj, idx, conts->cont, conts, img); 

				// update counters
				idx++;
			}
		} // END container parse block
	}
	else {
		err_msg(PFX "Error while parsing input JSON, containers type wrong!");
		exit(EXIT_INV_CONFIG);
	}
}

/// parse_image_data(): extract parameter values from JSON tokens for image
///
/// Arguments: - JSON object of tree containing image data
///			   - index in array list
/// 		   - structure to store values in, for this image
///			   - structure containig containers (all) information, parent
///
/// Return value: no return value, exits on error
static void parse_image_data(struct json_object *obj, int index, 
		img_t *data, containers_t *conts)
{

	printDbg(PFX "Parsing image [%d]\n", index);

	data->imgid = get_string_value_from(obj, "imgid", TRUE, "--");

	{
		struct json_object *attr;
		attr = get_in_object(obj, "params", TRUE);
		if (attr)
			parse_scheduling_data(attr,	&data->attr);
		else {
			data->attr = conts->attr;
			data->status |= MSK_STATSHAT;
			printDbg(PIN "defaulting to global scheduling settings\n");
		}
	}

	{
		struct json_object *rscs;
		rscs = get_in_object(obj, "res", TRUE);
		if (rscs)
			parse_resource_data(rscs, &data->rscs);
		else {
			data->rscs = conts->rscs;
			data->status |= MSK_STATSHRC;
			printDbg(PIN "defaulting to global scheduling settings\n");
		}
	}

	{// now parse contained Pids
		struct json_object *pidslist;
		struct json_object *pidobj;
		int idx = 0;

		if ((pidslist = get_in_object(obj, "pids", TRUE)))
			while ((pidobj = json_object_array_get_idx(pidslist, idx))){

				// chain new element to image pids list, pointing to this pid
				push((void**)&conts->pids, sizeof(pidc_t));
				push((void**)&data->pids, sizeof(pids_t));
				data->pids->pid = conts->pids;

				parse_pid_data(pidobj, idx, conts->pids, NULL, data, conts);
				
				idx++;
			}
	} // END parse contained Pids

	// parse contained containers

	{ // container settings block

		struct json_object *containers;
		containers = get_in_object(obj, "cont", TRUE);
		if (containers) {
			printDbg(PIN "Parsing image containers\n");
			parse_containers(containers, conts, data);
		}

	} // END container settings block
} 

/// parse_images(): extract parameter values from JSON tokens for container images
///
/// Arguments: - JSON object of tree containing image array
/// 		   - structure to store values in
///			   - structure containig containers (all) information, parent
///
/// Return value: no return value, exits on error
static void parse_images(struct json_object *images, containers_t *conts)
{
	
	printDbg(PFX "Parsing images section\n");
	if (json_type_array == json_object_get_type(images)) {
		// scan trough array_list

		{ // block, image and pid count

			struct json_object *contobj;
			struct json_object *pidobj;
			int idx = 0;


			while ((contobj = json_object_array_get_idx (images, idx))) {

				// for each image check pid entries count							
				if ((pidobj = get_in_object (contobj, "pids", TRUE))){
					conts->nthreads += json_object_array_length(pidobj);
				}

				// update counters
				conts->num_cont++;
				idx++;
			}
			printDbg(PFX "Found %d thread configurations in %d images\n",
				conts->nthreads, conts->num_cont);
		} // END image and PID count block

		{ // image parse block
			/*
			 * Parse thread data of defined images so that we can use them later
			 * when creating the images at main() and fork event.
			 */

			struct json_object *contobj;
			int idx = 0;

			while ((contobj = json_object_array_get_idx (images, idx))) {
				push((void**)&conts->img, sizeof(img_t)); // add new element to the head
				parse_image_data(contobj, idx, conts->img, conts); 

				// update counters
				idx++;
			}
		} // END image parse block
	}
	else {
		err_msg(PFX "Error while parsing input JSON, images type wrong!");
		exit(EXIT_INV_CONFIG);
	}
}

/// parse_dockerfileprefix(): extract parameter values from JSON tokens
///
/// Arguments: - structure storing program values
///
/// Return value: no return value, exits on error
void parse_dockerfileprefix(prgset_t *set)
{
	set->cpusetdfileprefix = malloc(strlen(set->cgroupfileprefix) + strlen(CGRP_CSET) + strlen(set->cont_cgrp)+1);
	if (!set->cpusetdfileprefix)
		err_exit_n(errno, "Could not allocate memory");

	set->cpusetdfileprefix = strcat(strcat (strcpy(set->cpusetdfileprefix, set->cgroupfileprefix), CGRP_CSET), set->cont_cgrp);
}

/// parse_global(): extract parameter values from JSON tokens
///
/// Arguments: - JSON object of tree containing global configuration
/// 		   - structure to store values in
///
/// Return value: no return value, exits on error
static void parse_global(struct json_object *global, prgset_t *set)
{

	printDbg(PFX "Parsing global section\n");
	if (!global) {
		printDbg(PFX " No global section Found: Use default value\n");

		// NOTE: errors here occur only if out of memory!
		// logging
		if (!(set->logdir = strdup("./")) || 
			!(set->logbasename = strdup("orchestrator.txt")))
			err_exit_n(errno, "Can not set parameter");

		// signatures and folders
		if (!set->cont_ppidc)
			if (!(set->cont_ppidc = strdup(CONT_PPID)))
				err_exit_n(errno, "Can not set parameter");
		if (!set->cont_pidc)
			if (!(set->cont_pidc = strdup(CONT_PID)))
				err_exit_n(errno, "Can not set parameter");
		if (!set->cont_cgrp)
			if (!(set->cont_cgrp = strdup(CGRP_DCKR)))
				err_exit_n(errno, "Can not set parameter");

		// filepaths virtual file system
		if (!(set->procfileprefix = strdup("/proc/sys/kernel/")) ||
			!(set->cgroupfileprefix = strdup("/sys/fs/cgroup/")) ||
			!(set->cpusystemfileprefix = strdup("/sys/devices/system/cpu/")))
			err_exit_n(errno, "Can not set parameter");

		parse_dockerfileprefix(set);

		// affinity default setting
		if (!set->affinity){
			char *defafin;
			if (!(defafin = malloc(22))) // has never been set
				err_exit("could not allocate memory!");

			(void)sprintf(defafin, "%d-%d", SYSCPUS+1, get_nprocs()-1);
			// no mask specified, use default
			set->affinity = strdup(defafin);
			free(defafin);
		}

		{
			char * numastr = malloc (5);
			if (!(numastr))
					err_exit("could not allocate memory!");
			if (-1 != numa_available()) {
				int numanodes = numa_max_node();

				(void)sprintf(numastr, "0-%d", numanodes);
			}
			else{
				warn("NUMA not enabled, defaulting to memory node '0'");
				// default NUMA string
				(void)sprintf(numastr, "0");
			}
			set->numa = strdup(numastr);
			free(numastr);
		}
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
	if (!set->cgroupfileprefix)
		set->cgroupfileprefix = get_string_value_from(global, "sys_cgroup", TRUE,
		"/sys/fs/cgroup/");
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
		set->cont_cgrp = get_string_value_from(global, "cont_cgrp", TRUE, CGRP_DCKR);
		parse_dockerfileprefix(set);
	}

	set->priority = get_int_value_from(global, "priority", TRUE, set->priority);
	set->clocksel = get_int_value_from(global, "clock", TRUE, set->clocksel);

	if (!set->policy)	// already been changed by parameter?
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
	//kernelversion -> runtime parameter

	{ // affinity selection switch block
		char *setaffinity;

		setaffinity = get_string_value_from(global, "setaffinity",
					       TRUE, "unspecified");

		// function to evaluate string value!
		set->setaffinity = string_to_affinity(setaffinity);
		free(setaffinity);

	} // END affinity selection switch block

	{  // default affinity mask and selection block

		char *defafin;
		if (!(defafin = malloc(22))) // has never been set
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
			set->affinity = get_string_value_from(global, "affinity", TRUE, defafin);

		free(defafin);
	} // END default affinity block 

	{
		char * numastr = malloc (5);
		if (!(numastr))
				err_exit("could not allocate memory!");
		if (-1 != numa_available()) {
			int numanodes = numa_max_node();

			(void)sprintf(numastr, "0-%d", numanodes);
		}
		else{
			warn("NUMA not enabled, defaulting to memory node '0'");
			// default NUMA string
			(void)sprintf(numastr, "0");
		}
		set->numa = get_string_value_from(global, "numa", TRUE, numastr);
		free(numastr);
	}

	set->ftrace = get_bool_value_from(global, "ftrace", TRUE, set->ftrace);
	set->ptresh = get_double_value_from(global, "ptresh", TRUE, set->ptresh);

}

/// config_set_default(): set default program parameters
///
/// Arguments: - structure to store values in
///
/// Return value: no return value, exits on error
void parse_config_set_default(prgset_t *set) {

	// logging
	set->logdir = NULL; 
	set->logbasename = NULL;

	set->cont_ppidc = NULL;
	set->cont_pidc = NULL;
	set->cont_cgrp = NULL;

	// filepaths virtual file system
	set->procfileprefix = NULL;
	set->cgroupfileprefix = NULL;
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

	set->dryrun = 0;
	set->blindrun = 0;
	set->lock_pages = 0;
	set->force = 0;
	set->smi = 0;
	set->rrtime = 0;

	// runtime values
	set->kernelversion = KV_NOT_SUPPORTED;
	// affinity specification for system vs RT
	set->setaffinity = AFFINITY_UNSPECIFIED;
	set->affinity = NULL;
	set->affinity_mask = NULL;
	set->numa = NULL;

	set->ftrace = 0;

	set->use_cgroup = DM_CGRP;
	set->sched_mode = SM_STATIC;
	set->ptresh = 0.9;
}

/// parse_config(): parse the JSON configuration and push back results
///
/// Arguments: - filename of the configuration file
/// 		   - struct to store the read parameters in
///
/// Return value: void (exits with error if needed)
static void parse_config(struct json_object *root, prgset_t *set, containers_t *conts)
{

	// if parameter object is empty, set it
	if (!set) {
		// empty pointer, create and init structure
		if ((set=malloc(sizeof(prgset_t))))
			err_msg("Error allocating memory!");
		parse_config_set_default(set);	
	}

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

	} // END program settings block

	{ // global scheduling parameters, default

		struct json_object *scheduling;
		scheduling = get_in_object(root, "scheduling", TRUE);
		if (scheduling)
			printDbg(PFX "scheduling: %s\n", json_object_to_json_string(scheduling));
		printDbg(PFX "Parsing scheduling\n");
		parse_scheduling_data(scheduling, &conts->attr);

	} // END global scheduling parameters, default

	{ // global resource limits block

		struct json_object *resources;
		resources = get_in_object(root, "resources", TRUE);
		if (resources)
			printDbg(PFX "resources: %s\n", json_object_to_json_string(resources));
		printDbg(PFX "Parsing resources\n");
		parse_resource_data(resources, &conts->rscs);

	} // END resource limits block

	{ // images settings block

		struct json_object *images;
		images = get_in_object(root, "images", TRUE);
		if (images) {
			printDbg(PFX "images    : %s\n", json_object_to_json_string(images));
			printDbg(PFX "Parsing images\n");
			parse_images(images, conts);
		}

	} // END images settings block

	{ // container settings block

		struct json_object *containers;
		containers = get_in_object(root, "containers", TRUE);
		if (containers) {
			printDbg(PFX "containers    : %s\n", json_object_to_json_string(containers));
			printDbg(PFX "Parsing containers\n");
			parse_containers(containers, conts, NULL);
		}

	} // END container settings block

	{ // PIDs settings block
		struct json_object *pidslist;
		struct json_object *pidobj;
		int idx = 0;

		if (((pidslist = get_in_object(root, "pids", TRUE)))) {
			printDbg(PFX "pids    : %s\n", json_object_to_json_string(pidslist));
			printDbg(PFX "Parsing pids\n");

			while ((pidobj = json_object_array_get_idx(pidslist, idx))){
				// chain new element to image pids list, pointing to this pid
				push((void**)&conts->pids, sizeof(pidc_t));
				parse_pid_data(pidobj, idx, conts->pids, NULL, NULL, conts);
				idx++;
			}
		}
	} // END PIDs settings block

}

/// parse_config_pipe(): parse the JSON configuration from a pipe until EOF
///
/// Arguments: - pipe to the input
/// 		   - structure to store the read parameters in
///
/// Return value: void (exits with error if needed)
void parse_config_pipe(FILE *inpipe, prgset_t *set, containers_t *conts) {
	size_t in_length;
	char buf[JSON_FILE_BUF_SIZE];
	struct json_object *js;
	printDbg(PFX "Reading JSON config from pipe/stdin...\n");

	in_length = fread(buf, sizeof(char), JSON_FILE_BUF_SIZE, inpipe);
	buf[in_length] = '\0';
	js = json_tokener_parse(buf);
	parse_config(js, set, conts);

	// end parsing JSON
	if (!json_object_put(js))
		err_exit(PFX "Could not free objects!");
}

/// parse_config_stdin(): parse the JSON configuration from stdin until EOF
///
/// Arguments: - structure to store the read parameters in
///
/// Return value: void (exits with error if needed)
void parse_config_stdin(prgset_t *set, containers_t *conts) {
	parse_config_pipe(stdin, set, conts);
}

/// parse_config_file(): parse the JSON configuration from file and push back results
///
/// Arguments: - filename of the configuration file
/// 		   - structure to store the read parameters in
///
/// Return value: void (exits with error if needed)
void parse_config_file (const char *filename, prgset_t *set, containers_t *conts) {
	char *fn = strdup(filename);
	struct json_object *js;
	printDbg(PFX "Reading JSON config from %s\n", fn);
	js = json_object_from_file(fn);
	free(fn);
	parse_config(js, set, conts);

	// end parsing JSON
	if (!json_object_put(js))
		err_exit(PFX "Could not free objects!");
}

