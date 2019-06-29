#include "schedstat.h"
#include "manage.h"

#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <numa.h>			// numa node ident

// Global variables used here ->

// parameter tree linked list head, resource linked list head
static parm_t * phead;
static struct resTracer * rhead;

static char *fileprefix; // Work variable for local things -> procfs & sysfs

////// TEMP ---------------------------------------------

/// checkUvalue(): verify if task fits into Utilization limits of a resource
///
/// Arguments: resource entry for this cpu, the attr structure of the task
///
/// Return value: 0 = ok, -1 = no space, 1 = ok but recalc base
int checkUvalue(struct resTracer * res, struct sched_attr * par) {
	uint64_t	base = res->basePeriod,
				used = res->usedPeriod;
	int rv = 0;
	
	if (base % par->sched_deadline != 0) {
		// realign periods
		uint64_t max_Value = MAX (base, par->sched_period);

		if (base % 1000 != 0 || par->sched_period % 1000 != 0)
			fatal("Nanosecond resolution periods not supported!\n");
			// temporary solution to avoid very long loops

		while(1) //Alway True
		{
			if(max_Value % base == 0 && max_Value % par->sched_period == 0) 
			{
				break;
			}
			max_Value+= 1000; // add a microsecond..
		}

		used *= max_Value/base;
		base = max_Value;	
		rv=1;
	}

	if (MAX_UL < (double)(used + par->sched_runtime * base/par->sched_period)/(double)(base) )
		rv = -1;

	return rv;
}

void addUvalue(struct resTracer * res, struct sched_attr * par) {
	if (res->basePeriod % par->sched_deadline != 0) {
		// realign periods
		uint64_t max_Value = MAX (res->basePeriod, par->sched_period);

		if (res->basePeriod % 1000 != 0 || par->sched_period % 1000 != 0)
			fatal("Nanosecond resolution periods not supported!\n");
			// temporary solution to avoid very long loops

		while(1) //Alway True
		{
			if(max_Value % res->basePeriod == 0 && max_Value % par->sched_period == 0) 
			{
				break;
			}
			max_Value+= 1000; // add a microsecond..
		}

		res->usedPeriod *= max_Value/res->basePeriod;
		res->basePeriod = max_Value;	

	}

	res->usedPeriod += par->sched_runtime * res->basePeriod/par->sched_period;
}

////// TEMP ---------------------------------------------



/* Function realloc_it() is a wrapper function for standard realloc()
 * with one difference - it frees old memory pointer in case of realloc
 * failure. Thus, DO NOT use old data pointer in anyway after call to
 * realloc_it(). If your code has some kind of fallback algorithm if
 * memory can't be re-allocated - use standard realloc() instead.
 */
static inline void *realloc_it(void *ptrmem, size_t size) {
	void *p = realloc(ptrmem, size);
	if (!p)  {
		free (ptrmem);
		fprintf(stderr, "realloc(): errno=%d\n", errno);
	}
	return p;
}

const char *keys[] = {"cmd", "contid", "params", "policy", "flags", "nice", "prio", "runtime", "deadline", "period", "res", "affinity", "rt-soft", "rt-hard", "data-soft", "data-hard"};

/// extractJSON(): extract parameter values from JSON tokens
///
/// Arguments: js - input token string, t token position, count token count (object)
/// 			depth (mostly for printout only), key - identifies key the parsed value belongs to 
///
/// Return value: number of tokens parsed
static int extractJSON(const char *js, jsmntok_t *t, size_t count, int depth, int key) {
	int i, j, k;

	// TODO: len check and case not matching type, also key > than values

	// base case, no elements in the object
	if (0 == count) {
		warn("JSON: faulty object data! No element count in this object");
		return 0;
	}

	// setting value, primitive
	if (JSMN_PRIMITIVE == t->type) {
		// enter here if a primitive value (int?) has been identified

		if (-1 == key || (t->end - t->start) > JSN_READ) { // no key value yet or value too long
			warn("JSON: faulty key selection data! %.*s", t->end - t->start, js+t->start);
			return 1; // 0 or 1? or count? check?
		}

		if (!phead) {
			warn("JSON: no element! %.*s", t->end - t->start, js+t->start);
			return 1;
		}

		// here len must be shorter than JSN_READ 
		char c[JSN_READ]; // buffer size for tem
		sprintf(c, "%.*s", t->end - t->start, js+t->start);

		switch (key) {

		case 4: // schedule flags
				phead->attr.sched_flags = strtol(c,NULL,10);
				printDbg("JSON: setting scheduler flags to '%ld'", phead->attr.sched_flags);
				break;

		case 5: // schedule niceness
				phead->attr.sched_nice = (uint32_t)strtol(c,NULL,10);
				printDbg("JSON: setting scheduler niceness to '%d'", phead->attr.sched_nice);// PDB
				break;

		case 6: // schedule rt priority
				phead->attr.sched_priority = (uint32_t)strtol(c,NULL,10);
				printDbg("JSON: setting rt-priority to '%d'", phead->attr.sched_priority);
				break;
		case 7: // schedule rt runtime
				phead->attr.sched_runtime = strtol(c,NULL,10);
				printDbg("JSON: setting rt-runtime to '%ld'", phead->attr.sched_runtime);
				break;
		
		case 8: // schedule rt deadline
				phead->attr.sched_deadline = strtol(c,NULL,10);
				printDbg("JSON: setting rt-deadline to '%ld'", phead->attr.sched_deadline);
				break;

		case 9: // schedule rt period
				phead->attr.sched_period = strtol(c,NULL,10);
				printDbg("JSON: setting rt-period to '%ld'", phead->attr.sched_period);
				break;

		case 11: // task affinity
				phead->rscs.affinity = strtol(c,NULL,10);
				printDbg("JSON: setting affinity to '%d'", phead->rscs.affinity);
				break;
		case 12: // rt-time soft limit
				phead->rscs.rt_timew = strtol(c,NULL,10);
				printDbg("JSON: setting rt-time Slimit to '%d'", phead->rscs.rt_timew);
				break;
		case 13: // rt-time hard limit
				phead->rscs.rt_time = strtol(c,NULL,10);
				printDbg("JSON: setting rt-time limit to '%d'", phead->rscs.rt_time);
				break;
		case 14: // data hard limit
				phead->rscs.mem_dataw = strtol(c,NULL,10);
				printDbg("JSON: setting data Slimit to '%d'", phead->rscs.mem_dataw);
				break;
		case 15: // data soft limit
				phead->rscs.mem_data = strtol(c,NULL,10);
				printDbg("JSON: setting data limit to '%d'", phead->rscs.mem_data);
				break;
		}

		printDbg("%.*s", t->end - t->start, js+t->start);
		return 1;
	
	// setting value or label
	} else if (JSMN_STRING == t->type) {
		// here len must be shorter than JSN_READ
		if ((t->end - t->start) > JSN_READ-1){
			warn("JSON: faulty key value! Too long '%.*s'", t->end - t->start, js+t->start);
			return 1; // 0 or 1? or count? check?
		}

		// a key has been identified
		if (0 > key){
			char c[JSN_READ]; // buffer size for temp storage
			size_t len = sizeof(keys)/sizeof(keys[0]); // count of keys

			// copy key to temp string buffer
			sprintf(c ,"%.*s", t->end - t->start, js+t->start);

			for (i=0; i<len; i++) 
				if (!strcasecmp(c, keys[i])){
					// key match.. find value for it
					printDbg("'%.*s': ", t->end - t->start, js+t->start);
					j = 0;					
					j += extractJSON(js, t+1+j, count-j, depth+1, i);   // key evaluation
					return j+1;
					break;
				}
			// if it get's here, key has not been found in list
			warn("NOT FOUND '%.*s'", t->end - t->start, js+t->start);
			return 1;
		}
		else
		{

			if (!phead) { // !! no object defined !!!
				warn("JSON: no element! '%.*s'", t->end - t->start, js+t->start);
				return 1;
			}

			switch (key) {
			
			case 0: // process/command signature found
					sprintf(phead->psig, "%.*s", t->end - t->start, js+t->start);
					printDbg("JSON: setting cmd to '%s'", phead->psig);
					break;

			case 1: // ID signature found
					sprintf(phead->contid, "%.*s", t->end - t->start, js+t->start);
					printDbg("JSON: setting cmd to '%s'", phead->psig);
					break;

			case 3: ; // schedule policy 
					char c[JSN_READ]; // buffer size for tem
					sprintf(c, "%.*s", t->end - t->start, js+t->start);
					phead->attr.sched_policy = string_to_policy(c);
					printDbg("JSON: setting scheduler to '%s'", policy_to_string(phead->attr.sched_policy));
					break;

			}

			// all string values end up here
			printDbg("'%.*s'", t->end - t->start, js+t->start);

			return 1;
		}

	// Process composed objects ( {} }
	} else if (JSMN_OBJECT == t->type) {

		// add a new settings item at head
		// depth 2 is fixed, 0 = main object, 1 = configuration array
		if (2 == depth) {
			printDbg("Adding new item:\n");
			ppush (&phead);
		}

		// printout 
		printDbg("\n");
		j = 0;
		for (i = 0; i < t->size; i++) {
			for (k = 0; k < depth; k++) printDbg("  "); // print depth

			// we evaluate only the key here, value is taken in cascade
			j += extractJSON(js, t+1+j, count-j, depth+1, -1);   // key evaluation

			printDbg("\n");
		}
		return j+1;

	// process arrays ( [] )
	} else if (JSMN_ARRAY == t->type) {
		j = 0;
		printDbg("\n");
		for (i = 0; i < t->size; i++) {
			for (k = 0; k < depth-1; k++) printDbg("  ");
			printDbg("   - ");
			j += extractJSON(js, t+1+j, count-j, depth+1, -1);
			printDbg("\n");
		}
		return j+1;
	}
	return 0;
}


/// readParams(): Read parameters from json file, for static params
///
/// Arguments: - 
///
/// Return value: Code - o for no error - EXIT_SUCCESS
int readParams() {
	int r;
	int eof_expected = 0; // ok to have end of file
	char *js = NULL;
	size_t jslen = 0;
	char buf[BUFSIZ];

	jsmn_parser p;
	jsmntok_t *tok;
	size_t tokcount = 2;

	/* Prepare parser */
	jsmn_init(&p);

	/* Allocate some tokens as a start */
	tok = malloc(sizeof(*tok) * tokcount);
	if (NULL == tok ) {
		fprintf(stderr, "malloc(): errno=%d\n", errno);
		return 3;
	}

	FILE * f = fopen (config, "r");

	if (f){ // !! does not check file existence! inserted into schedstat.h at startup
	for (;;) {
		/* Read another chunk */
		r = fread(buf, 1, sizeof(buf), f);
		if (0 > r) {
			fprintf(stderr, "fread(): %d, errno=%d\n", r, errno);
			fclose (f);
			return 1;
		}
		if (0 == r) {
			fclose (f);
			if (0 != eof_expected) {
				return 0;
			} else {
				fprintf(stderr, "fread(): unexpected EOF\n");
				return 2;
			}
		}

		js = realloc_it(js, jslen + r + 1);
		if (NULL == js) {
			fclose (f);
			return 3;
		}
		strncpy(js + jslen, buf, r);
		jslen = jslen + r;

again:
		r = jsmn_parse(&p, js, jslen, tok, tokcount);
		if (0 > r) {
			if (JSMN_ERROR_NOMEM == r) {
				tokcount = tokcount * 2;
				tok = realloc_it(tok, sizeof(*tok) * tokcount);
				if (NULL == tok) {
					fclose (f);
					return 3;
				}
				goto again;
			}
		} else {
			extractJSON(js, tok, p.toknext, 0, -1);
			eof_expected = 1;
		}
	}}
	else{
		printDbg("\n");
	}

	fclose (f);
	return 0;
}


/// findParams(): returns the matching parameter set for a function
//
/// Arguments: 
///
/// Return value: pointer to parameters
///
int findParams(node_t* node){

	parm_t * curr = phead;

	while (NULL != curr) {
		if(curr->psig && node->psig && !strcmp(curr->psig, node->psig)) {
			node->param = curr;
			return 0;
		}
		if(curr->contid && node->contid && !strncmp(curr->contid, node->contid, 12)) { 
			// 12 is standard docker short signature
			node->param = curr;
			return 0;
		}
		curr = curr->next; 
	}

	printDbg("... parameters not found, creating empty from PID\n");

	// TODO: fix for generic list
	ppush(&phead); // add new empty item
	// HAVE TO KEEP IT EMPTY, no other matches, default = no change

	// assing new parameters
	node->param = phead;
	return -1;

}

static cpu_set_t cset_full; // local static to avoid recomputation.. (may also use affinity_mask? )

/// updateSched(): main function called to verify status of threads
//
/// Arguments: 
///
/// Return value: N/D
///
int updateSched() {

    node_t * current = head;
	int ovr = 0;
	cpu_set_t cset;

	(void)pthread_mutex_lock(&dataMutex);

	while (NULL != current) {
		// skip deactivated tracking items
		if (current->pid<0){
			current=current->next; 
			continue;
		}

		// NEW Entry? Params are not assigned yet. Do it noe and reschedule.
		if (NULL == current->param) {
			// params unassigned
			if (!quiet)
				(void)printf("\n");
			info("new pid in list %d\n", current->pid);

			if (!findParams(current)) { // parameter set found in list -> assign and update
				// precompute affinity
				if (0 <= current->param->rscs.affinity) {
					// cpu affinity defined to one cpu?
					CPU_ZERO(&cset);
					CPU_SET(current->param->rscs.affinity & ~(SCHED_FAFMSK), &cset);
				}
				else {
					// cpu affinity to all
					cset = cset_full;
				}

				if (SCHED_OTHER != current->attr.sched_policy) { 
					// only if successful
					if (!current->psig) 
						current->psig = current->param->psig;
					if (!current->contid)
						current->contid = current->param->contid;

					// TODO: track failed scheduling update?

					// update CGroup setting of container if in CGROUP mode
					if (DM_CGRP == use_cgroup && 
						((0 <= current->param->rscs.affinity) & ~(SCHED_FAFMSK))) {

						char affinity[5];
						(void)sprintf(affinity, "%d", current->param->rscs.affinity);

						cont( "reassigning %.12s's CGroups CPU's to %s\n", current->contid, affinity);
						if ((fileprefix=malloc(strlen(cpusetdfileprefix))
								+ strlen(current->contid)+1)) {
							fileprefix[0] = '\0';   // ensures the memory is an empty string
							// copy to new prefix
							fileprefix = strcat(strcat(fileprefix,cpusetdfileprefix), current->contid);		
							
							if (setkernvar(fileprefix, "/cpuset.cpus", affinity)){
								warn("Can not set cpu-affinity\n");
							}
						}
						else 
							warn("malloc failed!\n");

						free (fileprefix);
					}
					// should it be else??
					else {

						// add pid to docker CGroup
						char pid[5];
						(void)sprintf(pid, "%d", current->pid);

						if (setkernvar(cpusetdfileprefix , "tasks", pid)){
							printDbg( KMAG "Warn!" KNRM " Can not move task %s\n", pid);
						}

						// Set affinity
						if (sched_setaffinity(current->pid, sizeof(cset), &cset ))
							err_msg_n(errno,"setting affinity for PID %d",
								current->pid);
						else
							cont("PID %d reassigned to CPU%d\n", current->pid, 
								current->param->rscs.affinity);
					}

					// only do if different than -1, <- not set values
					if (SCHED_NODATA != current->param->attr.sched_policy) {
						cont("Setting Scheduler of PID %d to '%s'\n", current->pid,
							policy_to_string(current->param->attr.sched_policy));
						if (sched_setattr (current->pid, &current->param->attr, 0U))
							err_msg_n(errno, "setting attributes for PID %d",
								current->pid);
					}
					else
						cont("Skipping setting of scheduler for PID %d\n", current->pid);  


					// controlling resource limits
          			struct rlimit rlim;		
					// TODO: upgrade to a list of parameters, looping through.			

					// RT-Time limit
					if (-1 != current->param->rscs.rt_timew || -1 != current->param->rscs.rt_time) {
						if (prlimit(current->pid, RLIMIT_RTTIME, NULL, &rlim))
							err_msg_n(errno, "getting RT-Limit for PID %d",
								current->pid);
						else {
							if (-1 != current->param->rscs.rt_timew)
								rlim.rlim_cur = current->param->rscs.rt_timew;
							if (-1 != current->param->rscs.rt_time)
								rlim.rlim_max = current->param->rscs.rt_time;
							if (prlimit(current->pid, RLIMIT_RTTIME, &rlim, NULL ))
								err_msg_n(errno,"setting RT-Limit for PID %d",
									current->pid);
							else
								cont("PID %d RT-Limit set to %d-%d\n", current->pid, 											rlim.rlim_cur, rlim.rlim_max);
						}
					}

					// Data limit - Heap.. unitialized or not
					if (-1 != current->param->rscs.mem_dataw || -1 != current->param->rscs.mem_data) {
						if (prlimit(current->pid, RLIMIT_DATA, NULL, &rlim))
							err_msg_n(errno, "getting Data-Limit for PID %d",
								current->pid);
						else {
							if (-1 != current->param->rscs.mem_dataw)
								rlim.rlim_cur = current->param->rscs.mem_dataw;
							if (-1 != current->param->rscs.mem_data)
								rlim.rlim_max = current->param->rscs.mem_data;
							if (prlimit(current->pid, RLIMIT_DATA, &rlim, NULL ))
								err_msg_n(errno, "setting Data-Limit for PID %d",
									current->pid);
							else
								cont("PID %d Data-Limit set to %d-%d\n", current->pid, 											rlim.rlim_cur, rlim.rlim_max);
						}
					}

				}
				else if (affother) {
					if (sched_setaffinity(current->pid, sizeof(cset), &cset ))
						err_msg_n(errno, "setting affinity for PID %d",
							current->pid);
					else
						cont("non-RT PID %d reassigned to CPU%d\n\n", current->pid,
							current->param->rscs.affinity);
				}
				else
					cont("Skipping non-RT PID %d from rescheduling\n", current->pid);
			}
		}

		// TODO: check if there is some faulty behaviour
//		if (current->mon->dl_overrun)
//			ovr++;

		current = current->next;

	}
	(void)pthread_mutex_unlock(&dataMutex);
	return ovr;
}

/// createResTracer(): create resource tracing memory elements
//
/// Arguments: 
///
/// Return value: N/D - int
///
int createResTracer(){
	// mask affinity and invert for system map / readout of smi of online CPUs
	for (int i=0;i<(affinity_mask->size);i++) 

		if (numa_bitmask_isbitset(affinity_mask, i)){ // filter by selected only
			rpush ( &rhead);
			rhead->affinity = i;
			rhead->basePeriod = 1;
	}		
	return 0;
}

/// manageSched(): main function called to reassign resources
///
/// Arguments: 
///
/// Return value: N/D - int
///
int manageSched(){

	// TODO: this is for the dynamic and adaptive scheduler only

    node_t * current = head;

	(void)pthread_mutex_lock(&dataMutex);

	while (current != NULL) {

        current = current->next;
    }


	(void)pthread_mutex_unlock(&dataMutex); 

	return 0;
}

/// thread_manage(): thread function call to manage schedule list
///
/// Arguments: - thread state/state machine, passed on to allow main thread stop
///
/// Return value: Exit Code - o for no error - EXIT_SUCCESS
void *thread_manage (void *arg)
{
	// be explicit!
	int32_t* pthread_state = (int32_t *)arg;
	// initialize the thread locals
	while(1)
	{
	  switch( *pthread_state )
	  {
	  case 0: // setup thread
		*pthread_state=1; // first thing
		if (readParams() != 0){
			err_msg("JSON configuration read failed!\n" KRED "Thread stopped.\n" KNRM);
			*pthread_state=-1;
			break;
		}
		// set lolcal variable -- all cpus set.
		// TODO: adapt to cpu mask
		for (int i=0; i<sizeof(cset_full); CPU_SET(i,&cset_full) ,i++);
	  case 1: // normal thread loop, check and update data
		if (!updateSched())
			break;
	  case 2: //
		// update resources
		*pthread_state=1;
		(void)manageSched();
		break;

	  case -1:
		// tidy or whatever is necessary
		pthread_exit(0); // exit the thread signalling normal return
		break;
	  }
	  sleep(1);
	}
	// TODO: Start using return value
	return EXIT_SUCCESS;
}

