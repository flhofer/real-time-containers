#include "schedstat.h"
#include "manage.h"
#include "pidparm.h"

// parameter tree linked list head
parm_t * phead;

static int handlepolicy(char *polname)
{
	if (strncasecmp(polname, "other", 5) == 0)
		return SCHED_OTHER;
	else if (strncasecmp(polname, "batch", 5) == 0)
		return SCHED_BATCH;
	else if (strncasecmp(polname, "idle", 4) == 0)
		return SCHED_IDLE;
	else if (strncasecmp(polname, "fifo", 4) == 0)
		return SCHED_FIFO;
	else if (strncasecmp(polname, "rr", 2) == 0)
		return SCHED_RR;
	else	/* default policy if we don't recognize the request */
		return SCHED_OTHER;
}

static char *policyname(int policy)
{
	char *policystr = "";

	switch(policy) {
	case SCHED_OTHER:
		policystr = "other";
		break;
	case SCHED_FIFO:
		policystr = "fifo";
		break;
	case SCHED_RR:
		policystr = "rr";
		break;
	case SCHED_BATCH:
		policystr = "batch";
		break;
	case SCHED_IDLE:
		policystr = "idle";
		break;
	}
	return policystr;
}


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


const char *keys[] = {"cmd", "params", "policy", "flags", "nice", "prio", "runtime", "deadline", "period"};

static int extractJSON(const char *js, jsmntok_t *t, size_t count, int indent, int key) {
	int i, j, k;


	// TODO: len check and case not matching type, also key > than values

	// base case, no elements in the object
	if (count == 0) {
		return 0;
	}

	// check key!!
/*	if (key<-1 || key > length(keys)) {
		// faulty data! 
		printDbg("JSON: faulty key selection data!");
		return 0; // 0 or 1? or count? check?
	}*/

	// setting value, primitive
	if (t->type == JSMN_PRIMITIVE) {
		// enter here if a primitive value (int?) has been identified

		if (key == -1) { // no key value yet
			printDbg("JSON: faulty key selection data! %.*s", t->end - t->start, js+t->start);
			return 1; // 0 or 1? or count? check?
		}

		// here len must be shorter than SIG_LEN 
		char c[SIG_LEN]; // buffer size for tem
		if (!phead) {
			printDbg("JSON: no element! %.*s", t->end - t->start, js+t->start);
			return 1;
		}

		switch (key) {

		case 2: // schedule policy 
				sprintf(c, "%.*s", t->end - t->start, js+t->start);
				phead->attr.sched_policy = handlepolicy(c);
				printDbg("JSON: setting scheduler to '%s'", policyname(phead->attr.sched_policy));
				break;
		}

		printDbg("%.*s", t->end - t->start, js+t->start);
		return 1;
	
	// setting value or label
	} else if (t->type == JSMN_STRING) {
		// here len must be shorter than SIG_LEN
		// a key has been identified
		if (key < 0){
			char c[SIG_LEN]; // buffer size for tem
			size_t len = sizeof(keys)/sizeof(c[0]); // count of keys

			// copy key to temp string buffer
			sprintf(c ,"%.*s", t->end - t->start, js+t->start);
			
			for (i=0; i<len; i++) 
				if (!strcasecmp(c, keys[i])){
					// key match.. find value for it
					printDbg("'%.*s': ", t->end - t->start, js+t->start);
					j = 0;					
					j += extractJSON(js, t+1+j, count-j, indent+1, i);   // key evaluation
					return j+1;
					break;
				}
			// if it get's here, key has not been found in list
			printDbg("NOT FOUND '%.*s'", t->end - t->start, js+t->start);
			return 1;
		}
		else
		{

			if (!phead) { // !! no object defined !!!
				printDbg("JSON: no element! '%.*s'", t->end - t->start, js+t->start);
				return 1;
			}

			switch (key) {
			
			case 0: // process/command signature found
					sprintf(phead->psig, "%.*s", t->end - t->start, js+t->start);
					printDbg("JSON: setting cmd to '%s'", phead->psig);
					break;

			case 2: ; // schedule policy 
					char c[SIG_LEN]; // buffer size for tem
					sprintf(c, "%.*s", t->end - t->start, js+t->start);
					phead->attr.sched_policy = handlepolicy(c);
					printDbg("JSON: setting scheduler to '%s'", policyname(phead->attr.sched_policy));
					break;

			}

			// all string values end up here
			printDbg("'%.*s'", t->end - t->start, js+t->start);

			return 1;
		}

	// Process composed objects ( {} }
	} else if (t->type == JSMN_OBJECT) {

		// add a new head
		if (indent == 2) {
			printDbg("Adding new item:\n");
			ppush (&phead);
		}
		

		// printout 
		printDbg("\n");
		j = 0;
		for (i = 0; i < t->size; i++) {
			for (k = 0; k < indent; k++) printDbg("  "); // print indent

			// we evaluate only the key here, value is taken in cascade
			j += extractJSON(js, t+1+j, count-j, indent+1, -1);   // key evaluation

			// printDbg(": ");  // separator

			//j += extractJSON(js, t+1+j, count-j, indent+1, -1); // value evaluation
			// if value is an object, it will contain aga`in keys..
			printDbg("\n");
		}
		return j+1;

	// process arrays ( [] )
	} else if (t->type == JSMN_ARRAY) {
		j = 0;
		printDbg("\n");
		for (i = 0; i < t->size; i++) {
			for (k = 0; k < indent-1; k++) printDbg("  ");
			printDbg("   - ");
			j += extractJSON(js, t+1+j, count-j, indent+1, -1);
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
/// thing is 
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
	if (tok == NULL) {
		fprintf(stderr, "malloc(): errno=%d\n", errno);
		return 3;
	}

	FILE * f = fopen ("config.json", "r");

	if (f){
	for (;;) {
		/* Read another chunk */
		r = fread(buf, 1, sizeof(buf), f);
		if (r < 0) {
			fprintf(stderr, "fread(): %d, errno=%d\n", r, errno);
			return 1;
		}
		if (r == 0) {
			if (eof_expected != 0) {
				return 0;
			} else {
				fprintf(stderr, "fread(): unexpected EOF\n");
				return 2;
			}
		}

		js = realloc_it(js, jslen + r + 1);
		if (js == NULL) {
			return 3;
		}
		strncpy(js + jslen, buf, r);
		jslen = jslen + r;

again:
		r = jsmn_parse(&p, js, jslen, tok, tokcount);
		if (r < 0) {
			if (r == JSMN_ERROR_NOMEM) {
				tokcount = tokcount * 2;
				tok = realloc_it(tok, sizeof(*tok) * tokcount);
				if (tok == NULL) {
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

// working item now
parm_t * now;

/// updateSched(): main function called to verify running schedule
//
/// Arguments: 
///
/// Return value: N/D
///
int manageSched(){
	uint64_t cputimes[MAX_CPUS] = {}; 
	uint64_t cpuperiod[MAX_CPUS] = {}; 
	cpu_set_t cset;

	// zero cpu-set, static size set
	CPU_ZERO(&cset);
	CPU_SET(0, &cset);

	pthread_mutex_lock(&dataMutex);

    node_t * current = head;
	while (current != NULL) {
		// get schedule of new pids
		if (current->attr.size == 0) {
			struct timespec tt;
			
			int ret = sched_rr_get_interval(current->pid, &tt);
			printDbg("Schedule pid %d: %d %ld\n", current->pid, ret, tt.tv_nsec);

			ret = sched_getattr (current->pid, &(current->attr), sizeof(node_t), 0U);
			printDbg("Attr: %d %d\n", ret, current->attr.sched_policy);

			ret = sched_setaffinity(current->pid, sizeof(cset), &cset );
			if (ret == 0)
				printDbg("Pid %d reassigned to CPU0\n", current->pid);

			// TODO: ret value evaluation 
		}

		// affinity not set?? default is 0, affinity of system stuff

		// sum of cpu-times, affinity is only 1 cpu here
		cputimes[current->affinity] += current->attr.sched_deadline;
		cpuperiod[current->affinity] += current->attr.sched_deadline;

        current = current->next;
    }


	pthread_mutex_unlock(&dataMutex);
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
		if (readParams() == 0)
			*pthread_state=-1;
		break;
	  case 1: // normal thread loop
		manageSched();
		break;
	  case -1:
		// tidy or whatever is necessary
		pthread_exit(0); // exit the thread signalling normal return
		break;
	  case 2: //
		// do something special
		
		break;
	  }
	  sleep(1);
	}
}

